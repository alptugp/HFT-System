#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <chrono> 
#include <iomanip>
#include <fstream>
#include <algorithm>

#include "Strategy.hpp"
#include "../BookBuilder/OrderBook/OrderBook.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "../BookBuilder/ThroughputMonitor/ThroughputMonitor.hpp"

#define CPU_CORE_INDEX_FOR_STRATEGY_THREAD 3
#define AFTER_FEE_RATE 0.99925
using namespace std::chrono;
using namespace std;

struct ExchangeRatePriceAndSize {
    double bestPrice;
    double bestPriceSize;
};

static std::ofstream strategyComponentDataFile;

#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
static size_t V;
static std::vector<std::vector<std::pair<int, double>>> g;
static std::vector<std::vector<ExchangeRatePriceAndSize>> exchangeRatesMatrix;
static std::vector<std::string> currencies;    
static std::unordered_map<string, int> symbolToIndex;
// 115:
static const std::unordered_map<string, vector<string>> pairsDict = {
    {"KSM", {"EUR", "BTC", "DOT", "GBP", "ETH", "USD"}},
    {"GBP", {"USD"}},
    {"BTC", {"CAD", "EUR", "AUD", "JPY", "GBP", "CHF", "USDT", "USD", "USDC"}},
    {"LTC", {"EUR", "BTC", "AUD", "JPY", "GBP", "ETH", "USDT", "USD"}},
    {"SOL", {"EUR", "BTC", "GBP", "ETH", "USDT", "USD"}},
    {"DOT", {"EUR", "BTC", "JPY", "GBP", "ETH", "USDT", "USD"}},
    {"ETH", {"CAD", "EUR", "BTC", "AUD", "JPY", "GBP", "CHF", "USDT", "USD", "USDC"}},
    {"LINK", {"EUR", "BTC", "AUD", "JPY", "GBP", "ETH", "USDT", "USD"}},
    {"USDC", {"CAD", "EUR", "AUD", "GBP", "CHF", "USDT", "USD"}},
    {"ADA", {"EUR", "BTC", "AUD", "GBP", "ETH", "USDT", "USD"}},
    {"ATOM", {"EUR", "BTC", "GBP", "ETH", "USDT", "USD"}},
    {"USDT", {"EUR", "AUD", "JPY", "GBP", "CHF", "USD", "CAD"}},
    {"AUD", {"JPY", "USD"}},
    {"XRP", {"CAD", "EUR", "BTC", "AUD", "GBP", "ETH", "USDT", "USD"}},
    {"EUR", {"CAD", "AUD", "JPY", "GBP", "CHF", "USD"}},
    {"BCH", {"EUR", "BTC", "AUD", "JPY", "GBP", "ETH", "USDT", "USD"}},
    {"USD", {"CHF", "JPY", "CAD"}},
    {"ALGO", {"EUR", "BTC", "GBP", "ETH", "USDT", "USD"}}
};

//85:
// const std::unordered_map<string, vector<string>> pairsDict  = {
//     {"BCH", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "USDT", "JPY"}},
//     {"BTC", {"USD", "EUR", "USDC", "AUD", "GBP", "CAD", "USDT", "JPY"}},
//     {"USD", {"CAD", "JPY"}},
//     {"XRP", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "CAD", "USDT"}},
//     {"EUR", {"USD", "AUD", "GBP", "CAD", "JPY"}},
//     {"LTC", {"USD", "EUR", "BTC", "AUD", "GBP", "ETH", "USDT", "JPY"}},
//     {"ETH", {"USD", "EUR", "BTC", "USDC", "AUD", "GBP", "CAD", "USDT", "JPY"}},
//     {"LINK", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "USDT", "JPY"}},
//     {"ADA", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "USDT"}},
//     {"USDC", {"USD", "EUR", "AUD", "GBP", "CAD", "USDT"}},
//     {"GBP", {"USD"}},
//     {"DOT", {"USD", "BTC", "EUR", "GBP", "ETH", "USDT", "JPY"}},
//     {"USDT", {"USD", "EUR", "AUD", "GBP", "CAD", "JPY"}},
//     {"AUD", {"USD", "JPY"}}
// };

