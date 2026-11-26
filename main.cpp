#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <array>
#include <cstddef>
#include <istream>
#include <sstream>
#include <string>
#include <vector>
#include <span>
#include <utility>

enum class Side { Buy, Sell };

struct Order {
    std::uint64_t id;
    Side side;
    std::int64_t price;  // 同一订单簿里的价格使用同一个整数单位。
    int remaining_qty;
};

struct Quote {
    std::int64_t price;
    std::int64_t qty;
};

class OrderBook {
public:
    void add(Order order) {
        if (order.remaining_qty <= 0) {
            throw std::invalid_argument("order quantity must be positive");
        }
        if (!orders_.emplace(order.id, order).second) {
            throw std::invalid_argument("order ID already exists");
        }
    }

    void reduce(std::uint64_t id, int qty) {
        auto& order = orders_.at(id);
        if (qty <= 0 || qty > order.remaining_qty) {
            throw std::invalid_argument("invalid quantity reduction");
        }
        order.remaining_qty -= qty;
        if (order.remaining_qty == 0) {
            orders_.erase(id);
        }
    }
    void erase(std::uint64_t id) {
        if (orders_.erase(id) == 0) {
            throw std::out_of_range("order ID not found");
        }
    }

    void replace(std::uint64_t old_id, std::uint64_t new_id,
                 std::int64_t new_price, int new_qty) {
        const Side side = orders_.at(old_id).side;
        // 新订单插入成功后再删除旧订单，避免替换失败时丢单。
        add({new_id, side, new_price, new_qty});
        orders_.erase(old_id);
    }

    std::optional<Quote> best_bid() const {
        return best_quote(Side::Buy);
    }

    std::optional<Quote> best_ask() const {
        return best_quote(Side::Sell);
    }

private:
    std::optional<Quote> best_quote(Side side) const {
        std::unordered_map<std::int64_t, std::int64_t> price_to_qty;
        std::optional<std::int64_t> best_price;

        for (const auto& [id, order] : orders_) {
            if (order.side != side) {
                continue;
            }

            price_to_qty[order.price] += order.remaining_qty;
            if (!best_price ||
                (side == Side::Buy ? order.price > *best_price : order.price < *best_price)) {
                best_price = order.price;
            }
        }

        if (!best_price) {
            return std::nullopt;
        }
        return Quote{*best_price, price_to_qty.at(*best_price)};
    }
    std::unordered_map<std::uint64_t, Order> orders_;
};

std::uint64_t read_be(std::span<const std::uint8_t> field) {
    std::uint64_t value = 0;
    for (const auto byte : field) {
        value = value * 256u + byte;
    }
    return value;
}

bool apply_add_order(OrderBook& book, std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload[0] != 'A') {
        return false;
    }
    if (payload.size() != 36) {
        throw std::invalid_argument("invalid A message length");
    }

    const auto direction = payload[19];
    if (direction != 'B' && direction != 'S') {
        throw std::invalid_argument("invalid order side");
    }

    const auto order_id = read_be(payload.subspan(11, 8));
    const auto shares = read_be(payload.subspan(20, 4));
    const auto price = read_be(payload.subspan(32, 4));
    if (!std::in_range<int>(shares)) {
        throw std::out_of_range("share count exceeds int range");
    }

    const Side side = direction == 'B' ? Side::Buy : Side::Sell;
    book.add({order_id, side, static_cast<std::int64_t>(price),
              static_cast<int>(shares)});
    return true;
}

bool apply_reduction(OrderBook& book, std::span<const std::uint8_t> payload) {
    if (payload.empty()) {
        throw std::invalid_argument("empty ITCH payload");
    }
    const auto type = payload[0];
    if (type != 'E' && type != 'X') {
        return false;
    }

    const std::size_t expected_size = type == 'E' ? 31 : 23;
    if (payload.size() != expected_size) {
        throw std::invalid_argument("invalid reduction message length");
    }

    const auto order_id = read_be(payload.subspan(11, 8));
    const auto shares = read_be(payload.subspan(19, 4));

    if (!std::in_range<int>(shares)) {
        throw std::out_of_range("share count exceeds int range");
    }
    book.reduce(order_id, static_cast<int>(shares));
    return true;
}

std::vector<std::uint8_t> read_payload(std::istream& input) {
    std::array<std::uint8_t, 2> length_bytes{};
    if (!input.read(reinterpret_cast<char*>(length_bytes.data()), 2)) {
        throw std::runtime_error("missing or truncated length header");
    }

    const std::size_t length = length_bytes[0] * 256u + length_bytes[1];
    std::vector<std::uint8_t> payload(length);

    if (length != 0 &&
        !input.read(reinterpret_cast<char*>(payload.data()),
                    static_cast<std::streamsize>(length))) {
        throw std::runtime_error("truncated message payload");
    }

    return payload;
}

void replay_messages(OrderBook& book, std::istream& input) {
    while (true) {
        const auto payload = read_payload(input);
        if (payload.empty()) {
            return;  // 读到了 00 00 结束标记。
        }
        if (!apply_add_order(book, payload) && !apply_reduction(book, payload)) {
            throw std::invalid_argument("unsupported message type");
        }
    }
}

