// order_book.cpp

#include "OrderBook.hpp"


std::pair<double, double> OrderBook::getBestBuyAndSellPrice() {
    // std::cout << (buyRoot != nullptr) << (sellRoot != nullptr) << std::endl;
    double bestBuyPrice = buyRoot != nullptr ? maxValueNode(this->buyRoot)->price : 0.0;
    double bestSellPrice = sellRoot != nullptr ? minValueNode(this->sellRoot)->price : 0.0;

    return std::make_pair(bestBuyPrice, bestSellPrice);
}

int OrderBook::height(AVLNode* node) {
    if (node == nullptr)
        return 0;
    std::cout << "height" << std::endl;
    return node->height;
}

int OrderBook::balanceFactor(AVLNode* node) {
    std::cout << "balanceFactor" << std::endl;
    if (node == nullptr)
        return 0;
    return height(node->left) - height(node->right);
}

AVLNode* OrderBook::insertHelper(AVLNode* node, uint64_t id, double price, int size) {
    // std::cout << "insertHelper" << std::endl;
    if (node == nullptr) {
        AVLNode* newNode = new AVLNode(id, price, size);
        return newNode;
    }

    if (price < node->price)
        node->left = insertHelper(node->left, id, price, size);
    else if (price > node->price)
        node->right = insertHelper(node->right, id, price, size);
    else
        return node;

    // node->height = 1 + std::max(height(node->left), height(node->right));

    // return balance(node);
    return node;
}

void OrderBook::updateHelper(AVLNode* node, double price, int size) {
    // std::cout << "updateHelper" << std::endl;
    if (node == nullptr)
        return;

    if (price < node->price)
        updateHelper(node->left, price, size);
    else if (price > node->price)
        updateHelper(node->right, price, size);
    else {
        node->size = size;
        return; 
    }

    // node->height = 1 + std::max(height(node->left), height(node->right));
    // balance(node);
}

AVLNode* OrderBook::rotateRight(AVLNode* y) {
    // std::cout << "rotateRight" << std::endl;
    AVLNode* x = y->left;
    AVLNode* T2 = x->right;

    x->right = y;
    y->left = T2;

    y->height = std::max(height(y->left), height(y->right)) + 1;
    x->height = std::max(height(x->left), height(x->right)) + 1;

    return x;
}

AVLNode* OrderBook::rotateLeft(AVLNode* x) {
    // std::cout << "rotateLeft" << std::endl;
    AVLNode* y = x->right;
    AVLNode* T2 = y->left;

    y->left = x;
    x->right = T2;

    x->height = std::max(height(x->left), height(x->right)) + 1;
    y->height = std::max(height(y->left), height(y->right)) + 1;

    return y;
}

AVLNode* OrderBook::balance(AVLNode* node) {
    // std::cout << "balance" << std::endl;
    if (node == nullptr)
        return node;

    int bf = balanceFactor(node);

    if (bf > 1) {
        if (balanceFactor(node->left) < 0) {
            node->left = rotateLeft(node->left);
            return rotateRight(node);
        } else {
            return rotateRight(node);
        }
    } else if (bf < -1) {
        if (balanceFactor(node->right) > 0) {
            node->right = rotateRight(node->right);
            return rotateLeft(node);
        } else {
            return rotateLeft(node);
        }
    }

    return node;
}

AVLNode* OrderBook::minValueNode(AVLNode* node) {
    // std::cout << "minValueNode" << std::endl;
    AVLNode* current = node;

    while (current->left != nullptr) 
        current = current->left;

    return current;
}

AVLNode* OrderBook::maxValueNode(AVLNode* node) {
    // std::cout << "maxValueNode" << std::endl;
    AVLNode* current = node;

    while (current->right != nullptr) 
        current = current->right;

    return current;
}

AVLNode* OrderBook::deleteNode(AVLNode* root, double price) {
    // std::cout << "deleteNode" << std::endl;
    if (root == nullptr)
        return root;

    if (price < root->price)
        root->left = deleteNode(root->left, price);
    else if (price > root->price)
        root->right = deleteNode(root->right, price);
    else {
        if (root->left == nullptr || root->right == nullptr) {
            AVLNode* temp = root->left ? root->left : root->right;

            if (temp == nullptr) {
                temp = root;
                root = nullptr;
            } else
                *root = *temp;

            delete temp;
        } else {
            AVLNode* temp = minValueNode(root->right);

            root->id = temp->id;
            root->price = temp->price;
            root->size = temp->size;

            root->right = deleteNode(root->right, temp->price);
        }
    }

    if (root == nullptr)
        return root;

    // root->height = 1 + std::max(height(root->left), height(root->right));

    //return balance(root);
    return root;
}

void OrderBook::insertBuy(uint64_t id, double price, int size, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp) {
    buyRoot = insertHelper(buyRoot, id, price, size);
    this->updateExchangeTimestamp_ = updateExchangeTimestamp;
    this->updateReceiveTimestamp_ = updateReceiveTimestamp;
    buyIdMap[id] = new AVLNode(id, price, size);
}

