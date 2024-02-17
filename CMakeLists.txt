cmake_minimum_required(VERSION 3.22)

project(main)

# Include Directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/websocketpp
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/rapidjson/include
)

# Source Files
set(SOURCES
    ${PROJECT_NAME}.cpp
    ./BookBuilder/OrderBook/OrderBook.cpp
    ./BookBuilder/ThroughputMonitor/ThroughputMonitor.cpp
    ./Utils/Utils.cpp
    ./StrategyComponent/Graph.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Compiler Features and Options
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Werror -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter)

# Linked Libraries
target_link_libraries(${PROJECT_NAME} PRIVATE pthread crypto ssl)
