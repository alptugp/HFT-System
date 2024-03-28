#include "Graph.hpp"

using namespace std::chrono;

Graph::Graph(int vertices) : V(vertices) {
    adjList.resize(V, std::vector<double>(V, 1.0));
}

void Graph::addEdge(int u, int v, double weight) {
    adjList[u][v] = weight;
}

std::pair<double, double> Graph::findTriangularArbitrage() {
    int startVertex = 0;
    std::vector<double> cycleWeightMultiplcations;

    for (int direction = 1; direction < V; direction++) {
        int currentNode = startVertex;
        int nextNode = direction;
        double weightMultiply = 1.0;
        do {
            weightMultiply *= adjList[currentNode][nextNode];
            for (int node = 0; node < V; node++) {
                if ((node != currentNode) && (node != nextNode)) {
                    currentNode = nextNode;
                    nextNode = node;
                    break;
                }
            }
        } while (currentNode != startVertex);
        cycleWeightMultiplcations.emplace_back(weightMultiply);
    }

    double weightMultiplyFirstDirection = cycleWeightMultiplcations[0];
    double weightMultiplySecondDirection = cycleWeightMultiplcations[1];
    return std::make_pair(weightMultiplyFirstDirection, weightMultiplySecondDirection);
}

double Graph::getExchangeRateBetween(int u, int v) {
    return adjList[u][v];
}

void strategy(int cpu, SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
    pinThread(cpu);
    Graph graph(3);

    std::unordered_map<std::string, int> symbolToGraphIndex;   
    symbolToGraphIndex["XBT"] = 0;
    symbolToGraphIndex["USDT"] = 1;
    symbolToGraphIndex["ETH"] = 2;

    ThroughputMonitor throughputMonitorStrategyComponent("Strategy Component Throughput Monitor", std::chrono::high_resolution_clock::now());
    while (true) {
      OrderBook orderBook;
      while (!builderToStrategyQueue.pop(orderBook));
      std::pair<double, double> bestBuyAndSellPrice = orderBook.getBestBuyAndSellPrice();
      double bestBuyPrice = bestBuyAndSellPrice.first;
      double bestSellPrice = bestBuyAndSellPrice.second;
      std::string symbol = orderBook.getSymbol();
      int firstCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(0, 3)];
      int secondCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(3, 6)];
      graph.addEdge(firstCurrencyGraphIndex, secondCurrencyGraphIndex, bestBuyPrice);
      graph.addEdge(secondCurrencyGraphIndex, firstCurrencyGraphIndex, 1.0 / bestSellPrice);
      std::pair<double, double> returns = graph.findTriangularArbitrage();
      double firstDirectionReturnsAfterFees = returns.first * std::pow(0.99925, 3);
      double secondDirectionReturnsAfterFees = returns.second * std::pow(0.99925, 3);
      // std::cout << symbol << " - Best Sell: " << bestSellPrice << " Best Buy: " << bestBuyPrice << std::endl;
      long exchangeTimestamp = orderBook.getExchangeTimestamp();
      auto exchangeTimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>(
                std::chrono::milliseconds(exchangeTimestamp));
      system_clock::time_point strategyTimestamp = high_resolution_clock::now();
      auto strategyTimepoint = std::to_string(duration_cast<milliseconds>(strategyTimestamp.time_since_epoch()).count());
      std::cout << "XBT->USDT->ETH->XBT: " << firstDirectionReturnsAfterFees << "      " 
                << "XBT->ETH->USDT->XBT: " << secondDirectionReturnsAfterFees << "      " 
                << "Builder to Strategy Latency (ms): " << duration_cast<milliseconds>(strategyTimestamp - exchangeTimePoint).count() << "      "
                << "Exch. Ts.: " << exchangeTimestamp << "      "
                << "Strat. Ts.: " << strategyTimepoint
                << std::endl;

      // throughputMonitorStrategyComponent.operationCompleted();
      // "symbol=XBTUSD&side=Buy&orderQty=1&price=50000&ordType=Limit"


      if (firstDirectionReturnsAfterFees > 1.0 && (duration_cast<milliseconds>(strategyTimestamp - exchangeTimePoint).count() > 1000)) {
        std::cout << "PROFIT POSSIBLE for XBT->USDT->ETH->XBT" << std::endl;

        //std::cout << "firstLegPrice: " << graph.getExchangeRateBetween(0, 1) << std::endl;
        //double usdConvertedAmount = 0.001 * graph.getExchangeRateBetween(0, 1) * 0.99925;
        //std::cout << "converted usd amount: " << usdConvertedAmount << std::endl;
        std::string firstLeg = std::string("symbol=XBTUSDT&side=Sell&orderQty=1000") + "&ordType=Market" + strategyTimepoint;
        while (!strategyToOrderManagerQueue.push(firstLeg));

        //double ethConvertedAmount = usdConvertedAmount * graph.getExchangeRateBetween(1, 2) * 0.99925;
        //std::cout << "converted eth amount: " << ethConvertedAmount << std::endl;
        std::string secondLeg = std::string("symbol=ETHUSDT&side=Buy&orderQty=1000") + "&ordType=Market" + strategyTimepoint;
        while (!strategyToOrderManagerQueue.push(secondLeg));

        //double xbtConvertedAmount = ethConvertedAmount * graph.getExchangeRateBetween(2, 0) * 0.99925;
        //std::cout << "converted xbt amount: " << xbtConvertedAmount << std::endl;
        std::string thirdLeg = std::string("symbol=XBTETH&side=Buy&orderQty=1") + "&ordType=Market" + strategyTimepoint;
        while (!strategyToOrderManagerQueue.push(thirdLeg));

        break;
      }

      if (secondDirectionReturnsAfterFees > 1.0 && (duration_cast<milliseconds>(strategyTimestamp - exchangeTimePoint).count() > 1000)) {
        std::cout << "PROFIT POSSIBLE for XBT->ETH->USDT->XBT" << std::endl;

        std::string firstLeg = std::string("symbol=XBTETH&side=Sell&orderQty=1") + "&ordType=Market" + strategyTimepoint;
        while (!strategyToOrderManagerQueue.push(firstLeg));

        std::string secondLeg = std::string("symbol=ETHUSDT&side=Sell&orderQty=1") + "&ordType=Market" + strategyTimepoint;
        while (!strategyToOrderManagerQueue.push(secondLeg));

        std::string thirdLeg = std::string("symbol=XBTUSDT&side=Buy&orderQty=1") + "&ordType=Market" + strategyTimepoint;
        while (!strategyToOrderManagerQueue.push(thirdLeg));

        break;
      }
    }
}
