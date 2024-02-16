#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cmath>
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
};

void strategy(int cpu, SPSCQueue<OrderBook>& queue);

#endif // GRAPH_HPP
