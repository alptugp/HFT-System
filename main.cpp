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
    pinThread(cpu1);
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
    pinThread(cpu2);
    
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

void runAlgo(int cpu1, int cpu2, int cpu3)  {   
    const size_t queueSize = 10000;
    SPSCQueue<OrderBook> builderToStrategyQueue(queueSize);
    SPSCQueue<std::string> strategyToOrderManagerQueue(queueSize);

    auto t1 = std::thread([&cpu1, &builderToStrategyQueue, &strategyToOrderManagerQueue] {
      strategy(cpu1, builderToStrategyQueue, strategyToOrderManagerQueue);
    });

    auto t2 = std::thread([&cpu2, &builderToStrategyQueue] {
      bookBuilder(cpu2, builderToStrategyQueue);
    });

    /*auto t3 = std::thread([&cpu3, &strategyToOrderManagerQueue] {
      orderManager(cpu3, strategyToOrderManagerQueue);
    });*/

    t2.join();
    t1.join();
    /*t3.join();*/
}

int main(int argc, char *argv[]) {
  int cpu1 = -1;
  int cpu2 = -1;
  int cpu3 = -1;
  
  if (argc == 4) {
    cpu1 = std::stoi(argv[1]);
    cpu2 = std::stoi(argv[2]);
    cpu3 = std::stoi(argv[3]);
  }

  // bench<SPSCQueue<OrderBook>>(cpu1, cpu2);
  runAlgo(cpu1, cpu2, cpu3);

  return 0;
}