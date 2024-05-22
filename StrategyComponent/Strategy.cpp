#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <chrono> 
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/iteration_macros.hpp>

#include "Strategy.hpp"
#include "../BookBuilder/OrderBook/OrderBook.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "../BookBuilder/ThroughputMonitor/ThroughputMonitor.hpp"

#define CPU_CORE_INDEX_FOR_STRATEGY_THREAD 2
#define AFTER_FEE_RATE 0.99925
using namespace std::chrono;
using namespace std;
using namespace boost;

#ifdef USE_KRAKEN_EXCHANGE
typedef adjacency_list<vecS, vecS, directedS, no_property, property<edge_weight_t, double>> Graph;
typedef graph_traits<Graph>::vertex_descriptor Vertex;
typedef graph_traits<Graph>::edge_descriptor Edge;

std::vector<std::string> currencies;    
std::unordered_map<string, int> symbolToIndex;

const std::unordered_map<string, vector<string>> pairsDict = {
        {"ETH", {"BTC", "USD"}},
        {"BTC", {"USD"}},
    };

vector<vector<double>> createAdjMatrix() {
    for (const auto& [key, vals] : pairsDict) {
        currencies.push_back(key);
        currencies.insert(currencies.end(), vals.begin(), vals.end());
    }
    sort(currencies.begin(), currencies.end());
    currencies.erase(unique(currencies.begin(), currencies.end()), currencies.end());

    cout << "CURRENCIES: " << endl;
    for (size_t i = 0; i < currencies.size(); ++i) {
        symbolToIndex[currencies[i]] = i;
        cout << currencies[i] << endl;
    }
    

    int n = currencies.size();
    vector<vector<double>> adjMatrix(n, vector<double>(n, 0.0));

    for (const auto& [p1, p2s] : pairsDict) {
        for (const auto& p2 : p2s) {
            adjMatrix[symbolToIndex[p1]][symbolToIndex[p2]] = 1;
            adjMatrix[symbolToIndex[p2]][symbolToIndex[p1]] = 1;
        }
    }

    return adjMatrix;
}

vector<vector<int>> findNegativeCycles(const vector<vector<double>>& adjMatrix, const Graph& g, const property_map<Graph, edge_weight_t>::type& weightmap, int start) {
    int V = adjMatrix.size();

    // Distance and predecessor maps
    vector<double> distances(V, numeric_limits<double>::infinity());
    vector<int> predecessors(V, -1);
    distances[start] = 0;

    auto starttime2 = std::chrono::high_resolution_clock::now();
    
    std::cout << "V: " << V << std::endl;
    for (int i = 0; i < V - 1; ++i) {
        for (int u = 0; u < V; ++u) {
            for (int v = 0; v < V; ++v) {
                double weight = adjMatrix[u][v];
                if (weight != 0) {
                    weight = -log(weight);
                    if (distances[u] + weight < distances[v]) {
                        distances[v] = distances[u] + weight;
                        predecessors[v] = u;
                    }
                }
            }
        }
    }

    // Cycle detection
    vector<vector<int>> allCycles;
    vector<bool> seen(V, false);

    auto starttime = std::chrono::high_resolution_clock::now();
    for (int u = 0; u < V; ++u) {
        auto [out_it, out_end] = out_edges(u, g);
        for (auto ei = out_it; ei != out_end; ++ei) {
            int v = target(*ei, g);
            double weight = weightmap[*ei];
            if (seen[v]) 
                continue;
            if (distances[u] + weight < distances[v]) {
                vector<int> cycle;
                int x = v;
                while (true) {
                    seen[x] = true;
                    cycle.push_back(x);
                    x = predecessors[x];
                    if (x == v || find(cycle.begin(), cycle.end(), x) != cycle.end()) break;
                }
                int idx = find(cycle.begin(), cycle.end(), x) - cycle.begin();
                cycle.push_back(x);
                allCycles.push_back(vector<int>(cycle.begin() + idx, cycle.end()));
                reverse(allCycles.back().begin(), allCycles.back().end());
            }
        }
    }

    return allCycles;
}

