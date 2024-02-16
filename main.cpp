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
#include "Strategy/Graph.cpp"

void pinThread(int cpu) {
  if (cpu < 0) {
    return;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == -1) {
    perror("pthread_setaffinity_no");
    exit(1);
  }
}

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
    Graph graph(3);

    std::unordered_map<std::string, int> symbolToGraphIndex;   
    symbolToGraphIndex["USD"] = 0;
    symbolToGraphIndex["XBT"] = 1;
    symbolToGraphIndex["ETH"] = 2;

    auto t1 = std::thread([&] {
      pinThread(cpu1);
      
      while (true) {
        OrderBook orderBook;
        while (!queue.pop(orderBook));
        std::pair<double, double> bestBuyAndSellPrice = orderBook.getBestBuyAndSellPrice();
        double bestBuyPrice = bestBuyAndSellPrice.first;
        double bestSellPrice = bestBuyAndSellPrice.second;
        std::string symbol = orderBook.getSymbol();
        int firstCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(0, 3)];
        int secondCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(3, 6)];

        graph.addEdge(firstCurrencyGraphIndex, secondCurrencyGraphIndex, bestSellPrice);
        graph.addEdge(secondCurrencyGraphIndex, firstCurrencyGraphIndex, 1.0 / bestBuyPrice);

        std::pair<double, double> returns = graph.findTriangularArbitrage();

        double firstDirectionReturnsAfterFees = returns.first * std::pow(0.99925, 3);
        double secondDirectionReturnsAfterFees = returns.second * std::pow(0.99925, 3);
      
        // std::cout << symbol << " - Best Sell: " << bestSellPrice << " Best Buy: " << bestBuyPrice << std::endl;
        // std::cout << "USD -> XBT -> ETH -> USD: " << firstDirectionReturnsAfterFees << "      " << "USD -> ETH -> XBT -> USD: " << secondDirectionReturnsAfterFees << std::endl;
      }
    });
    
    auto t2 = std::thread([&] {
      pinThread(cpu2);
    
      OrderBook XBTUSDOrderBook = OrderBook("XBTUSD");
      OrderBook ETHUSDOrderBook = OrderBook("ETHUSD");
      OrderBook XBTETHOrderBook = OrderBook("XBTETH");
      std::unordered_map<std::string, OrderBook> orderBookMap;
      orderBookMap["XBTUSD"] = XBTUSDOrderBook;
      orderBookMap["ETHUSD"] = ETHUSDOrderBook;
      orderBookMap["XBTETH"] = XBTETHOrderBook;

      ThroughputMonitor throughputMonitor(std::chrono::high_resolution_clock::now());
      
      bitmex::websocket::Client bmxClient;

      auto onTradeCallBackLambda = [&orderBookMap, &throughputMonitor, &queue]([[maybe_unused]] const char* symbol, const char* action, 
        uint64_t id, const char* side, int size, double price, const char* timestamp) {
        onTradeCallBack(orderBookMap, throughputMonitor, queue, symbol, action, id, side, size, price, timestamp);
      };

      bmxClient.on_trade(onTradeCallBackLambda);

      std::string uri = "wss://www.bitmex.com/realtime";
      client c;
      c.clear_access_channels(websocketpp::log::alevel::frame_payload);
      c.init_asio();
      c.set_tls_init_handler(&on_tls_init);
      c.set_open_handler(bind(&on_open, &c, &bmxClient, ::_1));
      c.set_message_handler(bind(&on_message, &bmxClient, ::_1, ::_2));

      websocketpp::lib::error_code ec;
      client::connection_ptr con = c.get_connection(uri, ec);
      if (ec) {
          std::cout << "Failed to create connection: " << ec.message() << std::endl;
          return 1;
      }
      c.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

      c.connect(con);

      c.run();

      return 0;
    });

    t2.join();
    t1.join();
}

int main(int argc, char *argv[]) {
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