// 50:
// const std::unordered_map<string, vector<string>> pairsDict = {
//     {"BCH", {"JPY", "ETH", "GBP", "AUD", "BTC", "USDT", "EUR", "USD"}},
//     {"USDT", {"JPY", "GBP", "AUD", "EUR", "USD"}},
//     {"BTC", {"JPY", "GBP", "AUD", "USDT", "EUR", "USD"}},
//     {"EUR", {"GBP", "JPY", "AUD", "USD"}},
//     {"ETH", {"JPY", "EUR", "AUD", "BTC", "USDT", "GBP", "USD"}},
//     {"USD", {"JPY"}},
//     {"LINK", {"JPY", "ETH", "EUR", "AUD", "BTC", "USDT", "GBP", "USD"}},
//     {"LTC", {"JPY", "ETH", "GBP", "AUD", "BTC", "USDT", "EUR", "USD"}},
//     {"GBP", {"USD"}},
//     {"AUD", {"JPY", "USD"}}
// };

//1
// const std::unordered_map<string, vector<string>> pairsDict = {
//     {"ETH", {"USD"}},
// };


void createExchangeRatesMatrix() {
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
    exchangeRatesMatrix = std::vector<std::vector<ExchangeRatePriceAndSize>>(n, std::vector<ExchangeRatePriceAndSize>(n, {0.0, 0.0}));

    for (const auto& [p1, p2s] : pairsDict) {
        for (const auto& p2 : p2s) {
            exchangeRatesMatrix[symbolToIndex[p1]][symbolToIndex[p2]].bestPrice = 1;
            exchangeRatesMatrix[symbolToIndex[p2]][symbolToIndex[p1]].bestPrice = 1;
        }
    }
}

vector<vector<int>> findTriangularArbitrage() {
    int V = exchangeRatesMatrix.size();
    vector<double> distances(V);
    vector<int> predecessors(V, -1);
    const int startCurrency = 0;
    distances[startCurrency] = 0;
    system_clock::time_point relaxationStartTimestamp = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < V - 1; ++i) { 
        bool relaxed = false;
        for (int u = 0; u < V; ++u) {
            if (distances[u] == numeric_limits<double>::infinity()) continue;
            for (const auto& [v, weight] : g[u]) 
                if (distances[u] + weight < distances[v]) {
                    distances[v] = distances[u] + weight;
                    predecessors[v] = u;
                    relaxed = true;
                }
        }
        if (!relaxed) break;
    }       

    system_clock::time_point relaxationCompletionTimestamp = std::chrono::high_resolution_clock::now();

    // Cycle detection
    vector<vector<int>> triangularArbitrageCycles;
    vector<bool> seen(V, false);
    system_clock::time_point detectionStartTimestamp = std::chrono::high_resolution_clock::now();
    for (int u = 0; u < V; ++u) 
        for (const auto& [v, weight] : g[u]) {
            if (seen[v] || !(distances[u] < numeric_limits<double>::infinity())) continue;
            if (distances[u] + weight < distances[v]) {
                vector<int> triangularArbitrageCycle;
                int x = v;
                while (true) {
                    seen[x] = true;
                    triangularArbitrageCycle.push_back(x);
                    x = predecessors[x];
                    if (x == v || find(triangularArbitrageCycle.begin(), triangularArbitrageCycle.end(), x) != triangularArbitrageCycle.end()) break;
                }
                int idx = find(triangularArbitrageCycle.begin(), triangularArbitrageCycle.end(), x) - triangularArbitrageCycle.begin();
                if (triangularArbitrageCycle.size() - idx == 3) {
                    triangularArbitrageCycle.push_back(x);
                    triangularArbitrageCycles.push_back(vector<int>(triangularArbitrageCycle.begin() + idx, triangularArbitrageCycle.end()));
                    reverse(triangularArbitrageCycles.back().begin(), triangularArbitrageCycles.back().end());
                }
            }
        }
    
    system_clock::time_point detectionCompletionTimestamp = std::chrono::high_resolution_clock::now();

    auto relaxationStartUs = timePointToMicroseconds(relaxationStartTimestamp);
    auto relaxationCompletionUs = timePointToMicroseconds(relaxationCompletionTimestamp);
    auto detectionStartUs = timePointToMicroseconds(detectionStartTimestamp);
    auto detectionCompletionUs = timePointToMicroseconds(detectionCompletionTimestamp);

    double relaxationTime = (relaxationCompletionUs - relaxationStartUs) / 1000.0;
    double detectionTime = (detectionCompletionUs - detectionStartUs) / 1000.0;
    strategyComponentDataFile 
    << relaxationTime << ", "
    << detectionTime << ", "
    << (triangularArbitrageCycles.size() > 0 ? "YES" : "NO") 
    << std::endl;

    return triangularArbitrageCycles;
}

