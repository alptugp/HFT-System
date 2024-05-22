// shared.h
#ifndef STRATEGY_HPP
#define STRATEGY_HPP

#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <chrono> 
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/iteration_macros.hpp>

#include "../BookBuilder/OrderBook/OrderBook.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "../BookBuilder/ThroughputMonitor/ThroughputMonitor.hpp"

using namespace std::chrono;
using namespace std;
using namespace boost;

typedef adjacency_list<vecS, vecS, directedS, no_property, property<edge_weight_t, double>> Graph;
typedef graph_traits<Graph>::vertex_descriptor Vertex;
typedef graph_traits<Graph>::edge_descriptor Edge;

vector<vector<double>> createAdjMatrix();
vector<vector<int>> findNegativeCycles(const vector<vector<double>>& adjMatrix, const Graph& g, const property_map<Graph, edge_weight_t>::type& weightmap, int start);
double calculateTriangularArbitrage(const vector<int>& cycle, const vector<vector<double>>& adjMatrix);
void strategy(SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<std::string>& strategyToOrderManagerQueue);

#endif // STRATEGY_HPP
