cmake_minimum_required(VERSION 3.22)
project(main)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(libwebsockets REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS graph)

# Include directories
include_directories(${EIGEN3_INCLUDE_DIR})
include_directories(${Boost_INCLUDE_DIRS})

# Add compile options
add_compile_options(-msse4.2)

# Options for different exchanges
option(USE_BITMEX_TESTNET_EXCHANGE "Use BitMEX Testnet Exchange" OFF)
option(USE_KRAKEN_MOCK_EXCHANGE "Use Kraken Mock Exchange" OFF)
option(USE_BITMEX_MOCK_EXCHANGE "Use BitMEX Mock Exchange" OFF)
option(USE_BITMEX_EXCHANGE "Use BitMEX Exchange" OFF)
option(USE_KRAKEN_EXCHANGE "Use Kraken Exchange" OFF)

# Verbose options
option(VERBOSE_BOOK_BUILDER "Enable verbose output for the Book Builder" OFF)
option(VERBOSE_STRATEGY "Enable verbose output for the Strategy" OFF)

# Portfolio options
option(USE_PORTFOLIO_122 "Use the optimized portfolio with 122 currency pairs" OFF)
option(USE_PORTFOLIO_92 "Use the optimized portfolio with 92 currency pairs" OFF)
option(USE_PORTFOLIO_50 "Use the optimized portfolio with 50 currency pairs" OFF)
option(USE_PORTFOLIO_3 "Use the optimized portfolio with 3 currency pairs" OFF)

# Set definitions based on options
if (USE_PORTFOLIO_122)
    add_definitions(-DUSE_PORTFOLIO_122=1)
elseif (USE_PORTFOLIO_92)
    add_definitions(-DUSE_PORTFOLIO_92=1)
elseif (USE_PORTFOLIO_50)
    add_definitions(-DUSE_PORTFOLIO_50=1)
elseif (USE_PORTFOLIO_3)
    add_definitions(-DUSE_PORTFOLIO_3=1)
else()
    message(FATAL_ERROR "You must specify one of the portfolio options.")
endif()

if(USE_BITMEX_TESTNET_EXCHANGE)
    add_definitions(-DUSE_BITMEX_TESTNET_EXCHANGE)
elseif(USE_KRAKEN_MOCK_EXCHANGE)
    add_definitions(-DUSE_KRAKEN_MOCK_EXCHANGE)
elseif(USE_BITMEX_MOCK_EXCHANGE)
    add_definitions(-DUSE_BITMEX_MOCK_EXCHANGE)
elseif(USE_BITMEX_EXCHANGE)
    add_definitions(-DUSE_BITMEX_EXCHANGE)
elseif(USE_KRAKEN_EXCHANGE)
    add_definitions(-DUSE_KRAKEN_EXCHANGE)
endif()

if(VERBOSE_BOOK_BUILDER)
    add_definitions(-DVERBOSE_BOOK_BUILDER)
endif()

if(VERBOSE_STRATEGY)
    add_definitions(-DVERBOSE_STRATEGY)
endif()

add_definitions(-DRAPIDJSON_SSE42)
add_definitions(-DLWS_WITH_LIBEV=1)

# Include and link directories
include_directories("/homes/aap221/local/include")
link_directories("/homes/aap221/local/lib")

# Source files
set(SOURCES
    ${PROJECT_NAME}.cpp
    ./OrderBook/OrderBook.cpp
    ./OrderManager/OrderManager.cpp
    ./Utils/ThroughputMonitor/ThroughputMonitor.cpp
    ./Utils/Utils.cpp
    ./StrategyComponent/Strategy.cpp
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Compiler features and options
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    uring
    pthread
    crypto
    ssl
    curl
    websockets
    ev
    Eigen3::Eigen
    ${Boost_LIBRARIES}
)
