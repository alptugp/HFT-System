cmake_minimum_required(VERSION 3.22)

project(BookBuilder)

set(WEBSOCKETPP_INCLUDE_DIR "/vol/bitbucket/aap221/websocketpp")
set(BITMEX_INCLUDE_DIR "/vol/bitbucket/aap221/bitmex-api/include")
set(RAPIDJSON_INCLUDE_DIR "/vol/bitbucket/aap221/rapidjson/include")

include_directories(${WEBSOCKETPP_INCLUDE_DIR} ${BITMEX_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR} ./OrderBook ./ThroughputMonitor)

add_executable(BookBuilder BookBuilder.cpp ./OrderBook/OrderBook.cpp ./ThroughputMonitor/ThroughputMonitor.cpp)

target_compile_features(BookBuilder PUBLIC cxx_std_11)

target_compile_options(BookBuilder PRIVATE -Wall -Wextra -pedantic -Werror)

target_link_libraries(BookBuilder PRIVATE pthread crypto ssl)
