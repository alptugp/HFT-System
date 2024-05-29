#include <libwebsockets.h>
#include <rapidjson/document.h>
#include <string>
#include <signal.h>
#include <chrono>
#include <ev.h>
#include <liburing.h>
#include <fstream>
#include <sys/socket.h>
#include "./OrderBook/OrderBook.hpp"
#include "./ThroughputMonitor/ThroughputMonitor.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

#define CPU_CORE_INDEX_FOR_BOOK_BUILDER_COMPONENT_THREAD 2

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
    #define JSON_START_PATTERN "{\"table\""
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    #define JSON_START_PATTERN "{\"channel\":\"book\""
#endif

#define JSON_END_PATTERN "}]}"

using namespace rapidjson;
using namespace std::chrono;

static std::unordered_map<std::string, OrderBook> orderBookMap;

static std::ofstream latencyDataFile;
static std::ofstream historicalDataFile;

void bookBuilder(SPSCQueue<BookBuilderGatewayToComponentQueueEntry>& bookBuilderGatewayToComponentQueue, SPSCQueue<OrderBook>& bookBuilderToStrategyQueue, std::vector<std::string> currencyPairs) {
    int numCores = std::thread::hardware_concurrency();
    
    if (numCores == 0) {
        std::cerr << "Error: Unable to determine the number of CPU cores." << std::endl;
        return;
    } else if (numCores < CPU_CORE_INDEX_FOR_BOOK_BUILDER_COMPONENT_THREAD) {
        std::cerr << "Error: Not enough cores to run the system." << std::endl;
        return;
    }

    int cpuCoreNumberForBookBuilderThread = CPU_CORE_INDEX_FOR_BOOK_BUILDER_COMPONENT_THREAD;
    setThreadAffinity(pthread_self(), cpuCoreNumberForBookBuilderThread);

    // Set the current thread's real-time priority to highest value
    // struct sched_param schedParams;
    // schedParams.sched_priority = sched_get_priority_max(SCHED_FIFO);
    // pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParams);

    for (std::string currencyPair : currencyPairs) { 
        orderBookMap[currencyPair] = OrderBook(currencyPair);
    }

    // ThroughputMonitor updateThroughputMonitorBookBuilder("Book Builder Throughput Monitor", std::chrono::high_resolution_clock::now());
    // updateThroughputMonitor = &updateThroughputMonitorBookBuilder;

    #if defined(USE_BITMEX_MOCK_EXCHANGE)    
        latencyDataFile.open("bitmex-book-builder-data/new.txt", std::ios_base::out); 
    #elif defined(USE_KRAKEN_MOCK_EXCHANGE)
        latencyDataFile.open("polling-and-reading-book-builder-data/new.txt", std::ios_base::out); 
        // latencyDataFile.open("conn-type-kraken-book-builder/conn-type-kraken-book-builder-data.txt", std::ios_base::out); 
    #endif

#if defined(USE_BITMEX_EXCHANGE)    
    // historicalDataFile.open("bitmex_data.txt", std::ios_base::out);
#elif defined(USE_KRAKEN_EXCHANGE)
    // historicalDataFile.open("kraken_data.txt", std::ios_base::out);
#endif

    while (true) {
        struct BookBuilderGatewayToComponentQueueEntry queueEntry;
        while (!bookBuilderGatewayToComponentQueue.pop(queueEntry)) {};

        removeIncorrectNullCharacters(queueEntry.decryptedReadBuffer, queueEntry.decryptedBytesRead);
        // std::cout << "BUFFER: " << queueEntry.decryptedReadBuffer << std::endl;
        const char* currentPos = queueEntry.decryptedReadBuffer;
        while (currentPos < queueEntry.decryptedReadBuffer + strlen(queueEntry.decryptedReadBuffer)) {
            const char* startPos = strstr(currentPos, JSON_START_PATTERN);
            if (!startPos) 
                break;
            const char* endPos = strstr(startPos, JSON_END_PATTERN);
            if (!endPos) 
                break;
            
            // Extract the substring containing the JSON object
            size_t jsonLen = endPos - startPos + strlen(JSON_END_PATTERN);
            
            if (jsonLen >= WEBSOCKET_CLIENT_RX_BUFFER_SIZE) {
                std::cerr << "JSON string too long\n";
                break;
            }
            
            char jsonStr[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];
            strncpy(jsonStr, startPos, jsonLen);
            jsonStr[jsonLen] = '\0';
            
            // if (historicalDataFile.is_open()) {
            //     historicalDataFile << jsonStr << std::endl;
            // } else {
            //     std::cerr << "Unable to open file: " << "kraken_data.txt" << std::endl;
            // }
            Document doc;
            doc.Parse(jsonStr);
            if (doc.HasParseError()) {
                std::cerr << "JSON parsing error\n";
                break;
            }
            system_clock::time_point marketUpdateJsonParsingCompletionTimestamp = high_resolution_clock::now();
            long exchangeUpdateRxTimestamp;
            std::vector<std::string> updatedCurrencies;
            GenericValue<rapidjson::UTF8<>>::MemberIterator data = doc.FindMember("data");

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) 
            const char* action = doc["action"].GetString();

            for (SizeType i = 0; i < doc["data"].Size(); i++) {
                const Value& data_i = data->value[i];
                const char* symbol = data_i["symbol"].GetString();
                uint64_t id = data_i["id"].GetInt64();
                const char* side = data_i["side"].GetString();
                double size;
                if (data->value[i].HasMember("size")) 
                    size = data_i["size"].GetInt64();
                
                double price = data_i["price"].GetDouble();
                const char* timestamp = data_i["timestamp"].GetString();
                exchangeUpdateRxTimestamp = convertTimestampToTimePoint(timestamp);
                // std::cout << "exchangeUpdateTimestamp: " << exchangeUpdateTimestamp << ", marketUpdateReceiveTimestamp: " << std::to_string(duration_cast<milliseconds>(marketUpdateReceiveTimestamp.time_since_epoch()).count()) << std::endl;
                if (strcmp(side, "Buy") == 0) {
                    switch (action[0]) {
                        case 'p':
                        case 'i':
                            orderBookMap[symbol].insertBuy(id, price, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadFinishTimestamp);
                            break;
                        case 'u':
                            orderBookMap[symbol].updateBuy(id, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadFinishTimestamp);
                            break;
                        case 'd':
                            orderBookMap[symbol].removeBuy(id, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadFinishTimestamp);
                            break;
                        default:
                            break;
                    }
                } else if (strcmp(side, "Sell") == 0) {
                    switch (action[0]) {
                        case 'p':
                        case 'i':
                            orderBookMap[symbol].insertSell(id, price, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadFinishTimestamp);
                            break;
                        case 'u':
                            orderBookMap[symbol].updateSell(id, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadFinishTimestamp);
                            break;
                        case 'd':
                            orderBookMap[symbol].removeSell(id, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadFinishTimestamp);
                            break;
                        default:
                            break;
                    }
                }
                updatedCurrencies.push_back(std::string(symbol));
            } 

#elif defined(USE_KRAKEN_EXCHANGE) || defined (USE_KRAKEN_MOCK_EXCHANGE)
            const char* type = doc["type"].GetString();

            for (SizeType i = 0; i < doc["data"].Size(); i++) {
                const Value& data_i = data->value[i];
                GenericValue<rapidjson::UTF8<>>::ConstMemberIterator asks = data_i.FindMember("asks");
                GenericValue<rapidjson::UTF8<>>::ConstMemberIterator bids = data_i.FindMember("bids");
                const char* symbol = data_i["symbol"].GetString();    
                uint64_t checksum = data_i["checksum"].GetInt64();
                
                if (type[0] == 's') {
                    for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                        const Value& ask_i = asks->value[i];
                        double price = ask_i["price"].GetDouble();
                        double size = ask_i["qty"].GetDouble();
                        orderBookMap[symbol].insertSell(price, price, size, 0, queueEntry.marketUpdateReadCompletionTimestamp);
                    }

                    for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                        const Value& bid_i = bids->value[i];
                        double price = bid_i["price"].GetDouble();
                        double size = bid_i["qty"].GetDouble();
                        orderBookMap[symbol].insertBuy(price, price, size, 0, queueEntry.marketUpdateReadCompletionTimestamp);
                    }
                } else if (type[0] == 'u') {
                    const char* timestamp = data_i["timestamp"].GetString();
                    exchangeUpdateRxTimestamp = convertTimestampToTimePoint(timestamp);
                    GenericValue<rapidjson::UTF8<>>::ConstMemberIterator asks = data_i.FindMember("asks");
                    GenericValue<rapidjson::UTF8<>>::ConstMemberIterator bids = data_i.FindMember("bids");
                    const char* symbol = data_i["symbol"].GetString();    
                    uint64_t checksum = data_i["checksum"].GetInt64();

                    for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                        const Value& ask_i = asks->value[i]; 
                        double price = ask_i["price"].GetDouble();
                        double size = ask_i["qty"].GetDouble();
                        if (size == 0)
                            orderBookMap[symbol].removeSell(price, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadCompletionTimestamp);
                        else if (orderBookMap[symbol].checkSellPriceLevel(price))
                            orderBookMap[symbol].updateSell(price, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadCompletionTimestamp);
                        else 
                            orderBookMap[symbol].insertSell(price, price, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadCompletionTimestamp);
                    }
                    
                    for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                        const Value& bid_i = bids->value[i]; 
                        double price = bid_i["price"].GetDouble();
                        double size = bid_i["qty"].GetDouble();
                        if (size == 0)
                            orderBookMap[symbol].removeBuy(price, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadCompletionTimestamp);
                        else if (orderBookMap[symbol].checkBuyPriceLevel(price))
                            orderBookMap[symbol].updateBuy(price, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadCompletionTimestamp);
                        else 
                            orderBookMap[symbol].insertBuy(price, price, size, exchangeUpdateRxTimestamp, queueEntry.marketUpdateReadCompletionTimestamp);
                    }
                }
                updatedCurrencies.push_back(std::string(symbol));
            }
