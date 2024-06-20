#include "Strategy.hpp"

#define CPU_CORE_INDEX_FOR_STRATEGY_THREAD 3
#define NUMBER_OF_ORDERS_FOR_TRIANGULAR_ARBITRAGE 3
#define AFTER_FEE_RATE 0.99925
#define ORDER_SIZE_RATIO_THRESHOLD 1

struct ExchangeRatePriceAndSize {
    double bestPrice;
    double bestPriceSize;
};

struct MinOrderSizeInfo {
    double minOrderSizeInBaseCurrency;
    double minOrderSizeInQuoteCurrency;
};

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
    #define ORDER_TYPE "Market"
    #define BUY_ORDER "Buy"
    #define SELL_ORDER "Sell"
    const std::unordered_map<string, vector<string>> currencyPairsDict = {
        {"XBT", {"USDT", "ETH"}},
        {"ETH", {"USDT"}},
    };
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    #define ORDER_TYPE "market"
    #define BUY_ORDER "buy"
    #define SELL_ORDER "sell"
    #if defined(USE_PORTFOLIO_122)
        static const std::unordered_map<string, vector<string>> currencyPairsDict = {
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
    #elif defined(USE_PORTFOLIO_92)
        const std::unordered_map<string, vector<string>> currencyPairsDict  = {
            {"BCH", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "USDT", "JPY"}},
            {"BTC", {"USD", "EUR", "USDC", "AUD", "GBP", "CAD", "USDT", "JPY"}},
            {"USD", {"CAD", "JPY"}},
            {"XRP", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "CAD", "USDT"}},
            {"EUR", {"USD", "AUD", "GBP", "CAD", "JPY"}},
            {"LTC", {"USD", "EUR", "BTC", "AUD", "GBP", "ETH", "USDT", "JPY"}},
            {"ETH", {"USD", "EUR", "BTC", "USDC", "AUD", "GBP", "CAD", "USDT", "JPY"}},
            {"LINK", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "USDT", "JPY"}},
            {"ADA", {"USD", "BTC", "EUR", "AUD", "GBP", "ETH", "USDT"}},
            {"USDC", {"USD", "EUR", "AUD", "GBP", "CAD", "USDT"}},
            {"GBP", {"USD"}},
            {"DOT", {"USD", "BTC", "EUR", "GBP", "ETH", "USDT", "JPY"}},
            {"USDT", {"USD", "EUR", "AUD", "GBP", "CAD", "JPY"}},
            {"AUD", {"USD", "JPY"}}
        };
    #elif defined(USE_PORTFOLIO_50)
        const std::unordered_map<string, vector<string>> currencyPairsDict = {
            {"BCH", {"JPY", "ETH", "GBP", "AUD", "BTC", "USDT", "EUR", "USD"}},
            {"USDT", {"JPY", "GBP", "AUD", "EUR", "USD"}},
            {"BTC", {"JPY", "GBP", "AUD", "USDT", "EUR", "USD"}},
            {"EUR", {"GBP", "JPY", "AUD", "USD"}},
            {"ETH", {"JPY", "EUR", "AUD", "BTC", "USDT", "GBP", "USD"}},
            {"USD", {"JPY"}},
            {"LINK", {"JPY", "ETH", "EUR", "AUD", "BTC", "USDT", "GBP", "USD"}},
            {"LTC", {"JPY", "ETH", "GBP", "AUD", "BTC", "USDT", "EUR", "USD"}},
            {"GBP", {"USD"}},
            {"AUD", {"JPY", "USD"}}
        };
    #elif defined(USE_PORTFOLIO_3)
        const std::unordered_map<string, vector<string>> currencyPairsDict = {
            {"USDT", {"USD"}},
            {"SOL", {"USDT", "USD"}},
        };
    #endif
#endif


static std::ofstream strategyComponentDataFile;

static size_t V;
static std::vector<std::vector<std::pair<int, double>>> g;
static std::vector<std::vector<ExchangeRatePriceAndSize>> exchangeRatesMatrix;
static std::vector<std::string> currencies;    
static std::unordered_map<string, int> currencySymbolToIndex;

void createCurrencyGraph() {
    g.resize(V);
    for (int u = 0; u < V; ++u) {
        for (int v = 0; v < V; ++v) {
            if (exchangeRatesMatrix[u][v].bestPrice != 0) {
                g[u].emplace_back(v, 0);
            }
        }
    }
}

