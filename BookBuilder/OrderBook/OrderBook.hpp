// order_book.hpp

#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono> 

struct AVLNode {
    uint64_t id;
    double price;
    int size;
    AVLNode* left;
    AVLNode* right;
    int height;

    AVLNode(uint64_t id, double price, int size) : id(id), price(price), size(size), left(nullptr), right(nullptr), height(1) {}
};

class OrderBook {
private:
    AVLNode* buyRoot;
    AVLNode* sellRoot;
    std::unordered_map<uint64_t, AVLNode*> buyIdMap;
    std::unordered_map<uint64_t, AVLNode*> sellIdMap;
    std::string symbol;
    std::vector<size_t> memoryUsages;  // Store memory usage values for average memory usage calculation
    static const int PRINT_INTERVAL = 100;

    int height(AVLNode* node);
    int balanceFactor(AVLNode* node);
    AVLNode* rotateRight(AVLNode* y);
    AVLNode* rotateLeft(AVLNode* x);
    AVLNode* balance(AVLNode* node);
    AVLNode* insertHelper(AVLNode* node, uint64_t id, double price, int size, std::unordered_map<uint64_t, AVLNode*>& idMap);
    void updateHelper(AVLNode* node, uint64_t id, int size, std::unordered_map<uint64_t, AVLNode*>& idMap);
    void removeHelper(AVLNode* root, uint64_t id, std::unordered_map<uint64_t, AVLNode*>& idMap);

    AVLNode* deleteNode(AVLNode* root, uint64_t id, std::unordered_map<uint64_t, AVLNode*>& idMap);
    AVLNode* minValueNode(AVLNode* node);
    
    void postorderTraversal(AVLNode* root);
    size_t calculateMemoryUsage() const;
    size_t calculateTreeMemoryUsage(AVLNode* root) const;
    double calculateAverageMemoryUsage() const;
    size_t calculateMapMemoryUsage(const std::unordered_map<uint64_t, AVLNode*>& idMap) const; 

public:
    OrderBook(std::string symbol) : buyRoot(nullptr), sellRoot(nullptr), symbol(symbol) {}
    OrderBook() : buyRoot(nullptr), sellRoot(nullptr), symbol("") {}

    void printOrderBook();
    std::string getSymbol() const {
        return this->symbol;
    }
    std::pair<double, double> getBestBuyAndSellPrice();

    // Buy side functions
    void insertBuy(uint64_t id, double price, int size);
    void updateBuy(uint64_t id, int size);
    void removeBuy(uint64_t id);

    // Sell side functions
    void insertSell(uint64_t id, double price, int size);
    void updateSell(uint64_t id, int size);
    void removeSell(uint64_t id);

    void updateOrderBookMemoryUsage();
};

#endif // ORDER_BOOK_HPP
