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
    LimitNode* lowestSellLimitNode;
    LimitNode* highestBuyLimitNode; 

    std::string currencyPairSymbol;
    long marketUpdateExchangeRxTimestamp;
    system_clock::time_point finalUpdateTimestamp;
    std::vector<size_t> memoryUsages;  // Store memory usage values for average memory usage calculation

    size_t buyNodeCount;
    size_t sellNodeCount;

    void transplant(LimitNode* u, LimitNode* v, OrderBookSide orderBookSide);
    void insertLimitNode(LimitNode* newNode, LimitNode* currentNode, LimitNode* parentLimitNodeNode, ParentRelation parentLimitNodeRelation);
    void removeLimitNode(LimitNode* node, OrderBookSide orderBookSide);
    LimitNode* minPriceLimitNode(LimitNode* node);
    LimitNode* maxPriceLimitNode(LimitNode* node);
    
    void postorderTraversal(LimitNode* root);
    size_t calculateMemoryUsage() const;
    size_t calculateTreeMemoryUsage(LimitNode* root) const;
    double calculateAverageMemoryUsage() const;
    size_t calculateMapMemoryUsage(const std::unordered_map<double, LimitNode*>& idMap) const; 

public:
    OrderBook(std::string currencyPairSymbol) : buyRootNode(nullptr), sellRootNode(nullptr), lowestSellLimitNode(nullptr), highestBuyLimitNode(nullptr), currencyPairSymbol(currencyPairSymbol), buyNodeCount(0), sellNodeCount(0) {}
    OrderBook() : buyRootNode(nullptr), sellRootNode(nullptr), lowestSellLimitNode(nullptr), highestBuyLimitNode(nullptr), currencyPairSymbol(""), buyNodeCount(0), sellNodeCount(0) {}

    // Buy side functions
    void insertBuy(double id, double price, double size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void updateBuy(double id, double size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void removeBuy(double id, long timestamp, system_clock::time_point updateReceiveTimestamp);
    // Sell side functions
    void insertSell(double id, double price, double size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void updateSell(double id, double size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void removeSell(double id, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp);

    std::pair<double, double> getBestBuyLimitPriceAndSize();
    std::pair<double, double> getBestSellLimitPriceAndSize(); 
    bool checkBuySidePriceLevel(double price);
    bool checkSellSidePriceLevel(double price);

    std::string getCurrencyPairSymbol() const {
        return this->currencyPairSymbol;
    }

    long getMarketUpdateExchangeTxTimestamp() {
        return this->marketUpdateExchangeRxTimestamp;
    }

    system_clock::time_point getFinalUpdateTimestamp() {
        return this->finalUpdateTimestamp;
    }

    void printOrderBook();
    void updateOrderBookMemoryUsage();
};

#endif // ORDER_BOOK_HPP
