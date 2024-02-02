cmake_minimum_required(VERSION 3.22)

project(xbtusd-builder)

set(WEBSOCKETPP_INCLUDE_DIR "/vol/bitbucket/aap221/websocketpp")
set(BITMEX_INCLUDE_DIR "/vol/bitbucket/aap221/bitmex-api/include")
set(RAPIDJSON_INCLUDE_DIR "/vol/bitbucket/aap221/rapidjson/include")

include_directories(${WEBSOCKETPP_INCLUDE_DIR} ${BITMEX_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR} ./order_book ./throughput_monitor)

add_executable(xbtusd-builder xbtusd-builder.cpp ./order_book/order_book.cpp ./throughput_monitor/throughput_monitor.cpp)

target_compile_features(xbtusd-builder PUBLIC cxx_std_11)

target_compile_options(xbtusd-builder PRIVATE -Wall -Wextra -pedantic -Werror)

target_link_libraries(xbtusd-builder PRIVATE pthread crypto ssl)
