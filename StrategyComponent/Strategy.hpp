// Strategy.hpp
#ifndef STRATEGY_HPP
#define STRATEGY_HPP

#include <chrono> 
#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <chrono> 
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <tuple>

#include "../OrderBook/OrderBook.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "../Utils/ThroughputMonitor/ThroughputMonitor.hpp"
#include "Strategy.hpp"

using namespace std::chrono;
using namespace std;

void strategy(SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<StrategyComponentToOrderManagerQueueEntry>& strategyToOrderManagerQueue);

#endif // STRATEGY_HPP
