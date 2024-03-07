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
    symbolToGraphIndex["USD"] = 1;
    symbolToGraphIndex["ETH"] = 2;

    system_clock::duration startingTimestamp = system_clock::duration::zero();
    
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
      auto exchangeTimestamp = orderBook.getExchangeTimestamp();
      std::cout << "XBT->USD->ETH->XBT: " << firstDirectionReturnsAfterFees << "      " 
                << "XBT->ETH->USD->XBT: " << secondDirectionReturnsAfterFees << "      " 
                << "Latency (ms): " << duration_cast<milliseconds>(high_resolution_clock::now() - exchangeTimestamp).count() << "      " 
                << "Timestamp: " << duration_cast<milliseconds>(exchangeTimestamp.time_since_epoch()).count()
                << std::endl;
      if (startingTimestamp == system_clock::duration::zero()) {
        startingTimestamp = exchangeTimestamp.time_since_epoch();
      }
      // throughputMonitorStrategyComponent.operationCompleted();
      // "symbol=XBTUSD&side=Buy&orderQty=1&price=50000&ordType=Limit"

      if (firstDirectionReturnsAfterFees > 1.0 && (duration_cast<milliseconds>(exchangeTimestamp.time_since_epoch() - startingTimestamp).count() > 1000)) {
        std::cout << "PROFIT POSSIBLE for XBT->USD->ETH->XBT" << std::endl;

        std::string firstLeg = std::string("symbol=XBTUSD&side=Sell&orderQty=100") + "&price=" + std::to_string(graph.getExchangeRateBetween(0, 1)) + "&ordType=Limit";
        while (!strategyToOrderManagerQueue.push(firstLeg));

        std::string secondLeg = std::string("symbol=ETHUSD&side=Buy&orderQty=1") + "&price=" + std::to_string(graph.getExchangeRateBetween(1, 2)) + "&ordType=Limit";
        while (!strategyToOrderManagerQueue.push(secondLeg));

        std::string thirdLeg = std::string("symbol=ETHXBT&side=Sell&orderQty=100") + "&price=" + std::to_string(graph.getExchangeRateBetween(2, 0)) + "&ordType=Limit";
        while (!strategyToOrderManagerQueue.push(thirdLeg));

        break;
      }

      if (secondDirectionReturnsAfterFees > 1.0 && (duration_cast<milliseconds>(exchangeTimestamp.time_since_epoch() - startingTimestamp).count() > 1000)) {
        std::cout << "PROFIT POSSIBLE for XBT->ETH->USD->XBT" << std::endl;

        std::string firstLeg = std::string("symbol=ETHXBT&side=Buy&orderQty=100") + "&price=" + std::to_string(graph.getExchangeRateBetween(0, 2)) + "&ordType=Limit";
        while (!strategyToOrderManagerQueue.push(firstLeg));

        std::string secondLeg = std::string("symbol=ETHUSD&side=Sell&orderQty=1") + "&price=" + std::to_string(graph.getExchangeRateBetween(2, 1)) + "&ordType=Limit";
        while (!strategyToOrderManagerQueue.push(secondLeg));

        std::string thirdLeg = std::string("symbol=XBTUSD&side=Buy&orderQty=100") + "&price=" + std::to_string(graph.getExchangeRateBetween(1, 0)) + "&ordType=Limit";
        while (!strategyToOrderManagerQueue.push(thirdLeg));

        break;
      }
    }
}
