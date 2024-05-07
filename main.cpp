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
#include "Utils/Utils.hpp"
#include "StrategyComponent/Graph.hpp"
#include "OrderManager/OrderManager.hpp"

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
      if (orderBook.getSymbol() != std::to_string(i)) {
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

void runAlgo()  {   
    const size_t queueSize = 10000;
    SPSCQueue<OrderBook> builderToStrategyQueue(queueSize);
    SPSCQueue<std::string> strategyToOrderManagerQueue(queueSize);

    auto t1 = std::thread([&builderToStrategyQueue, &strategyToOrderManagerQueue] {
      strategy(builderToStrategyQueue, strategyToOrderManagerQueue);
    });

    auto t2 = std::thread([&builderToStrategyQueue] {
      bookBuilder(builderToStrategyQueue);
    });

    auto t3 = std::thread([&strategyToOrderManagerQueue] {
      orderManager(strategyToOrderManagerQueue);
    });

    t2.join();
    t1.join();
    t3.join();
}

int main(int argc, char *argv[]) {
  // int cpu1 = 1;
  // int cpu2 = 2;
  // bench<SPSCQueue<OrderBook>>(cpu1, cpu2);
  runAlgo();

  return 0;
}