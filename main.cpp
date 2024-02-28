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

void buildBookAndDetectArbitrage(int cpu1, int cpu2)  {   
    const size_t queueSize = 100000;
    SPSCQueue<OrderBook> queue(queueSize);

    auto t1 = std::thread([&cpu1, &queue] {
      strategy(cpu1, queue);
    });

    auto t2 = std::thread([&cpu2, &queue] {
      bookBuilder(cpu2, queue);
    });

    t2.join();
    t1.join();
}

int main(int argc, char *argv[]) {
  std::setlocale(LC_ALL, "C");
  
  int cpu1 = -1;
  int cpu2 = -1;

  if (argc == 3) {
    cpu1 = std::stoi(argv[1]);
    cpu2 = std::stoi(argv[2]);
  }

  // bench<SPSCQueue<OrderBook>>(cpu1, cpu2);
  buildBookAndDetectArbitrage(cpu1, cpu2);

  return 0;
}