void printEdgeWeights() {
    cout << "Edge Weights:";
    for (int u = 0; u < V; ++u) 
        for (const auto& [v, weight] : g[u]) 
            cout << u << " -> " << v << " : " << weight << endl;
    cout << endl;
}

void printExchangeRatesMatrix() {
    cout << "Exchange Rates Matrix:" << endl;
    for (const auto& row : exchangeRatesMatrix) {
        for (ExchangeRatePriceAndSize exchangeRatePriceAndSize : row) {
            cout << exchangeRatePriceAndSize.bestPrice << " ";
        }
        cout << endl;
    }
    cout << endl;
}

void changeEdgeWeight(int baseCurrencyGraphIndex, int quoteCurrencyGraphIndex, double new_weight) {
    for (auto& edge : g[baseCurrencyGraphIndex]) {
        if (edge.first == quoteCurrencyGraphIndex) {
            edge.second = new_weight;
            return;
        }
    }
    g[baseCurrencyGraphIndex].emplace_back(quoteCurrencyGraphIndex, new_weight); 
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
    
    strategyComponentDataFile.open("strategy-component-data/new.txt", std::ios_base::out); 
    if (!strategyComponentDataFile.is_open()) {
        std::cerr << "Error: Unable to open file for " << std::endl;
        return;
    }

    system_clock::time_point startingTimestamp = time_point<std::chrono::system_clock>::min();

    // ThroughputMonitor throughputMonitorStrategyComponent("Strategy Component Throughput Monitor", std::chrono::high_resolution_clock::now());
    
    createExchangeRatesMatrix();

    V = exchangeRatesMatrix.size();
    g.resize(V);

    for (int u = 0; u < V; ++u) {
        for (int v = 0; v < V; ++v) {
            if (exchangeRatesMatrix[u][v].bestPrice != 0) {
                g[u].emplace_back(v, 0);
            }
        }
    }

    while (true) {
      OrderBook orderBook;
      while (!builderToStrategyQueue.pop(orderBook));
      auto bestBuy = orderBook.getBestBuyLimitPriceAndSize();
      auto bestSell = orderBook.getBestSellLimitPriceAndSize();  
      double bestBuyPrice = bestBuy.first;
      double bestBuyPriceSize = bestBuy.second;
      double bestSellPriceReciprocal = 1.0 / bestSell.first;
      double bestSellPriceSize = bestSell.second;

      std::string currencyPair = orderBook.getCurrencyPairSymbol();
      std::size_t baseCurrencyEndPos = currencyPair.find('/');
      int baseCurrencyGraphIndex = symbolToIndex[currencyPair.substr(0, baseCurrencyEndPos)];
      int quoteCurrencyGraphIndex = symbolToIndex[currencyPair.substr(baseCurrencyEndPos + 1, currencyPair.size())];
      std :: cout << bestBuyPrice << " " << bestSell.first << " " << currencyPair << std::endl;

      if (bestBuyPrice != exchangeRatesMatrix[baseCurrencyGraphIndex][quoteCurrencyGraphIndex].bestPrice) {
        exchangeRatesMatrix[baseCurrencyGraphIndex][quoteCurrencyGraphIndex].bestPrice = bestBuyPrice;
        changeEdgeWeight(baseCurrencyGraphIndex, quoteCurrencyGraphIndex, -log(bestBuyPrice));
      }

      if (bestSellPriceReciprocal != exchangeRatesMatrix[baseCurrencyGraphIndex][quoteCurrencyGraphIndex].bestPrice) {
        exchangeRatesMatrix[quoteCurrencyGraphIndex][baseCurrencyGraphIndex].bestPrice = bestSellPriceReciprocal;
        changeEdgeWeight(quoteCurrencyGraphIndex, baseCurrencyGraphIndex, -log(bestSellPriceReciprocal));
      }

      exchangeRatesMatrix[baseCurrencyGraphIndex][quoteCurrencyGraphIndex].bestPriceSize = bestBuyPriceSize;
      exchangeRatesMatrix[quoteCurrencyGraphIndex][baseCurrencyGraphIndex].bestPriceSize = bestSellPriceSize;
      
      // printExchangeRatesMatrix(exchangeRatesMatrix);
      // printEdgeWeights(g, weightmap);

      vector<vector<int>> cycles = findTriangularArbitrage();
      system_clock::time_point strategyComponentArbitrageDetectionTimestamp = high_resolution_clock::now();
      std::string strategyComponentArbitrageDetectionTimepoint = std::to_string(duration_cast<microseconds>(strategyComponentArbitrageDetectionTimestamp.time_since_epoch()).count());
    
      auto exchangeUpdateTxTimestamp = time_point<high_resolution_clock>(microseconds(orderBook.getMarketUpdateExchangeTxTimestamp()));
      std::string exchangeUpdateTxTimepoint = std::to_string(duration_cast<microseconds>(exchangeUpdateTxTimestamp.time_since_epoch()).count());  
      if (exchangeUpdateTxTimepoint == "0")
        continue;
      system_clock::time_point bookBuilderUpdateRxTimestamp = orderBook.getFinalUpdateTimestamp();
      std::string bookBuilderUpdateRxTimepoint = std::to_string(duration_cast<microseconds>(bookBuilderUpdateRxTimestamp.time_since_epoch()).count());


      std::cout 
                << "exchangeUpdateTxTimestamp: " << exchangeUpdateTxTimepoint
                << "bookBuilderUpdateRxTimestamp: " << bookBuilderUpdateRxTimepoint
                << "Exchange Update Occurence to Update Receival (ms): " << duration_cast<microseconds>(bookBuilderUpdateRxTimestamp - exchangeUpdateTxTimestamp).count() / 1000.0 << "      "
                << "Update Receival to Arbitrage Detection (ms): " << duration_cast<microseconds>(strategyComponentArbitrageDetectionTimestamp - bookBuilderUpdateRxTimestamp).count() / 1000.0 << "      "
      << std::endl;

      for (const auto& cycle : cycles) {
        //   std::cout << "CYCLE FOUND" << std::endl;

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
            //   std::cout << "ORDER: " << order << std::endl;
              while (!strategyToOrderManagerQueue.push(order));

              arb *= exchangeRatesMatrix[p1][p2].bestPrice;
          }

          arb = arb - 1;
            
          cout << "Currency Conversions: ";
          for (int node : cycle) {
                cout << node << " ";
          }
          cout << endl;
          cout << "% Return: " << arb * 100 << "%" << endl << endl;
          break; //FOR NOW
      }

    //   if (cycles.size() > 0)
    //     break;
    }
}
#elif defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
    int V;
    std::vector<std::vector<double>> g;
    const std::vector<std::string> currencies = {"XBT", "USDT", "ETH"};

    std::pair<double, double> findTriangularArbitrage() {
        int startVertex = 0;
        std::vector<double> cycleWeightMultiplications;

        for (int direction = 1; direction < V; direction++) {
            int currentNode = startVertex;
            int nextNode = direction;
            double weightMultiply = 1.0;
            do {
                weightMultiply *= g[currentNode][nextNode];
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

        // std::ofstream outFile("data/book-builder.txt", std::ios::app);

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
            double bestBuyPrice = orderBook.getBestBuyLimitPriceAndSize().first;
            double bestSellPrice = orderBook.getBestSellLimitPriceAndSize().first;
            std::string symbol = orderBook.getCurrencyPairSymbol();
            const std::size_t baseCurrencyEndPos = 3;  
            int baseCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(0, baseCurrencyEndPos)];
            int quoteCurrencyGraphIndex = symbolToGraphIndex[symbol.substr(baseCurrencyEndPos, symbol.size())];  
            addEdge(baseCurrencyGraphIndex, quoteCurrencyGraphIndex, bestBuyPrice);
            addEdge(quoteCurrencyGraphIndex, baseCurrencyGraphIndex, 1.0 / bestSellPrice);
            std::pair<double, double> returns = findTriangularArbitrage();
            double firstDirectionReturnsAfterFees = returns.first * std::pow(0.99925, 3);
            double secondDirectionReturnsAfterFees = returns.second * std::pow(0.99925, 3);
            // std::cout << symbol << " - Best Sell: " << bestSellPrice << " Best Buy: " << bestBuyPrice << std::endl;

            system_clock::time_point strategyComponentArbitrageDetectionTimestamp = high_resolution_clock::now();
            std::string strategyComponentArbitrageDetectionTimepoint = std::to_string(duration_cast<microseconds>(strategyComponentArbitrageDetectionTimestamp.time_since_epoch()).count());
            
            auto exchangeUpdateTxTimestamp = time_point<high_resolution_clock>(microseconds(orderBook.getMarketUpdateExchangeTxTimestamp()));
            std::string exchangeUpdateTxTimepoint = std::to_string(duration_cast<microseconds>(exchangeUpdateTxTimestamp.time_since_epoch()).count());  
            
            system_clock::time_point bookBuilderUpdateRxTimestamp = orderBook.getFinalUpdateTimestamp();
            std::string bookBuilderUpdateRxTimepoint = std::to_string(duration_cast<microseconds>(bookBuilderUpdateRxTimestamp.time_since_epoch()).count());

            std::cout << "XBT->USDT->ETH->XBT: " << firstDirectionReturnsAfterFees << "      "
                        << "XBT->ETH->USDT->XBT: " << secondDirectionReturnsAfterFees << "      "
                        << "Exchange Update Occurence to Update Receival (ms): " << duration_cast<microseconds>(bookBuilderUpdateRxTimestamp - exchangeUpdateTxTimestamp).count() / 1000.0 << "      "
                        << "Update Receival to Arbitrage Detection (ms): " << duration_cast<microseconds>(strategyComponentArbitrageDetectionTimestamp - bookBuilderUpdateRxTimestamp).count() / 1000.0 << "      "

                        // << "Exch. Ts.: " << updateExchangeTimestamp << "      "
                        // << "Rec. Ts.: " << std::to_string(duration_cast<microseconds>(updateReceiveTimepoint.time_since_epoch()).count()) << "      "
                        // << "Strat. Ts.: " << strategyTimepoint
            << std::endl;

            if (outFile.is_open()) {
                outFile << duration_cast<microseconds>(bookBuilderUpdateRxTimestamp - exchangeUpdateTxTimestamp).count() / 1000.0 << std::endl;
            } else {
                std::cerr << "Unable to open file for writing" << std::endl;
            }

            if (startingTimestamp == time_point<std::chrono::system_clock>::min()) {
                    startingTimestamp = exchangeUpdateTxTimestamp;
            }
            // throughputMonitorStrategyComponent.operationCompleted();

            if (/*firstDirectionReturnsAfterFees > 1.0 &&*/ (duration_cast<microseconds>(strategyComponentArbitrageDetectionTimestamp - startingTimestamp).count() > 1000)) {
                std::string firstLeg = std::string("symbol=XBTUSDT&side=Sell&orderQty=1000") + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
                while (!strategyToOrderManagerQueue.push(firstLeg));

                std::string secondLeg = std::string("symbol=ETHUSDT&side=Buy&orderQty=1000") + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
                while (!strategyToOrderManagerQueue.push(secondLeg));

                std::string thirdLeg = std::string("symbol=XBTETH&side=Buy&orderQty=1") + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
                while (!strategyToOrderManagerQueue.push(thirdLeg));

                startingTimestamp = time_point<high_resolution_clock>(high_resolution_clock::now());
            }

            if (/*secondDirectionReturnsAfterFees > 1.0 &&*/ (duration_cast<microseconds>(strategyComponentArbitrageDetectionTimestamp - startingTimestamp).count() > 1000)) {
                std::string firstLeg = std::string("symbol=XBTETH&side=Sell&orderQty=1") + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
                while (!strategyToOrderManagerQueue.push(firstLeg));

                std::string secondLeg = std::string("symbol=ETHUSDT&side=Sell&orderQty=1000") + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
                while (!strategyToOrderManagerQueue.push(secondLeg));

                std::string thirdLeg = std::string("symbol=XBTUSDT&side=Buy&orderQty=1000") + "&ordType=Market" + exchangeUpdateTxTimepoint + bookBuilderUpdateRxTimepoint + strategyComponentArbitrageDetectionTimepoint;
                while (!strategyToOrderManagerQueue.push(thirdLeg));

                startingTimestamp = time_point<high_resolution_clock>(high_resolution_clock::now());
            }
        }
    }
#endif