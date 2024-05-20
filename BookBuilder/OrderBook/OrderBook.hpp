// order_book.hpp

#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono>


using namespace std::chrono;

struct AVLNode {
    double id;
    double price;
    int size;
    AVLNode* left;
    AVLNode* right;
    int height;

    AVLNode(double id, double price, int size) : id(id), price(price), size(size), left(nullptr), right(nullptr), height(1) {}
};

class OrderBook {
private:
    AVLNode* buyRoot;
    AVLNode* sellRoot;
    std::unordered_map<double, AVLNode*> buyIdMap;
    std::unordered_map<double, AVLNode*> sellIdMap;
    std::string symbol;
    long updateExchangeTimestamp_;
    system_clock::time_point updateReceiveTimestamp_;
    std::vector<size_t> memoryUsages;  // Store memory usage values for average memory usage calculation
    static const int PRINT_INTERVAL = 100;

    int height(AVLNode* node);
    int balanceFactor(AVLNode* node);
    AVLNode* rotateRight(AVLNode* y);
    AVLNode* rotateLeft(AVLNode* x);
    AVLNode* balance(AVLNode* node);
    
    AVLNode* insertHelper(AVLNode* node, double id, double price, int size);
    void updateHelper(AVLNode* node, double price, int size);
    AVLNode* deleteNode(AVLNode* root, double price);
    AVLNode* minValueNode(AVLNode* node);
    AVLNode* maxValueNode(AVLNode* node);
    
    void postorderTraversal(AVLNode* root);
    size_t calculateMemoryUsage() const;
    size_t calculateTreeMemoryUsage(AVLNode* root) const;
    double calculateAverageMemoryUsage() const;
    size_t calculateMapMemoryUsage(const std::unordered_map<double, AVLNode*>& idMap) const; 

public:
    OrderBook(std::string symbol) : buyRoot(nullptr), sellRoot(nullptr), symbol(symbol) {}
    OrderBook() : buyRoot(nullptr), sellRoot(nullptr), symbol("") {}

    void printOrderBook();
    std::string getSymbol() const {
        return this->symbol;
    }
    long getUpdateExchangeTimestamp() {
        return this->updateExchangeTimestamp_;
    }
    system_clock::time_point getUpdateReceiveTimestamp() {
        return this->updateReceiveTimestamp_;
    }

    std::pair<double, double> getBestBuyAndSellPrice();

    // Buy side functions
    void insertBuy(double id, double price, int size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void updateBuy(double id, int size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void removeBuy(double id, long timestamp, system_clock::time_point updateReceiveTimestamp);

    // Sell side functions
    void insertSell(double id, double price, int size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void updateSell(double id, int size, long timestamp, system_clock::time_point updateReceiveTimestamp);
    void removeSell(double id, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp);

    bool checkBuyPriceLevel(double price);
    bool checkSellPriceLevel(double price);
   
    void updateOrderBookMemoryUsage();
};

#endif // ORDER_BOOK_HPP
