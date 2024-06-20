// order_book.cpp

#include "OrderBook.hpp"

std::pair<double, double> OrderBook::getBestBuyLimitPriceAndSize() {
    double highestBuyLimitNodePrice = highestBuyLimitNode != nullptr ? highestBuyLimitNode->price : 0.0;
    double highestBuyLimitNodeSize = highestBuyLimitNode != nullptr ? highestBuyLimitNode->size : 0.0;
    return std::make_pair(highestBuyLimitNodePrice, highestBuyLimitNodeSize); 
}

std::pair<double, double> OrderBook::getBestSellLimitPriceAndSize() {
    double lowestSellLimitNodePrice = lowestSellLimitNode != nullptr ? lowestSellLimitNode->price : 0.0;
    double lowestSellLimitNodeSize = lowestSellLimitNode != nullptr ? lowestSellLimitNode->size : 0.0;
    return std::make_pair(lowestSellLimitNodePrice, lowestSellLimitNodeSize); 
}

void OrderBook::insertLimitNode(LimitNode* newNode, LimitNode* currentNode, LimitNode* parentNode, ParentRelation parentRelation) {
    if (currentNode == nullptr) {
        (parentRelation == ParentRelation::Left) ? parentNode->leftLimitNode = newNode : parentNode->rightLimitNode = newNode;
        newNode->parentLimitNode = parentNode;
        return;
    }

    if (newNode->price < currentNode->price)
        insertLimitNode(newNode, currentNode->leftLimitNode, currentNode, ParentRelation::Left);
    else if (newNode->price > currentNode->price)
        insertLimitNode(newNode, currentNode->rightLimitNode, currentNode, ParentRelation::Right);        
    else {
        std::cerr << "Error: Price level already exists for price " << newNode->price << std::endl;
        return;
    }
}

LimitNode* OrderBook::minPriceLimitNode(LimitNode* node) {
    LimitNode* current = node;

    while (current->leftLimitNode != nullptr) 
        current = current->leftLimitNode;

    return current;
}

LimitNode* OrderBook::maxPriceLimitNode(LimitNode* node) {
    LimitNode* current = node;

    while (current->rightLimitNode != nullptr) 
        current = current->rightLimitNode;

    return current;
}

// Transplant function replaces subtree rooted at u with subtree rooted at v
void OrderBook::transplant(LimitNode* u, LimitNode* v, OrderBookSide orderBookSide) {
    if (u->parentLimitNode == nullptr) {
        (orderBookSide == OrderBookSide::Buy) ? buyRootNode = v : sellRootNode = v;
    } else if (u == u->parentLimitNode->leftLimitNode) {
        u->parentLimitNode->leftLimitNode = v;
    } else {
        u->parentLimitNode->rightLimitNode = v;
    }
    if (v != nullptr) {
        v->parentLimitNode = u->parentLimitNode;
    }
}

// Remove function
void OrderBook::removeLimitNode(LimitNode* node, OrderBookSide orderBookSide) {
    if (node->leftLimitNode == nullptr) {
        transplant(node, node->rightLimitNode, orderBookSide);
    } else if (node->rightLimitNode == nullptr) {
        transplant(node, node->leftLimitNode, orderBookSide);
    } else {
        LimitNode* successor = minPriceLimitNode(node->rightLimitNode);
        if (successor->parentLimitNode != node) {
            transplant(successor, successor->rightLimitNode, orderBookSide);
            successor->rightLimitNode = node->rightLimitNode;
            successor->rightLimitNode->parentLimitNode = successor;
        }
        transplant(node, successor, orderBookSide);
        successor->leftLimitNode = node->leftLimitNode;
        successor->leftLimitNode->parentLimitNode = successor;
    }
    delete node;
}

void OrderBook::insertBuy(double id, double price, double size, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp) {
    LimitNode* newBuyNode = new LimitNode(id, price, size);
    if (buyRootNode == nullptr)
        buyRootNode = newBuyNode;
    else if (price < buyRootNode->price)
        insertLimitNode(newBuyNode, buyRootNode->leftLimitNode, buyRootNode, ParentRelation::Left);
    else if (price > buyRootNode->price)
        insertLimitNode(newBuyNode, buyRootNode->rightLimitNode, buyRootNode, ParentRelation::Right);    

    buyMap[id] = newBuyNode; 
    this->marketUpdateExchangeRxTimestamp = updateExchangeTimestamp;
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    buyNodeCount++;
#endif
    if (highestBuyLimitNode == nullptr || price > highestBuyLimitNode->price)
        highestBuyLimitNode = newBuyNode;
    
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    if (buyNodeCount > 10) {
        LimitNode* nodeToRemove = minPriceLimitNode(buyRootNode);
        this->buyMap.erase(nodeToRemove->price);
        removeLimitNode(nodeToRemove, OrderBookSide::Buy);
        buyNodeCount--;
    }
#endif
    this->finalUpdateTimestamp = high_resolution_clock::now();
    this->updateSocketRxTimestamp = updateSocketRxTimestamp;
}

