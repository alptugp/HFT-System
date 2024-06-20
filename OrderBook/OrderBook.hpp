// order_book.hpp

#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono>

#define PRINT_INTERVAL 100
using namespace std::chrono;

struct LimitNode {
    double id;
    double price;
    double size;
    LimitNode* parentLimitNode;
    LimitNode* leftLimitNode;
    LimitNode* rightLimitNode;

    LimitNode(double id, double price, double size) : id(id), price(price), size(size), parentLimitNode(nullptr), leftLimitNode(nullptr), rightLimitNode(nullptr) {}
};

enum class ParentRelation {
    Left,
    Right
};

enum class OrderBookSide {
    Buy,
    Sell
};

class OrderBook {
private:
    LimitNode* buyRootNode;
    LimitNode* sellRootNode;
    std::unordered_map<double, LimitNode*> buyMap;
    std::unordered_map<double, LimitNode*> sellMap;
    LimitNode* lowestSellLimitNode; // Best sell price
    LimitNode* highestBuyLimitNode; // Best buy price
    
    std::string currencyPairSymbol;
    long marketUpdateExchangeRxTimestamp;
    system_clock::time_point finalUpdateTimestamp;
    system_clock::time_point updateSocketRxTimestamp;

#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    size_t buyNodeCount;
    size_t sellNodeCount;
    LimitNode* highestSelllLimitNode;
    LimitNode* lowestBuyLimitNode;
#endif
    void transplant(LimitNode* u, LimitNode* v, OrderBookSide orderBookSide);
    void insertLimitNode(LimitNode* newNode, LimitNode* currentNode, LimitNode* parentLimitNodeNode, ParentRelation parentLimitNodeRelation);
    void removeLimitNode(LimitNode* node, OrderBookSide orderBookSide);
    LimitNode* minPriceLimitNode(LimitNode* node);
    LimitNode* maxPriceLimitNode(LimitNode* node);
    
    void reverseInOrderTraversal(LimitNode* node);

public:
#if defined(USE_KRAKEN_EXCHANGE) || (USE_KRAKEN_MOCK_EXCHANGE)    
    OrderBook(std::string currencyPairSymbol) : buyRootNode(nullptr), sellRootNode(nullptr), lowestSellLimitNode(nullptr), highestBuyLimitNode(nullptr), currencyPairSymbol(currencyPairSymbol), buyNodeCount(0), sellNodeCount(0) {}
    OrderBook() : buyRootNode(nullptr), sellRootNode(nullptr), lowestSellLimitNode(nullptr), highestBuyLimitNode(nullptr), currencyPairSymbol(""), buyNodeCount(0), sellNodeCount(0) {}
#else
    OrderBook(std::string currencyPairSymbol) : buyRootNode(nullptr), sellRootNode(nullptr), lowestSellLimitNode(nullptr), highestBuyLimitNode(nullptr), currencyPairSymbol(currencyPairSymbol) {}
    OrderBook() : buyRootNode(nullptr), sellRootNode(nullptr), lowestSellLimitNode(nullptr), highestBuyLimitNode(nullptr), currencyPairSymbol("") {}
#endif
    // Buy side functions
    void insertBuy(double id, double price, double size, long timestamp, system_clock::time_point updateSocketRxTimestamp);
    void updateBuy(double id, double size, long timestamp, system_clock::time_point updateSocketRxTimestamp);
    void removeBuy(double id, long timestamp, system_clock::time_point updateSocketRxTimestamp);
    // Sell side functions
    void insertSell(double id, double price, double size, long timestamp, system_clock::time_point updateSocketRxTimestamp);
    void updateSell(double id, double size, long timestamp, system_clock::time_point updateSocketRxTimestamp);
    void removeSell(double id, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp);

    std::pair<double, double> getBestBuyLimitPriceAndSize();
    std::pair<double, double> getBestSellLimitPriceAndSize(); 
    bool checkBuySidePriceLevel(double price);
    bool checkSellSidePriceLevel(double price);

    std::string getCurrencyPairSymbol() const {
        return this->currencyPairSymbol;
    }

    long getMarketUpdateExchangeTimestamp() {
        return this->marketUpdateExchangeRxTimestamp;
    }

    system_clock::time_point getFinalUpdateTimestamp() {
        return this->finalUpdateTimestamp;
    }

    system_clock::time_point getUpdateSocketRxTimestamp() {
        return this->updateSocketRxTimestamp;
    }

    void printOrderBook();
};

#endif // ORDER_BOOK_HPP