void printEdgeWeights(const Graph& g, const property_map<Graph, edge_weight_t>::type& weightmap) {
    cout << "Edge Weights:" << endl;
    BGL_FORALL_EDGES(e, g, Graph) {
        cout << source(e, g) << " -> " << target(e, g) << " : " << weightmap[e] << endl;
    }
    cout << endl;
}

void printAdjMatrix(const vector<vector<double>>& adjMatrix) {
    cout << "Adjacency Matrix:" << endl;
    for (const auto& row : adjMatrix) {
        for (double val : row) {
            cout << val << " ";
        }
        cout << endl;
    }
    cout << endl;
}

// Function to change the weight of an edge
void changeEdgeWeight(Graph& g, property_map<Graph, edge_weight_t>::type& weightmap, int u, int v, double new_weight) {
    std::pair<Edge, bool> edge_pair = edge(u, v, g);
    if (edge_pair.second) {
        put(weightmap, edge_pair.first, new_weight);
    } else {
        std::cerr << "Error: Edge (" << u << ", " << v << ") not found in the graph." << std::endl;
    }
}

void strategy(SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
    int numCores = std::thread::hardware_concurrency();
    
    if (numCores == 0) {
        std::cerr << "Error: Unable to determine the number of CPU cores." << std::endl;
        return;
    } else if (numCores < CPU_CORE_INDEX_FOR_STRATEGY_THREAD) {
        std::cerr << "Error: Not enough cores to run the system." << std::endl;
        return;
    }

    int cpuCoreNumberForStrategyThread = CPU_CORE_INDEX_FOR_STRATEGY_THREAD;
    setThreadAffinity(pthread_self(), cpuCoreNumberForStrategyThread);

    // Set the current thread's real-time priority to highest value
    // struct sched_param schedParams;
    // schedParams.sched_priority = sched_get_priority_max(SCHED_FIFO);
    // pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParams);

    system_clock::time_point startingTimestamp = time_point<std::chrono::system_clock>::min();

    ThroughputMonitor throughputMonitorStrategyComponent("Strategy Component Throughput Monitor", std::chrono::high_resolution_clock::now());
    
    vector<vector<double>> adjMatrix = createAdjMatrix();

    int V = adjMatrix.size();
    Graph g(V);
    property_map<Graph, edge_weight_t>::type weightmap = get(edge_weight, g);

    for (int u = 0; u < V; ++u) {
        for (int v = 0; v < V; ++v) {
            if (adjMatrix[u][v] != 0) {
                std::cout << "NOT ZERO" << std::endl;
                Edge e; bool inserted;
                tie(e, inserted) = add_edge(u, v, g);
            }
        }
    }

    while (true) {
      OrderBook orderBook;
      while (!builderToStrategyQueue.pop(orderBook));
      std::pair<double, double> bestBuyAndSellPrice = orderBook.getBestBuyAndSellPrice();
      double bestBuyPrice = bestBuyAndSellPrice.first;
      double bestSellPrice = bestBuyAndSellPrice.second;
      std::string currencyPair = orderBook.getSymbol();
      std :: cout << bestBuyPrice << " " << bestSellPrice << " " << currencyPair << std::endl;

      std::size_t baseCurrencyEndPos = currencyPair.find('/');
      int baseCurrencyGraphIndex = symbolToIndex[currencyPair.substr(0, baseCurrencyEndPos)];
      int quoteCurrencyGraphIndex = symbolToIndex[currencyPair.substr(baseCurrencyEndPos + 1, currencyPair.size())];

      adjMatrix[baseCurrencyGraphIndex][quoteCurrencyGraphIndex] = bestBuyPrice;
      adjMatrix[quoteCurrencyGraphIndex][baseCurrencyGraphIndex] = 1.0 / bestSellPrice;
      changeEdgeWeight(g, weightmap, baseCurrencyGraphIndex, quoteCurrencyGraphIndex, -log(bestBuyPrice));
      changeEdgeWeight(g, weightmap, quoteCurrencyGraphIndex, baseCurrencyGraphIndex, -log(1.0 / bestSellPrice));
      
      printAdjMatrix(adjMatrix);
      printEdgeWeights(g, weightmap);

      vector<vector<int>> cycles = findNegativeCycles(adjMatrix, g, weightmap, 1);
      
      system_clock::time_point strategyComponentArbitrageDetectionTimestamp = high_resolution_clock::now();
      std::string strategyComponentArbitrageDetectionTimepoint = std::to_string(duration_cast<milliseconds>(strategyComponentArbitrageDetectionTimestamp.time_since_epoch()).count());
    
      auto exchangeUpdateTxTimestamp = time_point<high_resolution_clock>(milliseconds(orderBook.getUpdateExchangeTimestamp()));
      std::string exchangeUpdateTxTimepoint = std::to_string(duration_cast<milliseconds>(exchangeUpdateTxTimestamp.time_since_epoch()).count());  
      if (exchangeUpdateTxTimepoint == "0")
        continue;
      
      system_clock::time_point bookBuilderUpdateRxTimestamp = orderBook.getUpdateReceiveTimestamp();
      std::string bookBuilderUpdateRxTimepoint = std::to_string(duration_cast<milliseconds>(bookBuilderUpdateRxTimestamp.time_since_epoch()).count());
      std::cout << "bookBuilderUpdateRxTimepoint: " << bookBuilderUpdateRxTimepoint << std::endl;

      std::cout << "Exchange-Receival (ms): " << duration_cast<milliseconds>(bookBuilderUpdateRxTimestamp - exchangeUpdateTxTimestamp).count() << "      "
                << "Receival-Detection (ms): " << duration_cast<milliseconds>(strategyComponentArbitrageDetectionTimestamp - bookBuilderUpdateRxTimestamp).count() << "      "
      << std::endl;

      for (const auto& cycle : cycles) {
          std::cout << "CYCLE FOUND" << std::endl;

          // Calculate triangular arbitrage
          double arb = 1;
          for (size_t i = 0; i < cycle.size() - 1; ++i) {
              int p1 = cycle[i];
              int p2 = cycle[i + 1];

              std::string orderQty = "1";  

              std::string orderSide;
              std::string orderPair;
              auto it = pairsDict.find(currencies[p1]);
              if (it != pairsDict.end() && std::find(it->second.begin(), it->second.end(), currencies[p2]) != it->second.end()) {
                orderSide = "Sell";
                orderPair = currencies[p1] + "/" + currencies[p2];
              } else {
                orderSide = "Buy";
                orderPair = currencies[p2] + "/" + currencies[p1];
              }

              std::string order = std::string("symbol=") + orderPair + "&side=" + orderSide + "&orderQty=" + orderQty + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
              std::cout << "ORDER: " << order << std::endl;
              while (!strategyToOrderManagerQueue.push(order));

              arb *= adjMatrix[p1][p2];
          }

          arb = arb - 1;
            
          cout << "Path: ";
          for (int node : cycle) {
                cout << node << " ";
          }
          cout << endl;
          cout << arb * 100 << "%" << endl << endl;
      }

    //   if (cycles.size() > 0)
    //     break;
    }
}
#elif defined(USE_BITMEX_EXCHANGE) || defined(USE_MOCK_EXCHANGE)
    int V;
    std::vector<std::vector<double>> adjList;
    const std::vector<std::string> currencies = {"XBT", "USDT", "ETH"};

    std::pair<double, double> findTriangularArbitrage() {
        int startVertex = 0;
        std::vector<double> cycleWeightMultiplications;

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
            cycleWeightMultiplications.emplace_back(weightMultiply);
        }

        double weightMultiplyFirstDirection = cycleWeightMultiplications[0];
        double weightMultiplySecondDirection = cycleWeightMultiplications[1];
        return std::make_pair(weightMultiplyFirstDirection, weightMultiplySecondDirection);
    }

    void addEdge(int u, int v, double weight) {
        adjList[u][v] = weight;
    }

    double getExchangeRateBetween(int u, int v) {
        return adjList[u][v];
    }

    void strategy(SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
        int numCores = std::thread::hardware_concurrency();
        
        if (numCores == 0) {
            std::cerr << "Error: Unable to determine the number of CPU cores." << std::endl;
            return;
        } else if (numCores < CPU_CORE_INDEX_FOR_STRATEGY_THREAD) {
            std::cerr << "Error: Not enough cores to run the system." << std::endl;
            return;
        }

        int cpuCoreNumberForStrategyThread = CPU_CORE_INDEX_FOR_STRATEGY_THREAD;
        setThreadAffinity(pthread_self(), cpuCoreNumberForStrategyThread);

        // Set the current thread's real-time priority to highest value
        // struct sched_param schedParams;
        // schedParams.sched_priority = sched_get_priority_max(SCHED_FIFO);
        // pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParams);

        V = currencies.size();
        adjList.resize(V, std::vector<double>(V, 1.0));

        std::unordered_map<std::string, int> symbolToGraphIndex;   
        for (int graphIndex = 0; graphIndex < currencies.size(); graphIndex++)
            symbolToGraphIndex[currencies[graphIndex]] = graphIndex;

        system_clock::time_point startingTimestamp = time_point<std::chrono::system_clock>::min();

        ThroughputMonitor throughputMonitorStrategyComponent("Strategy Component Throughput Monitor", std::chrono::high_resolution_clock::now());
        while (true) {
            OrderBook orderBook;
            while (!builderToStrategyQueue.pop(orderBook));
            std::pair<double, double> bestBuyAndSellPrice = orderBook.getBestBuyAndSellPrice();
            double bestBuyPrice = bestBuyAndSellPrice.first;
            double bestSellPrice = bestBuyAndSellPrice.second;
            std::string symbol = orderBook.getSymbol();
            const std::size_t baseCurrencyEndPos = 3;  
            int baseCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(0, baseCurrencyEndPos)];
            int quoteCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(baseCurrencyEndPos, symbol.size())];  
            addEdge(baseCurrencyGraphIndex, quoteCurrencyGraphIndex, bestBuyPrice);
            addEdge(quoteCurrencyGraphIndex, baseCurrencyGraphIndex, 1.0 / bestSellPrice);
            std::pair<double, double> returns = findTriangularArbitrage();
            double firstDirectionReturnsAfterFees = returns.first * std::pow(0.99925, 3);
            double secondDirectionReturnsAfterFees = returns.second * std::pow(0.99925, 3);
            // std::cout << symbol << " - Best Sell: " << bestSellPrice << " Best Buy: " << bestBuyPrice << std::endl;

            system_clock::time_point strategyTimestamp = high_resolution_clock::now();
            std::string strategyTimepoint = std::to_string(duration_cast<milliseconds>(strategyTimestamp.time_since_epoch()).count());
            long updateExchangeTimestamp = orderBook.getUpdateExchangeTimestamp();
            auto updateExchangeTimepoint = std::chrono::time_point<std::chrono::high_resolution_clock>(
                        std::chrono::milliseconds(updateExchangeTimestamp));
            system_clock::time_point updateReceiveTimepoint = orderBook.getUpdateReceiveTimestamp();

            std::cout << "XBT->USDT->ETH->XBT: " << firstDirectionReturnsAfterFees << "      "
                        << "XBT->ETH->USDT->XBT: " << secondDirectionReturnsAfterFees << "      "
                        << "Exchange-Receival (ms): " << duration_cast<milliseconds>(updateReceiveTimepoint - updateExchangeTimepoint).count() << "      "
                        << "Receival-Detection (ms): " << duration_cast<milliseconds>(strategyTimestamp - updateReceiveTimepoint).count() << "      "

                        // << "Exch. Ts.: " << updateExchangeTimestamp << "      "
                        // << "Rec. Ts.: " << std::to_string(duration_cast<milliseconds>(updateReceiveTimepoint.time_since_epoch()).count()) << "      "
                        // << "Strat. Ts.: " << strategyTimepoint
                        << std::endl;
            if (startingTimestamp == time_point<std::chrono::system_clock>::min()) {
                    startingTimestamp = updateExchangeTimepoint;
            }
            // throughputMonitorStrategyComponent.operationCompleted();
            // "symbol=XBTUSD&side=Buy&orderQty=1&price=50000&ordType=Limit"

            std::string updateExchangeTimepointStr = std::to_string(duration_cast<milliseconds>(updateExchangeTimepoint.time_since_epoch()).count());
            std::string updateReceiveTimepointStr = std::to_string(duration_cast<milliseconds>(updateReceiveTimepoint.time_since_epoch()).count());
            if (/*firstDirectionReturnsAfterFees > 1.0 &&*/ (duration_cast<milliseconds>(strategyTimestamp - startingTimestamp).count() > 1000)) {
                /*std::cout << "PROFIT POSSIBLE for XBT->USDT->ETH->XBT" << std::endl;*/

                //std::cout << "firstLegPrice: " << graph.getExchangeRateBetween(0, 1) << std::endl;
                //double usdConvertedAmount = 0.001 * graph.getExchangeRateBetween(0, 1) * 0.99925;
                //std::cout << "converted usd amount: " << usdConvertedAmount << std::endl;
                std::string firstLeg = std::string("symbol=XBTUSDT&side=Sell&orderQty=1000") + "&ordType=Market" + updateExchangeTimepointStr + updateReceiveTimepointStr + strategyTimepoint;
                while (!strategyToOrderManagerQueue.push(firstLeg));

                //double ethConvertedAmount = usdConvertedAmount * graph.getExchangeRateBetween(1, 2) * 0.99925;
                //std::cout << "converted eth amount: " << ethConvertedAmount << std::endl;
                std::string secondLeg = std::string("symbol=ETHUSDT&side=Buy&orderQty=1000") + "&ordType=Market" + updateExchangeTimepointStr + updateReceiveTimepointStr + strategyTimepoint;
                while (!strategyToOrderManagerQueue.push(secondLeg));

                //double xbtConvertedAmount = ethConvertedAmount * graph.getExchangeRateBetween(2, 0) * 0.99925;
                //std::cout << "converted xbt amount: " << xbtConvertedAmount << std::endl;
                std::string thirdLeg = std::string("symbol=XBTETH&side=Buy&orderQty=1") + "&ordType=Market" + updateExchangeTimepointStr + updateReceiveTimepointStr + strategyTimepoint;
                while (!strategyToOrderManagerQueue.push(thirdLeg));

                startingTimestamp = time_point<high_resolution_clock>(high_resolution_clock::now());
            }

            if (/*secondDirectionReturnsAfterFees > 1.0 &&*/ (duration_cast<milliseconds>(strategyTimestamp - startingTimestamp).count() > 1000)) {
                /*std::cout << "PROFIT POSSIBLE for XBT->ETH->USDT->XBT" << std::endl;*/

                std::string firstLeg = std::string("symbol=XBTETH&side=Sell&orderQty=1") + "&ordType=Market" + updateExchangeTimepointStr + updateReceiveTimepointStr + strategyTimepoint;
                while (!strategyToOrderManagerQueue.push(firstLeg));

                std::string secondLeg = std::string("symbol=ETHUSDT&side=Sell&orderQty=1000") + "&ordType=Market" + updateExchangeTimepointStr + updateReceiveTimepointStr + strategyTimepoint;
                while (!strategyToOrderManagerQueue.push(secondLeg));

                std::string thirdLeg = std::string("symbol=XBTUSDT&side=Buy&orderQty=1000") + "&ordType=Market" + updateExchangeTimepointStr + updateReceiveTimepointStr + strategyTimepoint;
                while (!strategyToOrderManagerQueue.push(thirdLeg));

                startingTimestamp = time_point<high_resolution_clock>(high_resolution_clock::now());
            }
        }
    }
#endif