void OrderBook::updateBuy(double id, double size, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp) {
    this->buyMap[id]->size = size;
    this->marketUpdateExchangeRxTimestamp = updateExchangeTimestamp;
    this->finalUpdateTimestamp = high_resolution_clock::now();
    this->updateSocketRxTimestamp = updateSocketRxTimestamp;
}

void OrderBook::removeBuy(double id, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp) {
    LimitNode* nodeToRemove = buyMap[id];
    removeLimitNode(nodeToRemove, OrderBookSide::Buy);
    this->buyMap.erase(id);
    this->marketUpdateExchangeRxTimestamp = updateExchangeTimestamp;
    
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    buyNodeCount--;
#endif
    
    if (nodeToRemove == highestBuyLimitNode) {
        if (highestBuyLimitNode->parentLimitNode == nullptr) {
            highestBuyLimitNode = maxPriceLimitNode(highestBuyLimitNode->leftLimitNode);
        } else if (highestBuyLimitNode->parentLimitNode->leftLimitNode == nullptr) {
            highestBuyLimitNode = highestBuyLimitNode->parentLimitNode;
        } else {
            highestBuyLimitNode = maxPriceLimitNode(highestBuyLimitNode->parentLimitNode->leftLimitNode);
        }
    }

    this->finalUpdateTimestamp = high_resolution_clock::now();
    this->updateSocketRxTimestamp = updateSocketRxTimestamp; 
}

void OrderBook::insertSell(double id, double price, double size, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp) {
    LimitNode* newSellLimitNode = new LimitNode(id, price, size);
    if (sellRootNode == nullptr)
        sellRootNode = newSellLimitNode;
    else if (price < sellRootNode->price)
        insertLimitNode(newSellLimitNode, sellRootNode->leftLimitNode, sellRootNode, ParentRelation::Left);
    else if (price > sellRootNode->price)
        insertLimitNode(newSellLimitNode, sellRootNode->rightLimitNode, sellRootNode, ParentRelation::Right);    

    sellMap[id] = newSellLimitNode; 
    this->marketUpdateExchangeRxTimestamp = updateExchangeTimestamp;
    
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    sellNodeCount++;
#endif

    if (lowestSellLimitNode == nullptr || price < lowestSellLimitNode->price)
        lowestSellLimitNode = newSellLimitNode;

#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    if (sellNodeCount > 10) {
        LimitNode* nodeToRemove = maxPriceLimitNode(sellRootNode);
        this->sellMap.erase(nodeToRemove->price);
        removeLimitNode(nodeToRemove, OrderBookSide::Sell);
        sellNodeCount--;
    }
#endif
    this->finalUpdateTimestamp = high_resolution_clock::now();
    this->updateSocketRxTimestamp = updateSocketRxTimestamp; 
}

void OrderBook::updateSell(double id, double size, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp) {
    this->sellMap[id]->size = size;
    this->marketUpdateExchangeRxTimestamp = updateExchangeTimestamp;
    this->finalUpdateTimestamp = high_resolution_clock::now();
}

void OrderBook::removeSell(double id, long updateExchangeTimestamp, system_clock::time_point updateSocketRxTimestamp) {
    LimitNode* nodeToRemove = sellMap[id];
    removeLimitNode(nodeToRemove, OrderBookSide::Sell);
    this->sellMap.erase(id);
    this->marketUpdateExchangeRxTimestamp = updateExchangeTimestamp;
    
#if defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)    
    sellNodeCount--;
#endif

    if (nodeToRemove == lowestSellLimitNode) {
        if (lowestSellLimitNode->parentLimitNode == nullptr) {
            lowestSellLimitNode = minPriceLimitNode(lowestSellLimitNode->rightLimitNode);
        } else if (lowestSellLimitNode->parentLimitNode->rightLimitNode == nullptr)
            lowestSellLimitNode = lowestSellLimitNode->parentLimitNode;
        else {
            lowestSellLimitNode = minPriceLimitNode(lowestSellLimitNode->parentLimitNode->rightLimitNode);
        }
    } 

    this->finalUpdateTimestamp = high_resolution_clock::now(); 
    this->updateSocketRxTimestamp = updateSocketRxTimestamp;
}

bool OrderBook::checkBuySidePriceLevel(double price) {
    return this->buyMap.count(price) != 0;
}

bool OrderBook::checkSellSidePriceLevel(double price) {
    return this->sellMap.count(price) != 0;
}

void OrderBook::reverseInOrderTraversal(LimitNode* node) {
    if (node != nullptr) {
        reverseInOrderTraversal(node->rightLimitNode);
        std::cout << "Price: " << node->price << ", Size: " << node->size << "\n";
        reverseInOrderTraversal(node->leftLimitNode);
    }
}

void OrderBook::printOrderBook() {
    std::cout << currencyPairSymbol << " - Sell Side of the LOB for " << currencyPairSymbol << ":\n";
    reverseInOrderTraversal(sellRootNode);
    std::cout << "------------------------\n";
    std::cout << currencyPairSymbol << " - Buy Side of the LOB for " << currencyPairSymbol << ":\n";
    reverseInOrderTraversal(buyRootNode);
    std::cout << "########################\n";
}

