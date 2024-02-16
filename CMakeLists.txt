cmake_minimum_required(VERSION 3.22)

project(main)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/websocketpp
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/rapidjson/include
)

add_executable(main main.cpp ./BookBuilder/OrderBook/OrderBook.cpp ./BookBuilder/ThroughputMonitor/ThroughputMonitor.cpp)

target_compile_features(main PUBLIC cxx_std_11)

target_compile_options(main PRIVATE -Wall -Wextra -pedantic -Werror)

target_link_libraries(main PRIVATE pthread crypto ssl)
