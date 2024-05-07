#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <chrono> 
#include "../BookBuilder/OrderBook/OrderBook.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "../BookBuilder/ThroughputMonitor/ThroughputMonitor.hpp"

class Graph {
private:
    int V;
    std::vector<std::vector<double>> adjList;

public:
    Graph(int vertices);

    void addEdge(int u, int v, double weight);
    std::pair<double, double> findTriangularArbitrage();
    double getExchangeRateBetween(int u, int v);
};

void strategy(SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<std::string>& strategyToOrderManagerQueue);

#endif // GRAPH_HPP
