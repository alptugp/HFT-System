#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <bitmex/bitmex.hpp>
#include <string>
#include <chrono>

#include "./order_book/order_book.hpp"
#include "./throughput_monitor/throughput_monitor.hpp"


using client = websocketpp::client<websocketpp::config::asio_tls_client>;
using context_ptr = websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

using Clock = std::chrono::high_resolution_clock;

std::string currency_pair = "XBTUSD";

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


static context_ptr on_tls_init(websocketpp::connection_hdl)
{
    using namespace boost::asio::ssl;
    context_ptr ctx = websocketpp::lib::make_shared<context>(context::sslv23);
    return ctx;
}

static void on_open(client* c, bitmex::websocket::Client* bmx_client, websocketpp::connection_hdl hdl)
{
    // Make a subscription message for "XBTUSD" symbol on the trade topic...
    auto msg = bmx_client->make_subscribe(currency_pair, bitmex::websocket::Topic::OrderBookL2_25);
    /// ...and send it over the WebSocket.
    c->send(hdl, msg, websocketpp::frame::opcode::text);
}

static void on_message(bitmex::websocket::Client* client, websocketpp::connection_hdl, client::message_ptr msg)
{
    // Parse the message received from BitMEX. Note that "client" invokes all the relevant callbacks.
    client->parse_msg(msg->get_payload());
}


int main()
{   
    OrderBook order_book = OrderBook(currency_pair);
    // Create BitMEX C++ API client object.
    bitmex::websocket::Client bmx_client;

    // Create a ThroughputMonitor and pass the start time
    [[maybe_unused]] ThroughputMonitor throughputMonitor(std::chrono::high_resolution_clock::now());

    // Register a callback that is invoked when a trade is reported.
    bmx_client.on_trade([&order_book, &throughputMonitor]([[maybe_unused]] const char* symbol, const char action, uint64_t id, const char* side, int size, double price, const char* timestamp) {
        auto programReceiveTime = Clock::now();
        throughputMonitor.onTradeReceived();
        auto exchangeTimeStamp = convertTimestampToTimePoint(timestamp);
        [[maybe_unused]] auto networkLatency = std::chrono::duration_cast<std::chrono::microseconds>(programReceiveTime - exchangeTimeStamp);

        std::string sideStr(side);
        if (sideStr == "Buy") {
            switch (action) {
                case 'p':
                case 'i':
                    order_book.insertBuy(id, price, size);
                    break;
                case 'u':
                    order_book.updateBuy(id, size);
                    break;
                case 'd':
                    order_book.removeBuy(id);
                    break;
                default:
                    // Handle other message types if needed
                    break;
            }
        } else if (sideStr == "Sell") {
            switch (action) {
                case 'p':
                case 'i':
                    order_book.insertSell(id, price, size);
                    break;
                case 'u':
                    order_book.updateSell(id, size);
                    break;
                case 'd':
                    order_book.removeSell(id);
                    break;
                default:
                    // Handle other message types if needed
                    break;
            }
        }
        auto marketUpdateTime = Clock::now();
        auto bookBuildingDuration = std::chrono::duration_cast<std::chrono::microseconds>(marketUpdateTime - programReceiveTime);
        // std::cout << "Symbol: " << symbol << " - Action: " << action << " - Size: " << size << " - Price: " << price << " (" << side << ")" << " - id: " << id << " - Timestamp: " << timestamp << std::endl;
        // std::cout << "Time taken to receive market update: " << networkLatency.count() << " microseconds" << std::endl;
        // std::cout << "Time taken to process market update: " << bookBuildingDuration.count() << " microseconds" << std::endl;
        // std::cout << "Timestamp: " << timestamp << std::endl;
        // order_book.printOrderBook();
        order_book.updateOrderBookMemoryUsage();
    });

    // Setup WebSocket++ library that connects to the BitMEX realtime feed...
    std::string uri = "wss://www.bitmex.com/realtime";
    client c;
    c.clear_access_channels(websocketpp::log::alevel::frame_payload);
#if 0
    c.set_access_channels(websocketpp::log::alevel::all);
    c.set_error_channels(websocketpp::log::elevel::all);
#endif
    c.init_asio();
    c.set_tls_init_handler(&on_tls_init);
    c.set_open_handler(bind(&on_open, &c, &bmx_client, ::_1));
    c.set_message_handler(bind(&on_message, &bmx_client, ::_1, ::_2));

    websocketpp::lib::error_code ec;
    client::connection_ptr con = c.get_connection(uri, ec);
    if (ec) {
        std::cout << "Failed to create connection: " << ec.message() << std::endl;
        return 1;
    }
    c.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

    c.connect(con);

    c.run();
}
