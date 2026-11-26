# Market Data Replay and Order Book Reconstruction

A C++20 project that reconstructs a visible limit order book by replaying exchange messages in file order. The first milestone is an end-to-end replay of AAPL on Nasdaq BX for December 30, 2019. The current prototype runs on hand-built binary records.

## Build and test

Requires CMake 3.20 or later and a C++20 compiler. The tests use in-memory inputs and require no market data download.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs one executable with five scenarios. In the binary add, execute, and cancel case, a 100-share AAPL bid at $100.00 fills for 30 shares and cancels 20, leaving 50 shares at the best bid.

## Current implementation

- Tracks resting orders by ID and computes the best bid and ask with aggregate share counts at those prices. Quote queries scan the active orders.
- Supports adding, reducing, deleting, and replacing orders in the book. The binary replay currently handles add (`A`), execute (`E`), and cancel (`X`) messages.
- Reads big-endian, length-prefixed BinaryFILE records and reports truncated input or unsupported message types. [Nasdaq BinaryFILE specification](https://nasdaqtrader.com/content/technicalSupport/specifications/dataproducts/binaryfile.pdf)

## Next milestones

1. Resolve AAPL's stock locate code from the stock directory and handle the remaining order updates required for a full-day replay.
2. Emit one-second best-bid and best-ask snapshots and compare quotes against a simple reference implementation on a fixed slice of real data.
3. Measure full replay throughput and per-update latency, profile the baseline, and verify quote parity after one optimization.

The first historical input is `20191230.BX_ITCH_50.gz`. [Nasdaq BX historical sample directory](https://emi.nasdaq.com/ITCH/Nasdaq%20BX%20ITCH/) · [Nasdaq BX TotalView-ITCH 5.0 specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/BXTVITCHSpecification.pdf)
