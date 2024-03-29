#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <string>
#include <chrono>
#include "BitmexClient.hpp"
#include "./OrderBook/OrderBook.hpp"
#include "./ThroughputMonitor/ThroughputMonitor.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

using client = websocketpp::client<websocketpp::config::asio_tls_client>;
using context_ptr = websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

using Clock = std::chrono::high_resolution_clock;

static context_ptr on_tls_init(websocketpp::connection_hdl) {
    using namespace boost::asio::ssl;
    context_ptr ctx = websocketpp::lib::make_shared<context>(context::sslv23);
    return ctx;
}

/*const std::vector<std::string> currencyPairs = {"BTCUSD", "ETHBTC", "ETHUSD"};*/
const std::vector<std::string> currencyPairs = {"XBTETH", "XBTUSDT", "ETHUSDT"};
// const std::vector<std::string> currencyPairs = {"XBTUSD", "ETHUSD", "XBTETH", "XBTUSDT", "SOLUSD", "XRPUSD", "LINKUSD", "SOLUSD", "XRPUSD"};
// const std::vector<std::string> currencyPairs = {"XBTUSD", "ETHUSD", "SOLUSD", "XBTUSDT", "DOGEUSD", "XBTH24", "LINKUSD", "XRPUSD", "SOLUSDT", "BCHUSD"};

static void on_open(client* c, BitmexClient::websocket::Client* bmxClient, websocketpp::connection_hdl hdl) {
    for (std::string currencyPair : currencyPairs) {
        auto msg = bmxClient->make_subscribe(currencyPair, BitmexClient::websocket::Topic::OrderBookL2_25);
        c->send(hdl, msg, websocketpp::frame::opcode::text);
    } 
}

static void on_message(BitmexClient::websocket::Client* client, websocketpp::connection_hdl, client::message_ptr msg) {
    // Parse the message received from BitMEX. Note that "client" invokes all the relevant callbacks.
    client->parse_msg(msg->get_payload());
}

void onTradeCallBack(std::unordered_map<std::string, OrderBook>& orderBookMap, ThroughputMonitor& updateThroughputMonitor, SPSCQueue<OrderBook>& bookBuilderToStrategyQueue, const char* symbol, 
                    const char* action, uint64_t id, const char* side, int size, double price, const char* timestamp, system_clock::time_point marketUpdateReceiveTimestamp) {
    // auto programReceiveTime = Clock::now();
    updateThroughputMonitor.operationCompleted();

    long exchangeUpdateTimestamp = convertTimestampToTimePoint(timestamp);
    /*std::cout << updateExchangeTimestamp << std::endl;*/
    // auto networkLatency = std::chrono::duration_cast<std::chrono::microseconds>(programReceiveTime - exchangeUnixTimestamp);
    
    std::string sideStr(side);
    if (sideStr == "Buy") {
        switch (action[0]) {
            case 'p':
            case 'i':
                orderBookMap[symbol].insertBuy(id, price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                break;
            case 'u':
                orderBookMap[symbol].updateBuy(id, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                break;
            case 'd':
                orderBookMap[symbol].removeBuy(id, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                break;
            default:
                break;
        }
    } else if (sideStr == "Sell") {
        switch (action[0]) {
            case 'p':
            case 'i':
                orderBookMap[symbol].insertSell(id, price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                break;
            case 'u':
                orderBookMap[symbol].updateSell(id, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                break;
            case 'd':
                orderBookMap[symbol].removeSell(id, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                break;
            default:
                break;
        }
    }
    
    while (!bookBuilderToStrategyQueue.push(orderBookMap[symbol]));

    // auto bookBuildingEndTime = Clock::now();
    // auto bookBuildingDuration = std::chrono::duration_cast<std::chrono::microseconds>(bookBuildingEndTime - programReceiveTime);
    // std::cout << "Symbol: " << symbol << " - Action: " << action << " - Size: " << size << " - Price: " << price << " (" << side << ")" << " - id: " << id << " - Timestamp: " << updateExchangeTimestamp << std::endl;
    // std::cout << "Time taken to receive market update: " << networkLatency.count() << " microseconds" << std::endl;
    // std::cout << "Time taken to process market update: " << bookBuildingDuration.count() << " microseconds" << std::endl;
    // std::cout << "Timestamp: " << updateExchangeTimestamp << std::endl;
    // orderBook.printOrderBook();
    // orderBook.updateOrderBookMemoryUsage();
}

void bookBuilder(int cpu, SPSCQueue<OrderBook>& bookBuilderToStrategyQueue) {
    pinThread(cpu);

    std::unordered_map<std::string, OrderBook> orderBookMap;

    for (std::string currencyPair : currencyPairs) { 
        orderBookMap[currencyPair] = OrderBook(currencyPair);
    }

    ThroughputMonitor updateThroughputMonitorBookBuilder("Book Builder Throughput Monitor", std::chrono::high_resolution_clock::now());

    BitmexClient::websocket::Client bitmexClient;

    auto onTradeCallBackLambda = [&orderBookMap, &updateThroughputMonitorBookBuilder, &bookBuilderToStrategyQueue](const char* symbol, const char* action,
        uint64_t id, const char* side, int size, double price, const char* timestamp, system_clock::time_point marketUpdateReceiveTimestamp) {
        onTradeCallBack(orderBookMap, updateThroughputMonitorBookBuilder, bookBuilderToStrategyQueue, symbol, action, id, side, size, price, timestamp, marketUpdateReceiveTimestamp);
    };

    bitmexClient.on_trade(onTradeCallBackLambda);

    std::string uri = "wss://ws.testnet.bitmex.com/realtime";
    /*std::string uri = "wss://www.bitmex.com/realtime";*/
    
    client c;
    c.clear_access_channels(websocketpp::log::alevel::frame_payload);
    c.init_asio();
    c.set_tls_init_handler(&on_tls_init);
    c.set_open_handler(bind(&on_open, &c, &bitmexClient, ::_1));
    c.set_message_handler(bind(&on_message, &bitmexClient, ::_1, ::_2));

    websocketpp::lib::error_code ec;
    client::connection_ptr con = c.get_connection(uri, ec);
    if (ec) {
        std::cout << "Failed to create connection: " << ec.message() << std::endl;
        return;
    }
    c.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

    c.connect(con);

    c.run();
}




