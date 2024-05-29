cmake_minimum_required(VERSION 3.22)
project(main)

# set(CMAKE_CXX_STANDARD 17)

find_package(libwebsockets REQUIRED)

find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIR})

find_package(Boost REQUIRED COMPONENTS graph)
include_directories(${Boost_INCLUDE_DIRS})

add_compile_options(-msse4.2)
add_definitions(-DRAPIDJSON_SSE42)
add_definitions(-DLWS_WITH_LIBEV=1)
add_definitions(-DUSE_KRAKEN_MOCK_EXCHANGE=1)
# add_definitions(-DUSE_BITMEX_MOCK_EXCHANGE=1)
# add_definitions(-DUSE_BITMEX_EXCHANGE=1)
# add_definitions(-DUSE_KRAKEN_EXCHANGE=1)

include_directories("/homes/aap221/local/include")
link_directories("/homes/aap221/local/lib")

# Source Files
set(SOURCES
    ${PROJECT_NAME}.cpp
    ./BookBuilder/OrderBook/OrderBook.cpp
    ./OrderManager/OrderManager.cpp
    ./BookBuilder/ThroughputMonitor/ThroughputMonitor.cpp
    ./Utils/Utils.cpp
    ./StrategyComponent/Strategy.cpp
    ./OrderManager/ThreadPool.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Compiler Features and Options
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)

# Linked Libraries
target_link_libraries(${PROJECT_NAME} PRIVATE uring pthread crypto ssl curl websockets ev Eigen3::Eigen ${Boost_LIBRARIES})