void OrderBook::updateBuy(uint64_t id, int size, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp) {
    updateHelper(buyRoot, this->buyIdMap[id]->price, size);
    this->updateExchangeTimestamp_ = updateExchangeTimestamp;
    this->updateReceiveTimestamp_ = updateReceiveTimestamp;
    this->buyIdMap[id]->size = size;
}

void OrderBook::removeBuy(uint64_t id, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp) {
    deleteNode(buyRoot, this->buyIdMap[id]->price);
    this->updateExchangeTimestamp_ = updateExchangeTimestamp;
    this->updateReceiveTimestamp_ = updateReceiveTimestamp;
    this->buyIdMap.erase(id);
}

void OrderBook::insertSell(uint64_t id, double price, int size, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp) {
    sellRoot = insertHelper(sellRoot, id, price, size);
    this->updateExchangeTimestamp_ = updateExchangeTimestamp;
    this->updateReceiveTimestamp_ = updateReceiveTimestamp;
    sellIdMap[id] = new AVLNode(id, price, size);
}

void OrderBook::updateSell(uint64_t id, int size, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp) {
    updateHelper(sellRoot, this->sellIdMap[id]->price, size);
    this->updateExchangeTimestamp_ = updateExchangeTimestamp;
    this->updateReceiveTimestamp_ = updateReceiveTimestamp;
    this->sellIdMap[id]->size = size;
}

void OrderBook::removeSell(uint64_t id, long updateExchangeTimestamp, system_clock::time_point updateReceiveTimestamp) {
    deleteNode(sellRoot, this->sellIdMap[id]->price);
    this->updateExchangeTimestamp_ = updateExchangeTimestamp;
    this->updateReceiveTimestamp_ = updateReceiveTimestamp;
    this->sellIdMap.erase(id);
}

void OrderBook::postorderTraversal(AVLNode* root) {
    if (root != nullptr) {
        postorderTraversal(root->left);
        postorderTraversal(root->right);
        std::cout << "ID: " << root->id << ", Price: " << root->price << ", Size: " << root->size << "\n";
    }
}

void OrderBook::printOrderBook() {
    std::cout << symbol << " - Sell Side Order Book:\n";
    postorderTraversal(sellRoot);
    
    std::cout << "------------------------\n";

    std::cout << symbol << " - Buy Side Order Book:\n";
    postorderTraversal(buyRoot);
    
    std::cout << "########################\n";
}

size_t OrderBook::calculateMemoryUsage() const {
    size_t size = 0;

    // Include the size of the OrderBook object itself
    size += sizeof(*this);

    // Include the size of the buy side and sell side trees
    size += calculateTreeMemoryUsage(buyRoot);
    size += calculateTreeMemoryUsage(sellRoot);

    // Add the size of the buyIdMap and sellIdMap
    size += calculateMapMemoryUsage(buyIdMap);
    size += calculateMapMemoryUsage(sellIdMap);

    return size;
}

size_t OrderBook::calculateTreeMemoryUsage(AVLNode* root) const {
    if (root == nullptr) {
        return 0;
    }

    // Calculate the memory usage for the current node
    size_t nodeSize = sizeof(AVLNode);

    // Calculate memory usage for left and right subtrees recursively
    size_t leftSize = calculateTreeMemoryUsage(root->left);
    size_t rightSize = calculateTreeMemoryUsage(root->right);

    // Total memory usage for the current node and its subtrees
    return nodeSize + leftSize + rightSize;
}

size_t OrderBook::calculateMapMemoryUsage(const std::unordered_map<uint64_t, AVLNode*>& idMap) const {
    size_t mapSize = sizeof(idMap); // Size of the map object

    // Iterate over the map and add the size of its elements
    for (const auto& entry : idMap) {
        mapSize += sizeof(entry.first);  // Size of key
        mapSize += sizeof(entry.second); // Size of value
    }

    return mapSize;
}

void OrderBook::updateOrderBookMemoryUsage() {
    // Calculate memory usage and store in the vector
    size_t memoryUsage = calculateMemoryUsage();

    memoryUsages.push_back(memoryUsage);

    // Print average memory usage periodically
    if (memoryUsages.size() % PRINT_INTERVAL == 0) {
        [[maybe_unused]] double averageMemory = calculateAverageMemoryUsage();
        std::cout << "Average Memory Usage: " << averageMemory << " bytes" << std::endl;
    }
}

double OrderBook::calculateAverageMemoryUsage() const {
    if (memoryUsages.empty()) {
        return 0.0;  // Avoid division by zero
    }

    size_t totalMemory = 0;
    for (const auto& memory : memoryUsages) {
        totalMemory += memory;
    }

    return static_cast<double>(totalMemory) / memoryUsages.size();
}


