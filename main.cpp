// © 2021 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: CC0-1.0

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include "SPSCQueue/SPSCQueue.hpp"
#include "BookBuilder/OrderBook/OrderBook.hpp"
#include "BookBuilder/ThroughputMonitor/ThroughputMonitor.hpp"
#include "BookBuilder/BookBuilder.cpp"
#include "BookBuilder/BookBuilderGateway.cpp"
#include "Utils/Utils.hpp"
#include "StrategyComponent/Strategy.hpp"
#include "OrderManager/OrderManager.hpp"

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)  
static const std::vector<std::string> currencyPairs = {"XBTETH", "XBTUSDT", "ETHUSDT"};
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)  
static const std::vector<std::string> currencyPairs = {"ETH/BTC", "BTC/USD", "ETH/USD"};
#endif


template <typename T> 
void bench(int cpu1, int cpu2) {
  const size_t queueSize = 100000;
  const int64_t iters = 100000000;

  T q(queueSize);
  auto t1 = std::thread([&] {
    setThreadAffinity(pthread_self(), cpu1);
    for (int i = 0; i < iters; ++i) {
      OrderBook orderBook;
      while (!q.pop(orderBook));
      if (orderBook.getCurrencyPairSymbol() != std::to_string(i)) {
        throw std::runtime_error("");
      }
    }
  });

  auto start = std::chrono::steady_clock::now();  

  auto t2 = std::thread([&] {
    setThreadAffinity(pthread_self(), cpu2);
    
    for (int i = 0; i < iters; ++i) {
      OrderBook orderBook(std::to_string(i));
      while (!q.push(orderBook));
    }

    while (q.readIdx_.load(std::memory_order_relaxed) != q.writeIdx_.load(std::memory_order_relaxed));

    auto stop = std::chrono::steady_clock::now();
    std::cout << iters * 1000000000 / std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count() << " ops/s" << std::endl;
  });
  
  t1.join();
  t2.join();
}

void runTradingSystem()  {   
    const size_t queueSize = 10000;
    SPSCQueue<BookBuilderGatewayToComponentQueueEntry> bookBuilderGatewayToComponentQueue(queueSize);
    SPSCQueue<OrderBook> builderToStrategyQueue(queueSize);
    SPSCQueue<std::string> strategyToOrderManagerQueue(queueSize);
    
    int pipefd[2]; 
    // Create a pipe for communication between Book Builder and Order Manager
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    int bookBuilderPipeEnd = pipefd[0];
    int orderManagerPipeEnd = pipefd[1];

    auto strategyThread = std::thread([&builderToStrategyQueue, &strategyToOrderManagerQueue] {
      strategy(builderToStrategyQueue, strategyToOrderManagerQueue);
    });

    auto bookBuilderGatewayThread = std::thread([&bookBuilderGatewayToComponentQueue, orderManagerPipeEnd, currencyPairs = currencyPairs] {
      bookBuilderGateway(bookBuilderGatewayToComponentQueue, currencyPairs, orderManagerPipeEnd);
    });

    auto bookBuilderThread = std::thread([&bookBuilderGatewayToComponentQueue, &builderToStrategyQueue, currencyPairs = currencyPairs] {
      bookBuilder(bookBuilderGatewayToComponentQueue, builderToStrategyQueue, currencyPairs);
    });

    auto orderManagerThread = std::thread([&strategyToOrderManagerQueue, bookBuilderPipeEnd] {
      orderManager(strategyToOrderManagerQueue, bookBuilderPipeEnd);
    });

    bookBuilderGatewayThread.join();
    bookBuilderThread.join();
    strategyThread.join();
    orderManagerThread.join();
}

int main(int argc, char *argv[]) {
  // int cpu1 = 1;
  // int cpu2 = 2;
  // bench<SPSCQueue<OrderBook>>(cpu1, cpu2);
  runTradingSystem();

  return 0;
}