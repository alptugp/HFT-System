#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <bitmex/bitmex.hpp>
#include <string>
#include <chrono>
#include "./OrderBook/OrderBook.hpp"
#include "./ThroughputMonitor/ThroughputMonitor.hpp"


using client = websocketpp::client<websocketpp::config::asio_tls_client>;
using context_ptr = websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

using Clock = std::chrono::high_resolution_clock;

std::chrono::time_point<Clock> convertTimestampToTimePoint(const std::string& timestamp) {
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    // If your timestamp includes milliseconds, parse and add them
    long milliseconds = 0;
    if (ss && ss.peek() == '.') {
        ss.ignore(); // Ignore the dot
        ss >> milliseconds;
    }

    auto time_point = Clock::from_time_t(std::mktime(&tm)) + std::chrono::milliseconds(milliseconds);
    return time_point;
}


static context_ptr on_tls_init(websocketpp::connection_hdl) {
    using namespace boost::asio::ssl;
    context_ptr ctx = websocketpp::lib::make_shared<context>(context::sslv23);
    return ctx;
}

static void on_open(client* c, bitmex::websocket::Client* bmxClient, websocketpp::connection_hdl hdl) {
    auto msgXBTUSD = bmxClient->make_subscribe("XBTUSD", bitmex::websocket::Topic::OrderBookL2_25);
    c->send(hdl, msgXBTUSD, websocketpp::frame::opcode::text);

    auto msgETHUSD = bmxClient->make_subscribe("ETHUSD", bitmex::websocket::Topic::OrderBookL2_25);
    c->send(hdl, msgETHUSD, websocketpp::frame::opcode::text);

    auto msgXBTETH = bmxClient->make_subscribe("XBTETH", bitmex::websocket::Topic::OrderBookL2_25);
    c->send(hdl, msgXBTETH, websocketpp::frame::opcode::text);
}

static void on_message(bitmex::websocket::Client* client, websocketpp::connection_hdl, client::message_ptr msg) {
    // Parse the message received from BitMEX. Note that "client" invokes all the relevant callbacks.
    client->parse_msg(msg->get_payload());
}

void onTradeCallBack(std::unordered_map<std::string, OrderBook>& orderBookMap, [[maybe_unused]] ThroughputMonitor& throughputMonitor, SPSCQueue<OrderBook>& queue, [[maybe_unused]] const char* symbol, 
                    const char* action, uint64_t id, const char* side, int size, double price, [[maybe_unused]] const char* timestamp) {
    // auto programReceiveTime = Clock::now();
    // throughputMonitor.onTradeReceived();
    // auto exchangeTimeStamp = convertTimestampToTimePoint(timestamp);
    // auto networkLatency = std::chrono::duration_cast<std::chrono::microseconds>(programReceiveTime - exchangeTimeStamp);
    // std::cout << action << std::endl;
    std::string sideStr(side);
    if (sideStr == "Buy") {
        switch (action[0]) {
            case 'p':
            case 'i':
                orderBookMap[symbol].insertBuy(id, price, size);
                break;
            case 'u':
                orderBookMap[symbol].updateBuy(id, size);
                break;
            case 'd':
                orderBookMap[symbol].removeBuy(id);
                break;
            default:
                // Handle other message types if needed
                break;
        }
    } else if (sideStr == "Sell") {
        switch (action[0]) {
            case 'p':
            case 'i':
                orderBookMap[symbol].insertSell(id, price, size);
                break;
            case 'u':
                orderBookMap[symbol].updateSell(id, size);
                break;
            case 'd':
                orderBookMap[symbol].removeSell(id);
                break;
            default:
                // Handle other message types if needed
                break;
        }
    }
    
    while (!queue.push(orderBookMap[symbol]));
    // throughputMonitor.operationCompleted();

    // auto bookBuildingEndTime = Clock::now();
    // auto bookBuildingDuration = std::chrono::duration_cast<std::chrono::microseconds>(bookBuildingEndTime - programReceiveTime);
    // std::cout << "Symbol: " << symbol << " - Action: " << action << " - Size: " << size << " - Price: " << price << " (" << side << ")" << " - id: " << id << " - Timestamp: " << timestamp << std::endl;
    // std::cout << "Time taken to receive market update: " << networkLatency.count() << " microseconds" << std::endl;
    // std::cout << "Time taken to process market update: " << bookBuildingDuration.count() << " microseconds" << std::endl;
    // std::cout << "Timestamp: " << timestamp << std::endl;
    // orderBook.printOrderBook();
    // orderBook.updateOrderBookMemoryUsage();
}

// int main() {   
//     OrderBook orderBook = OrderBook(currencyPair);
//     // Create BitMEX C++ API client object.
//     bitmex::websocket::Client bmxClient;

//     // Create a ThroughputMonitor and pass the start time
//     ThroughputMonitor throughputMonitor(std::chrono::high_resolution_clock::now());

//     auto onTradeCallBackLambda = [&orderBook, &throughputMonitor]([[maybe_unused]] const char* symbol, const char action, 
//         uint64_t id, const char* side, int size, double price, const char* timestamp) {
//         onTradeCallBack(orderBook, throughputMonitor, symbol, action, id, side, size, price, timestamp);
//     };
//     // Register a callback that is invoked when a trade is reported.
//     bmxClient.on_trade(onTradeCallBackLambda);

//     // Setup WebSocket++ library that connects to the BitMEX realtime feed...
//     std::string uri = "wss://www.bitmex.com/realtime";
//     client c;
//     c.clear_access_channels(websocketpp::log::alevel::frame_payload);
// #if 0
//     c.set_access_channels(websocketpp::log::alevel::all);
//     c.set_error_channels(websocketpp::log::elevel::all);
// #endif
//     c.init_asio();
//     c.set_tls_init_handler(&on_tls_init);
//     c.set_open_handler(bind(&on_open, &c, &bmxClient, ::_1));
//     c.set_message_handler(bind(&on_message, &bmxClient, ::_1, ::_2));

//     websocketpp::lib::error_code ec;
//     client::connection_ptr con = c.get_connection(uri, ec);
//     if (ec) {
//         std::cout << "Failed to create connection: " << ec.message() << std::endl;
//         return 1;
//     }
//     c.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

//     c.connect(con);

//     c.run();
// }