#endif          
            system_clock::time_point marketUpdateBookBuildingCompletionTimestamp = high_resolution_clock::now();
                        
            for (std::string updatedCurrency : updatedCurrencies) { 
                std::cout << "UPDATED CURRENCY: " << updatedCurrency << std::endl;
                while (!bookBuilderToStrategyQueue.push(orderBookMap[updatedCurrency]));    
            }

            // Convert time_point to microseconds since epoch
            auto socketRxUs = timePointToMicroseconds(queueEntry.marketUpdateSocketRxTimestamp);
            auto pollUs = timePointToMicroseconds(queueEntry.marketUpdatePollTimestamp);
            auto readFinishUs = timePointToMicroseconds(queueEntry.marketUpdateReadCompletionTimestamp);
            auto decryptionFinishUs = timePointToMicroseconds(queueEntry.marketUpdateDecryptionCompletionTimestamp);
            auto jsonParsingFinishUs = timePointToMicroseconds(marketUpdateJsonParsingCompletionTimestamp);
            auto bookBuildingFinishUs = timePointToMicroseconds(marketUpdateBookBuildingCompletionTimestamp);
            double networkLatency = (socketRxUs - exchangeUpdateRxTimestamp) / 1000.0; 
            double socketWaitLatency = (pollUs - socketRxUs) / 1000.0;
            double readLatency = (readFinishUs - pollUs) / 1000.0;
            double decryptionLatency = (decryptionFinishUs - readFinishUs) / 1000.0;
            double jsonParsingLatency = (jsonParsingFinishUs - decryptionFinishUs) / 1000.0;
            double bookBuildingLatency = (bookBuildingFinishUs - jsonParsingFinishUs) / 1000.0;

            latencyDataFile 
            << networkLatency << ", "
            << socketWaitLatency << ", "
            << readLatency << ", "
            << decryptionLatency << ", "
            << jsonParsingLatency << ", "
            << bookBuildingLatency 
            << std::endl;
            // Move to the next JSON object
            currentPos = endPos + 1;
        }
    }    
}