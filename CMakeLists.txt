cmake_minimum_required(VERSION 3.22)

project(main)

set(WEBSOCKETPP_INCLUDE_DIR "/vol/bitbucket/aap221/websocketpp")
set(BITMEX_INCLUDE_DIR "/vol/bitbucket/aap221/bitmex-api/include")
set(RAPIDJSON_INCLUDE_DIR "/vol/bitbucket/aap221/rapidjson/include")

include_directories(${WEBSOCKETPP_INCLUDE_DIR} ${BITMEX_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR} ./BookBuilder/OrderBook ./BookBuilder/ThroughputMonitor)

add_executable(main main.cpp ./BookBuilder/OrderBook/OrderBook.cpp ./BookBuilder/ThroughputMonitor/ThroughputMonitor.cpp)

target_compile_features(main PUBLIC cxx_std_11)

target_compile_options(main PRIVATE -Wall -Wextra -pedantic -Werror)

target_link_libraries(main PRIVATE pthread crypto ssl)
