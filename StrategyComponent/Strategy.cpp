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

typedef adjacency_list<vecS, vecS, directedS, no_property, property<edge_weight_t, double>> Graph;
typedef graph_traits<Graph>::vertex_descriptor Vertex;
typedef graph_traits<Graph>::edge_descriptor Edge;

std::vector<std::string> currencies;    
std::unordered_map<string, int> symbolToIndex;

vector<vector<double>> createAdjMatrix() {
    const std::unordered_map<string, vector<string>> pairsDict = {
        {"ETH", {"BTC", "USD"}},
        {"BTC", {"USD"}},
    };

    for (const auto& [key, vals] : pairsDict) {
        currencies.push_back(key);
        currencies.insert(currencies.end(), vals.begin(), vals.end());
    }
    sort(currencies.begin(), currencies.end());
    currencies.erase(unique(currencies.begin(), currencies.end()), currencies.end());

    for (size_t i = 0; i < currencies.size(); ++i) {
        symbolToIndex[currencies[i]] = i;
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

// Function to calculate arbitrage from a cycle
double calculateTriangularArbitrage(const vector<int>& cycle, const vector<vector<double>>& adjMatrix) {
    double arb = 1;
    for (size_t i = 0; i < cycle.size() - 1; ++i) {
        int p1 = cycle[i];
        int p2 = cycle[i + 1];
        arb *= adjMatrix[p1][p2];
    }

    return arb - 1;
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

    // auto end2 = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double, std::milli> duration2 = end2 - starttime2;
    // std::cout << "Time taken by relaxation(): " << duration2.count() << " milliseconds" << std::endl;

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
    // auto end = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double, std::milli> duration = end - starttime;
    // std::cout << "Time taken by for(): " << duration.count() << " milliseconds" << std::endl;

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
    // std::pair<Edge, bool> edge_pair = edge(u, v, g);
    // put(boost::edge_weight_t(), g, edge_pair.first, new_weight);

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
                weightmap[e] = adjMatrix[u][v];
            }
        }
    }

    while (true) {
      OrderBook orderBook;
      while (!builderToStrategyQueue.pop(orderBook));
      std::pair<double, double> bestBuyAndSellPrice = orderBook.getBestBuyAndSellPrice();
      double bestBuyPrice = bestBuyAndSellPrice.first;
      double bestSellPrice = bestBuyAndSellPrice.second;
      std::string symbol = orderBook.getSymbol();
      std :: cout << bestBuyPrice << " " << bestSellPrice << " " << symbol << std::endl;

      std::size_t baseCurrencyEndPos = symbol.find('/');
      int baseCurrencyGraphIndex = symbolToIndex[symbol.substr(0, baseCurrencyEndPos)];
      int quoteCurrencyGraphIndex = symbolToIndex[symbol.substr(baseCurrencyEndPos + 1, symbol.size())];

      adjMatrix[baseCurrencyGraphIndex][quoteCurrencyGraphIndex] = bestBuyPrice;
      adjMatrix[quoteCurrencyGraphIndex][baseCurrencyGraphIndex] = 1.0 / bestSellPrice;
      std::cout << symbol << " " << symbol.substr(0, baseCurrencyEndPos) << " " << symbol.substr(baseCurrencyEndPos + 1, symbol.size()) << baseCurrencyGraphIndex << " " << quoteCurrencyGraphIndex << std::endl;
      changeEdgeWeight(g, weightmap, baseCurrencyGraphIndex, quoteCurrencyGraphIndex, -log(bestBuyPrice));
      changeEdgeWeight(g, weightmap, quoteCurrencyGraphIndex, baseCurrencyGraphIndex, -log(1.0 / bestSellPrice));
      
    //   printAdjMatrix(adjMatrix);
    //   printEdgeWeights(g, weightmap);

      vector<vector<int>> cycles = findNegativeCycles(adjMatrix, g, weightmap, 0);
      
      for (const auto& cycle : cycles) {
          std::cout << "CYCLE FOUND" << std::endl;
          double arb = calculateTriangularArbitrage(cycle, adjMatrix);
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
