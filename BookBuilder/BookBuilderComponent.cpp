#include <libwebsockets.h>
#include <rapidjson/document.h>
#include <string>
#include <signal.h>
#include <chrono>
#include <ev.h>
#include <liburing.h>
#include <fstream>
#include <sys/socket.h>
#include <algorithm>
#include "../OrderBook/OrderBook.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

#define CPU_CORE_INDEX_FOR_BOOK_BUILDER_COMPONENT_THREAD 2

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
    #define JSON_START_PATTERN "{\"table\""
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    #define JSON_START_PATTERN "{\"channel\":\"book\""
#endif

#define JSON_END_PATTERN "}]}"

using namespace rapidjson;
using namespace std::chrono;

static std::unordered_map<std::string, OrderBook> orderBookMap;

#if defined(USE_KRAKEN_EXCHANGE) 
static std::unordered_map<std::string, std::ofstream> historicalDataFiles;
#endif

static std::ofstream latencyDataFile;

void bookBuilderComponent(SPSCQueue<BookBuilderGatewayToComponentQueueEntry>& bookBuilderGatewayToComponentQueue, SPSCQueue<OrderBook>& bookBuilderToStrategyQueue, std::vector<std::string> currencyPairs) {
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

    for (std::string currencyPair : currencyPairs) { 
        orderBookMap[currencyPair] = OrderBook(currencyPair);
    }

    const char *currentPos, *startPos, *endPos;
    char jsonStr[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];
    size_t jsonLen;
    system_clock::time_point marketUpdateJsonParsingCompletionTimestamp, marketUpdateBookBuildingCompletionTimestamp;
    long marketUpdateExchangeTimestamp;
    GenericValue<rapidjson::UTF8<>>::MemberIterator data;
#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
    const char *action, *symbol, *side, *exchangeTimestamp;
    uint64_t id;
    double size, price;
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    const char *type, *symbol, *exchangeTimestamp;
    double price, size;
    uint64_t checksum;
    uint64_t prevChecksum = 0;
    GenericValue<rapidjson::UTF8<>>::ConstMemberIterator asks;
    GenericValue<rapidjson::UTF8<>>::ConstMemberIterator bids;
#endif

    while (true) {
        struct BookBuilderGatewayToComponentQueueEntry queueEntry;
        while (!bookBuilderGatewayToComponentQueue.pop(queueEntry)) {};

        removeIncorrectNullCharacters(queueEntry.decryptedReadBuffer, queueEntry.decryptedBytesRead);
#ifdef VERBOSE_BOOK_BUILDER
        std::cout << queueEntry.decryptedReadBuffer << std::endl;
#endif
        currentPos = queueEntry.decryptedReadBuffer;
        size_t jsonNo, stop = 0;
        while (currentPos < queueEntry.decryptedReadBuffer + strlen(queueEntry.decryptedReadBuffer)) {
            startPos = strstr(currentPos, JSON_START_PATTERN);
            if (!startPos) 
                break;
            endPos = strstr(startPos, JSON_END_PATTERN);
            if (!endPos) 
                break;
            
            // Extract the substring containing the JSON object
            jsonLen = endPos - startPos + strlen(JSON_END_PATTERN);
            if (jsonLen >= WEBSOCKET_CLIENT_RX_BUFFER_SIZE) {
                std::cerr << "JSON string too long\n";
                break;
            }
            
            strncpy(jsonStr, startPos, jsonLen);
            jsonStr[jsonLen] = '\0';
            
            Document doc;
            doc.Parse(jsonStr);
            if (doc.HasParseError()) {
                std::cerr << "JSON parsing error\n";
                break;
            }
            jsonNo++;
            marketUpdateJsonParsingCompletionTimestamp = high_resolution_clock::now();
            data = doc.FindMember("data");
#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
            action = doc["action"].GetString();

            for (SizeType i = 0; i < doc["data"].Size(); i++) {
                const Value& data_i = data->value[i];
                symbol = data_i["symbol"].GetString();
                id = data_i["id"].GetInt64();
                side = data_i["side"].GetString();
                if (data->value[i].HasMember("size")) 
                    size = data_i["size"].GetInt64();
                price = data_i["price"].GetDouble();
                exchangeTimestamp = data_i["timestamp"].GetString();
                marketUpdateExchangeTimestamp = timePointToMicroseconds(convertTimestampToTimePoint(exchangeTimestamp));
                if (side[0] == 'B') {
                    switch (action[0]) {
                        case 'p':
                        case 'i':
                            orderBookMap[symbol].insertBuy(id, price, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                            break;
                        case 'u':
                            orderBookMap[symbol].updateBuy(id, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                            break;
                        case 'd':
                            orderBookMap[symbol].removeBuy(id, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                            break;
                        default:
                            break;
                    }
                } else if (side[0] == 'S') {
                    switch (action[0]) {
                        case 'p':
                        case 'i':
                            orderBookMap[symbol].insertSell(id, price, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                            break;
                        case 'u':
                            orderBookMap[symbol].updateSell(id, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                            break;
                        case 'd':
                            orderBookMap[symbol].removeSell(id, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                            break;
                        default:
                            break;
                    }
                }
            }
            marketUpdateBookBuildingCompletionTimestamp = high_resolution_clock::now(); 
            while (!bookBuilderToStrategyQueue.push(orderBookMap[symbol]));   
#elif defined(USE_KRAKEN_EXCHANGE) || defined (USE_KRAKEN_MOCK_EXCHANGE)
            type = doc["type"].GetString();

            for (SizeType i = 0; i < doc["data"].Size(); i++) {
                const Value& data_i = data->value[i];
                asks = data_i.FindMember("asks");
                bids = data_i.FindMember("bids");
                symbol = data_i["symbol"].GetString();    
                checksum = data_i["checksum"].GetInt64();
                if (prevChecksum != 0) {
                    if (checksum == prevChecksum) {
                        stop++;
                        break;
                    }
                }
                prevChecksum = checksum;
                if (type[0] == 's') {
                    for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                        const Value& ask_i = asks->value[i];
                        price = ask_i["price"].GetDouble();
                        size = ask_i["qty"].GetDouble();
                        orderBookMap[symbol].insertSell(price, price, size, 0, queueEntry.marketUpdateSocketRxTimestamp);
                    }
                    for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                        const Value& bid_i = bids->value[i];
                        price = bid_i["price"].GetDouble();
                        size = bid_i["qty"].GetDouble();
                        orderBookMap[symbol].insertBuy(price, price, size, 0, queueEntry.marketUpdateSocketRxTimestamp);
                    }
                } else if (type[0] == 'u') {
                    exchangeTimestamp = data_i["timestamp"].GetString();
                    marketUpdateExchangeTimestamp = timePointToMicroseconds(convertTimestampToTimePoint(exchangeTimestamp));
                    asks = data_i.FindMember("asks");
                    bids = data_i.FindMember("bids");
                    symbol = data_i["symbol"].GetString();    
                    checksum = data_i["checksum"].GetInt64();
                    for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                        const Value& ask_i = asks->value[i]; 
                        price = ask_i["price"].GetDouble();
                        size = ask_i["qty"].GetDouble();
                        if (size == 0)
                            orderBookMap[symbol].removeSell(price, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                        else if (orderBookMap[symbol].checkSellSidePriceLevel(price))
                            orderBookMap[symbol].updateSell(price, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                        else 
                            orderBookMap[symbol].insertSell(price, price, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                    }
                    for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                        const Value& bid_i = bids->value[i]; 
                        price = bid_i["price"].GetDouble();
                        size = bid_i["qty"].GetDouble();
                        if (size == 0)
                            orderBookMap[symbol].removeBuy(price, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                        else if (orderBookMap[symbol].checkBuySidePriceLevel(price))
                            orderBookMap[symbol].updateBuy(price, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                        else 
                            orderBookMap[symbol].insertBuy(price, price, size, marketUpdateExchangeTimestamp, queueEntry.marketUpdateSocketRxTimestamp);
                    }
                }

                while (!bookBuilderToStrategyQueue.push(orderBookMap[symbol]));
                marketUpdateBookBuildingCompletionTimestamp = high_resolution_clock::now();    
#ifdef VERBOSE_BOOK_BUILDER
                orderBookMap[symbol].printOrderBook();
#endif
            }
#endif          
            if (stop == 1) {
                stop = 0;
                break;
            }

#if defined(USE_KRAKEN_EXCHANGE)
            if (symbol)
                historicalDataFiles[symbol] << jsonStr << std::endl;
#endif 

            memset(jsonStr, 0, WEBSOCKET_CLIENT_RX_BUFFER_SIZE);

            // Move to the next JSON object
            currentPos = endPos + 1;
        }
    }    
}