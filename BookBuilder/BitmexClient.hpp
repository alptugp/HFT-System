#pragma once

#include <rapidjson/document.h>

#include <functional>
#include <optional>
#include <string>

/// See the following documentation for more details:
///
/// https://www.bitmex.com/app/wsAPI
namespace BitmexClient::websocket {

/// BitMEX subscription topic.
enum class Topic : uint8_t {
    Announcement,
    Chat,
    Connected,
    Funding,
    Instrument,
    Insurance,
    Liquidation,
    OrderBookL2_25,
    OrderBookL2,
    OrderBook10,
    PublicNotifications,
    Quote,
    QuoteBin1m,
    QuoteBin5m,
    QuoteBin1h,
    QuoteBin1d,
    Settlement,
    Trade,
    TradeBin1m,
    TradeBin5m,
    TradeBin1h,
    TradeBin1d,
};

inline const std::string to_string(Topic topic)
{
    switch (topic) {
    case Topic::Announcement:
        return "announcement";
    case Topic::Chat:
        return "chat";
    case Topic::Connected:
        return "connected";
    case Topic::Funding:
        return "funding";
    case Topic::Instrument:
        return "instrument";
    case Topic::Insurance:
        return "insurance";
    case Topic::Liquidation:
        return "liquidation";
    case Topic::OrderBookL2_25:
        return "orderBookL2_25";
    case Topic::OrderBookL2:
        return "orderBookL2";
    case Topic::OrderBook10:
        return "orderBook10";
    case Topic::PublicNotifications:
        return "publicNotifications";
    case Topic::Quote:
        return "quote";
    case Topic::QuoteBin1m:
        return "quoteBin1m";
    case Topic::QuoteBin5m:
        return "quoteBin5m";
    case Topic::QuoteBin1h:
        return "quoteBin1h";
    case Topic::QuoteBin1d:
        return "quoteBin1d";
    case Topic::Settlement:
        return "settlement";
    case Topic::Trade:
        return "trade";
    case Topic::TradeBin1m:
        return "tradeBin1m";
    case Topic::TradeBin5m:
        return "tradeBin5m";
    case Topic::TradeBin1h:
        return "tradeBin1h";
    case Topic::TradeBin1d:
        return "tradeBin1d";
    }
    __builtin_unreachable();
}

/// Callback function that is invoked when a trade is reported.
using OnTradeCallback = std::function<void(const char* symbol, const char* action, uint64_t id, const char* side, int size, double price, const char* timestamp)>;
/// BitMEX WebSocket API client.
///
/// This class provides an interface for interacting with the BitMEX
/// WebSocket API. The class does not provide WebSocket (the low level
/// protocol) connectivity, but expects users of this class to provide that
/// using, for example, the websocketpp library.
class Client {
    OnTradeCallback _on_trade;

public:
    ThroughputMonitor dataThroughputMonitor = ThroughputMonitor("Data Throughput Monitor", std::chrono::high_resolution_clock::now()); 

    /// Set the callback function that is invoked when a new trade is reported.
    void on_trade(OnTradeCallback on_trade)
    {
        _on_trade = on_trade;
    }

    /// Make a subscription request message for a given instrument and topic.
    std::string make_subscribe(const std::string& instrument, Topic topic) const
    {
        return "{\"op\":\"subscribe\",\"args\":[\"" + to_string(topic) + ":" + instrument + "\"]}";
    }

    /// Parse a message and invoke the relevant callbacks.
    void parse_msg(const std::string& msg) const
    {   
        // std::cout << msg.size() << std::endl;
        rapidjson::Document doc;
        doc.Parse(msg.c_str());
        if (doc.HasParseError()) {
            return;
        }
        auto table = doc.FindMember("table");
        if (table == doc.MemberEnd() || !table->value.IsString()) {
            return;
        }
        auto data = doc.FindMember("data");
        if (data == doc.MemberEnd() || !data->value.IsArray()) {
            return;
        }
        auto action = doc.FindMember("action");
        if (action == doc.MemberEnd() || !action->value.IsString()) {
            return;
        }

        std::optional<const char*> actionType = action->value.GetString();
        for (rapidjson::SizeType i = 0; i < data->value.Size(); i++) {
            auto symbol = maybe_string(data->value[i], "symbol");
            if (!symbol) {
                continue;
            }
            auto id = maybe_int64(data->value[i], "id");
            if (!id) {
                continue;
            }
            auto side = maybe_string(data->value[i], "side");
            if (!side) {
                continue;
            }

            auto size = maybe_int(data->value[i], "size");
            auto price = maybe_double(data->value[i], "price");
            auto timestamp = maybe_string(data->value[i], "timestamp");
            
            _on_trade(*symbol, *actionType, *id, *side, *size, *price, *timestamp);
        }
    }

private:
    std::optional<const char*> maybe_string(const rapidjson::Value& parent, const char* name) const
    {
        auto child = parent.FindMember(name);
        if (child == parent.MemberEnd() || !child->value.IsString()) {
            return std::nullopt;
        }
        return child->value.GetString();
    }
    std::optional<int> maybe_int(const rapidjson::Value& parent, const char* name) const
    {
        auto child = parent.FindMember(name);
        if (child == parent.MemberEnd() || !child->value.IsInt()) {
            return std::nullopt;
        }
        return child->value.GetInt();
    }
    std::optional<uint64_t> maybe_int64(const rapidjson::Value& parent, const char* name) const
    {
        auto child = parent.FindMember(name);
        if (child == parent.MemberEnd() || !child->value.IsInt64()) {
            return std::nullopt;
        }
        return child->value.GetInt64();
    }
    std::optional<double> maybe_double(const rapidjson::Value& parent, const char* name) const
    {
        auto child = parent.FindMember(name);
        if (child == parent.MemberEnd() || !child->value.IsDouble()) {
            return std::nullopt;
        }
        return child->value.GetDouble();
    }
};
}
