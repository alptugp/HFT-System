#include "Graph.hpp"

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

void strategy(int cpu, SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
    pinThread(cpu);
    Graph graph(3);

    std::unordered_map<std::string, int> symbolToGraphIndex;   
    symbolToGraphIndex["USD"] = 0;
    symbolToGraphIndex["XBT"] = 1;
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
      auto exchangeTimestamp = orderBook.getExchangeTimestamp();
      std::cout << "USD->XBT->ETH->USD: " << firstDirectionReturnsAfterFees << "      " 
                << "USD->ETH->XBT->USD: " << secondDirectionReturnsAfterFees << "      " 
                << "Latency (ms): " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - exchangeTimestamp).count() << "      " 
                << "Timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(exchangeTimestamp.time_since_epoch()).count()
                << std::endl;
      // throughputMonitorStrategyComponent.operationCompleted();
      // "symbol=XBTUSD&side=Buy&orderQty=1&price=50000&ordType=Limit"

      if (firstDirectionReturnsAfterFees > 1.0) {
        std::cout << "PROFIT POSSIBLE for USD->XBT->ETH->USD" << std::endl;
        while (!strategyToOrderManagerQueue.push(std::string("symbol=XBTUSD") +  "&side=Buy" + "&orderQty=0" + "&price=0" + "&ordType=Limit"));
        while (!strategyToOrderManagerQueue.push(std::string("symbol=ETHXBT") +  "&side=Buy" + "&orderQty=0" + "&price=0" + "&ordType=Limit"));
        while (!strategyToOrderManagerQueue.push(std::string("symbol=ETHUSD") +  "&side=Sell" + "&orderQty=0" + "&price=0" + "&ordType=Limit"));
      }

      if (secondDirectionReturnsAfterFees > 1.0) {
        std::cout << "PROFIT POSSIBLE for USD->XBT->ETH->USD" << std::endl;
        while (!strategyToOrderManagerQueue.push(std::string("symbol=ETHUSD") +  "&side=Buy" + "&orderQty=0" + "&price=0" + "&ordType=Limit"));
        while (!strategyToOrderManagerQueue.push(std::string("symbol=ETHXBT") +  "&side=Sell" + "&orderQty=0" + "&price=0" + "&ordType=Limit"));
        while (!strategyToOrderManagerQueue.push(std::string("symbol=XBTUSD") +  "&side=Sell" + "&orderQty=0" + "&price=0" + "&ordType=Limit"));
      }
      
    }
}
