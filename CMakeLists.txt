cmake_minimum_required(VERSION 3.20)
project(market_data_replay LANGUAGES CXX)

include(CTest)

if(BUILD_TESTING)
    add_executable(market_data_replay_tests main.cpp)
    set_target_properties(market_data_replay_tests PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    # The existing tests use assert, including in Release builds.
    if(MSVC)
        target_compile_options(market_data_replay_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(market_data_replay_tests PRIVATE -UNDEBUG)
    endif()

    add_test(NAME market_data_replay COMMAND market_data_replay_tests)
endif()