void test_replay_two_executions() {
    OrderBook book;
    book.add({1, Side::Buy, 10000, 100});

    std::array<std::uint8_t, 31> payload{};
    payload[0] = 'E';
    payload[18] = 1;  // 两次成交都更新订单 1。

    std::string records;
    for (const auto qty : std::array<std::uint8_t, 2>{30, 20}) {
        payload[22] = qty;
        records.append("\x00\x1F", 2);
        records.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    }
    records.append("\x00\x00", 2);

    std::istringstream input(records);
    replay_messages(book, input);

    const auto bid = book.best_bid();
    assert(bid && bid->price == 10000 && bid->qty == 50);
}

std::string make_add_execute_records() {
    std::array<std::uint8_t, 36> add{};
    add[0] = 'A';
    add[2] = 1;   // 手写用例的证券定位码。
    add[18] = 1;  // 订单编号 1。
    add[19] = 'B';
    add[23] = 100;
    const std::string symbol = "AAPL    ";
    for (std::size_t i = 0; i < symbol.size(); ++i) {
        add[24 + i] = static_cast<std::uint8_t>(symbol[i]);
    }

    // Price(4): 00 0F 42 40 表示 1'000'000，即 100.00 美元。
    add[33] = 0x0F;
    add[34] = 0x42;
    add[35] = 0x40;

    std::array<std::uint8_t, 31> execution{};
    execution[0] = 'E';
    execution[2] = 1;
    execution[18] = 1;
    execution[22] = 30;
    execution[30] = 1;

    std::string records{"\x00\x24", 2};
    records.append(reinterpret_cast<const char*>(add.data()), add.size());
    records.append("\x00\x1F", 2);
    records.append(reinterpret_cast<const char*>(execution.data()), execution.size());
    return records;
}

void test_replay_add_then_execute() {
    OrderBook book;
    assert(!book.best_bid());

    auto records = make_add_execute_records();
    records.append("\x00\x00", 2);

    std::istringstream input(records);
    replay_messages(book, input);

    const auto bid = book.best_bid();
    assert(bid && bid->price == 1'000'000 && bid->qty == 70);
}

void test_replay_add_execute_then_cancel() {
    OrderBook book;
    assert(!book.best_bid());

    std::array<std::uint8_t, 23> cancel{};
    cancel[0] = 'X';
    cancel[2] = 1;   // 和挂单消息使用相同的证券定位码。
    cancel[18] = 1;  // 订单编号 1。
    cancel[22] = 20;

    auto records = make_add_execute_records();
    records.append("\x00\x17", 2);
    records.append(reinterpret_cast<const char*>(cancel.data()), cancel.size());
    records.append("\x00\x00", 2);

    std::istringstream input(records);
    replay_messages(book, input);

    const auto bid = book.best_bid();
    assert(bid && bid->price == 1'000'000 && bid->qty == 50);
}

void test_buy_order_lifecycle() {
    OrderBook book;
    // 第 1 步：A 挂买单。
    book.add({1, Side::Buy, 10000, 100});
    const auto after_add_a = book.best_bid();
    assert(after_add_a && after_add_a->price == 10000 && after_add_a->qty == 100);

    // 第 2 步：B 在更低的价位挂买单。
    book.add({2, Side::Buy, 9999, 60});
    const auto after_add_b = book.best_bid();
    assert(after_add_b && after_add_b->price == 10000 && after_add_b->qty == 100);

    // 第 3 步：A 成交 30 股。
    std::array<std::uint8_t, 31> payload{};
    payload[0] = 'E';
    payload[18] = 0x01;  // 订单编号 1
    payload[22] = 0x1E;  // 成交 30 股

    std::string record{"\x00\x1F", 2};
    record.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    std::istringstream input(record);

    const auto message = read_payload(input);
    assert(message.size() == 31 && message[0] == 'E');
    const bool applied = apply_reduction(book, message);
    assert(applied);
    const auto after_fill = book.best_bid();
    assert(after_fill && after_fill->price == 10000 && after_fill->qty == 70);

    // 第 4 步：A 撤掉 20 股。
    book.reduce(1, 20);
    const auto after_cancel = book.best_bid();
    assert(after_cancel && after_cancel->price == 10000 && after_cancel->qty == 50);

    // 第 5 步：删除 A 的剩余订单。
    book.erase(1);
    const auto after_delete = book.best_bid();
    assert(after_delete && after_delete->price == 9999 && after_delete->qty == 60);
}

void test_sell_order_replacement() {
    OrderBook book;
    assert(!book.best_bid() && !book.best_ask());

    book.add({1, Side::Sell, 10001, 40});
    book.add({2, Side::Sell, 10002, 60});
    const auto before_replace = book.best_ask();
    assert(before_replace && before_replace->price == 10001 && before_replace->qty == 40);
    assert(!book.best_bid());

    // A 的旧价位清空，新订单 C 与 B 在 10002 美分这一档。
    book.replace(1, 3, 10002, 10);
    const auto after_replace = book.best_ask();
    assert(after_replace && after_replace->price == 10002 && after_replace->qty == 70);

    book.reduce(3, 10);
    const auto after_fill = book.best_ask();
    assert(after_fill && after_fill->price == 10002 && after_fill->qty == 60);

    book.erase(2);
    assert(!book.best_ask());
}

int main() {
    test_buy_order_lifecycle();
    test_sell_order_replacement();
    test_replay_two_executions();
    test_replay_add_then_execute();
    test_replay_add_execute_then_cancel();
}