void createExchangeRatesMatrix() {
    for (const auto& [key, vals] : currencyPairsDict) {
        currencies.push_back(key);
        currencies.insert(currencies.end(), vals.begin(), vals.end());
    }
    sort(currencies.begin(), currencies.end());
    currencies.erase(unique(currencies.begin(), currencies.end()), currencies.end());

    cout << "CURRENCIES: " << endl;
    for (size_t i = 0; i < currencies.size(); ++i) {
        currencySymbolToIndex[currencies[i]] = i;
        cout << currencies[i] << endl;
    }
    
    int n = currencies.size();
    exchangeRatesMatrix = std::vector<std::vector<ExchangeRatePriceAndSize>>(n, std::vector<ExchangeRatePriceAndSize>(n, {0.0, 0.0}));

    for (const auto& [p1, p2s] : currencyPairsDict) {
        for (const auto& p2 : p2s) {
            exchangeRatesMatrix[currencySymbolToIndex[p1]][currencySymbolToIndex[p2]].bestPrice = 1;
            exchangeRatesMatrix[currencySymbolToIndex[p2]][currencySymbolToIndex[p1]].bestPrice = 1;
        }
    }
}

std::pair<std::vector<int>, std::chrono::system_clock::time_point> findTriangularArbitrage() {
    int V = exchangeRatesMatrix.size();
    vector<double> distances(V);
    vector<int> predecessors(V, -1);
    const int startCurrency = 0;
    distances[startCurrency] = 0;

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
    vector<int> triangularArbitrageCurrencySequence(NUMBER_OF_ORDERS_FOR_TRIANGULAR_ARBITRAGE);
    vector<bool> seen(V, false);
    bool stop = false;
    system_clock::time_point detectionStartTimestamp = std::chrono::high_resolution_clock::now();
    for (int u = 0; u < V; ++u) { 
        for (const auto& [v, weight] : g[u]) {
            if (seen[v] || !(distances[u] < numeric_limits<double>::infinity())) continue;
            if (distances[u] + weight < distances[v]) {
                vector<int> triangularArbitrageCycle;
                int x = v;
                while (true) {
                    if (x == -1) return std::make_pair(triangularArbitrageCurrencySequence, detectionStartTimestamp);
                    seen[x] = true;
                    triangularArbitrageCycle.push_back(x);
                    x = predecessors[x];
                    if (x == v || find(triangularArbitrageCycle.begin(), triangularArbitrageCycle.end(), x) != triangularArbitrageCycle.end()) break;
                }
                if (x == -1) return std::make_pair(triangularArbitrageCurrencySequence, detectionStartTimestamp);
                int idx = find(triangularArbitrageCycle.begin(), triangularArbitrageCycle.end(), x) - triangularArbitrageCycle.begin();
                if (triangularArbitrageCycle.size() - idx == NUMBER_OF_ORDERS_FOR_TRIANGULAR_ARBITRAGE) {
                    triangularArbitrageCycle.push_back(x);
                    triangularArbitrageCurrencySequence = vector<int>(triangularArbitrageCycle.begin() + idx, triangularArbitrageCycle.end()); 
                    reverse(triangularArbitrageCurrencySequence.begin(), triangularArbitrageCurrencySequence.end());
                    stop = true;
                    break;
                }
            }
        }
        if (stop) break;
    }
    
    // return triangularArbitrageCurrencySequence;
    return std::make_pair(triangularArbitrageCurrencySequence, relaxationCompletionTimestamp);
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

void strategy(SPSCQueue<OrderBook>& builderToStrategyQueue, SPSCQueue<StrategyComponentToOrderManagerQueueEntry>& strategyToOrderManagerQueue) {
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

    // strategyComponentDataFile.open("strategy-component-data/new.txt", std::ios_base::out); 
    // if (!strategyComponentDataFile.is_open()) {
    //     std::cerr << "Error: Unable to open file for " << std::endl;
    //     return;
    // }

    ifstream minOrderSizesJsonFile("min-order-sizes.json");
    nlohmann::json minOrderSizesJson;
    minOrderSizesJsonFile >> minOrderSizesJson;
    unordered_map<string, MinOrderSizeInfo> minOrderSizes;
    for (auto& [currencyPair, minOrderSizeInfo] : minOrderSizesJson.items()) {
        minOrderSizes[currencyPair] = {
            minOrderSizeInfo["ordermin"].get<double>(),
            minOrderSizeInfo["costmin"].get<double>()
        };
    }

    createExchangeRatesMatrix();
    V = exchangeRatesMatrix.size();
    createCurrencyGraph();
    
    while (true) {
      OrderBook orderBook;
      while (!builderToStrategyQueue.pop(orderBook));
      system_clock::time_point newOrderBookDetectionTimestamp = high_resolution_clock::now();
      auto bestBuy = orderBook.getBestBuyLimitPriceAndSize();
      auto bestSell = orderBook.getBestSellLimitPriceAndSize();  
      double bestBuyPrice = bestBuy.first;
      double bestBuyPriceSize = bestBuy.second;
      double bestSellPriceReciprocal = 1.0 / bestSell.first;
      double bestSellPriceSize = bestSell.second;

      std::string currencyPair = orderBook.getCurrencyPairSymbol();
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
      std::size_t baseCurrencyEndPos = currencyPair.find('/');
      int quoteCurrencyGraphIndex = currencySymbolToIndex[currencyPair.substr(baseCurrencyEndPos + 1, currencyPair.size())];
#elif defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
      std::size_t baseCurrencyEndPos = 3;
      int quoteCurrencyGraphIndex = currencySymbolToIndex[currencyPair.substr(baseCurrencyEndPos, currencyPair.size())];
#endif
      int baseCurrencyGraphIndex = currencySymbolToIndex[currencyPair.substr(0, baseCurrencyEndPos)];
      //  std::cout << bestBuyPrice << " " << bestSell.first << " " << currencyPair << std::endl;

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
#ifdef VERBOSE_STRATEGY      
      printExchangeRatesMatrix();
      printEdgeWeights();
#endif
      std::chrono::system_clock::time_point findArbitrageStartTimestamp = high_resolution_clock::now();
      std::pair<std::vector<int>, std::chrono::system_clock::time_point> findTriangularArbitrageResult = findTriangularArbitrage();
      std::vector<int> triangularArbitrageCurrencySequence = findTriangularArbitrageResult.first;
      std::chrono::system_clock::time_point relaxationCompletionTimestamp = findTriangularArbitrageResult.second;
      std::chrono::system_clock::time_point arbitrageDetectionCompletionTimestamp = high_resolution_clock::now();
            
      if (triangularArbitrageCurrencySequence.empty())
        continue;   

      if (triangularArbitrageCurrencySequence.size() > 1 && triangularArbitrageCurrencySequence[0] == triangularArbitrageCurrencySequence[1]) { //happens for ADA/ADA
        continue;
      }          
      
      system_clock::time_point marketUpdateExchangeTimestamp = time_point<high_resolution_clock>(microseconds(orderBook.getMarketUpdateExchangeTimestamp()));
      system_clock::time_point orderBookFinalChangeTimestamp = orderBook.getFinalUpdateTimestamp();
      system_clock::time_point updateSocketRxTimeStamp = orderBook.getUpdateSocketRxTimestamp();
      
      std::string marketUpdateExchangeTimepoint = std::to_string(duration_cast<microseconds>(marketUpdateExchangeTimestamp.time_since_epoch()).count());  
      if (marketUpdateExchangeTimepoint == "0") 
        continue;
      
      bool cancelOrders = false;
      double arbitrageProfit = 1;
      double convertedSize;
      StrategyComponentToOrderManagerQueueEntry orderManagerQueueEntries[NUMBER_OF_ORDERS_FOR_TRIANGULAR_ARBITRAGE];
      for (size_t i = 0; i < triangularArbitrageCurrencySequence.size() - 1; ++i) {
          int sourceCurrencyIndex = triangularArbitrageCurrencySequence[i];
          int targetCurrencyIndex = triangularArbitrageCurrencySequence[i + 1];
          std::string sourceCurrencySymbol = currencies[sourceCurrencyIndex];
          std::string targetCurrencySymbol = currencies[targetCurrencyIndex];

          std::string orderSide;
          std::string orderBookSymbol;
          double orderSize;
          double orderSizeRatio;
          auto it = currencyPairsDict.find(sourceCurrencySymbol);
          if (it != currencyPairsDict.end() && std::find(it->second.begin(), it->second.end(), targetCurrencySymbol) != it->second.end()) {
            orderSide = SELL_ORDER;
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)           
            orderBookSymbol = sourceCurrencySymbol + "/" + targetCurrencySymbol;
#elif defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
            orderBookSymbol = sourceCurrencySymbol + targetCurrencySymbol;
#endif  
            // std::cout << orderBookSymbol<< std::endl;
            if (i == 0) 
                orderSize = minOrderSizes.find(orderBookSymbol)->second.minOrderSizeInBaseCurrency;
            else 
                orderSize = convertedSize;
            convertedSize = orderSize /*in base*/ * exchangeRatesMatrix[sourceCurrencyIndex][targetCurrencyIndex].bestPrice; /*in quote*/ 
          } else {
            orderSide = BUY_ORDER;
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)           
            orderBookSymbol = targetCurrencySymbol + "/" + sourceCurrencySymbol;
#elif defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
            orderBookSymbol = targetCurrencySymbol + sourceCurrencySymbol;
#endif  
            // std::cout << orderBookSymbol<< std::endl;
            if (i == 0) 
                orderSize = minOrderSizes.find(orderBookSymbol)->second.minOrderSizeInBaseCurrency;
            else 
                orderSize = convertedSize /*in quote*/ * exchangeRatesMatrix[sourceCurrencyIndex][targetCurrencyIndex].bestPrice /*in base*/; /*reciprocal*/
            convertedSize = orderSize; // in base
          }
          
          orderSizeRatio = orderSize / exchangeRatesMatrix[sourceCurrencyIndex][targetCurrencyIndex].bestPriceSize;  
          if (orderSizeRatio > ORDER_SIZE_RATIO_THRESHOLD) {
            cancelOrders = true;
            // break;
          }

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
          orderManagerQueueEntries[i].order = std::string("symbol=") + orderBookSymbol + "&side=" + orderSide + "&orderQty=" + std::to_string(orderSize) + "&ordType=" + ORDER_TYPE;
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)          
          orderManagerQueueEntries[i].order = std::string("pair=") + orderBookSymbol + "&type=" + orderSide + "&volume=" + std::to_string(orderSize) + "&ordertype=" + ORDER_TYPE;
#endif          
          orderManagerQueueEntries[i].marketUpdateExchangeTimestamp = marketUpdateExchangeTimestamp;
          orderManagerQueueEntries[i].orderBookFinalChangeTimestamp = orderBookFinalChangeTimestamp;
          orderManagerQueueEntries[i].updateSocketRxTimeStamp = updateSocketRxTimeStamp;

          arbitrageProfit *= exchangeRatesMatrix[sourceCurrencyIndex][targetCurrencyIndex].bestPrice;

        //   std::cout << "NEW ORDER CREATED: " << orderManagerQueueEntries[i].order << std::endl; 
      }

      std::chrono::system_clock::time_point ordersCreationTimestamp = high_resolution_clock::now();

      if (cancelOrders || arbitrageProfit < 1.000) 
        continue;

      for (int i = 0; i < NUMBER_OF_ORDERS_FOR_TRIANGULAR_ARBITRAGE; ++i) {
        orderManagerQueueEntries[i].strategyOrderPushTimestamp = high_resolution_clock::now();
        while (!strategyToOrderManagerQueue.push(orderManagerQueueEntries[i]));
      }    

    //   std::chrono::system_clock::time_point arbitrageOrdersCreationCompletionTimestamp = high_resolution_clock::now();  
    //   auto orderBookFinalChangeUs = timePointToMicroseconds(orderBookFinalChangeTimestamp);
    //   auto newOrderBookDetectionUs = timePointToMicroseconds(newOrderBookDetectionTimestamp);
    //   auto findArbitrageStartUs = timePointToMicroseconds(findArbitrageStartTimestamp);
    //   auto relaxationCompletionUs = timePointToMicroseconds(relaxationCompletionTimestamp);
    //   auto arbitrageDetectionCompletionUs = timePointToMicroseconds(arbitrageDetectionCompletionTimestamp);
    //   auto ordersCreationUs = timePointToMicroseconds(ordersCreationTimestamp);
    //   double queueLatency = (newOrderBookDetectionUs - orderBookFinalChangeUs) / 1000.0; 
    //   double modificationLatency = (findArbitrageStartUs - newOrderBookDetectionUs) / 1000.0;
    //   double relaxationLatency = (relaxationCompletionUs - findArbitrageStartUs) / 1000.0;
    //   double detectionLatency = (arbitrageDetectionCompletionUs - relaxationCompletionUs) / 1000.0;
    //   double creationLatency = (ordersCreationUs - arbitrageDetectionCompletionUs) / 1000.0;
  
    //   strategyComponentDataFile 
    //   << queueLatency << ", "
    //   << modificationLatency << ", "
    //   << relaxationLatency << ", "
    //   << detectionLatency << ", "
    //   << creationLatency 
    //   << std::endl;
      
      cout << "Expected percentage profit for the detected triangular arbitrage: " << (arbitrageProfit - 1) * 100 << "%" << endl;
 
 #ifdef VERBOSE_STRATEGY
      std::cout 
      << "Exchange Update Occurence to Update Receival (ms): " << duration_cast<microseconds>(orderBookFinalChangeTimestamp - marketUpdateExchangeTimestamp).count() / 1000.0 << "      "
      << "Update Receival to Arbitrage Detection (ms): " << duration_cast<microseconds>(arbitrageDetectionCompletionTimestamp - orderBookFinalChangeTimestamp).count() / 1000.0 << "      "
      << std::endl;
#endif
    //   std::cout << "TRIANGULAR ARBITRAGE OPPORTUNITY FOUND" << std::endl;  
    //   cout << "Currency conversions for triangular arbitrage opportunity: ";
    //   for (int currency : triangularArbitrageCurrencySequence) {
    //       cout << currency << " ";
    //   }
    //   cout << endl;
    }
}
