#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "SPSCQueue/SPSCQueue.hpp"
#include "OrderBook/OrderBook.hpp"
#include "BookBuilder/BookBuilderComponent.cpp"
#include "BookBuilder/BookBuilderGateway.cpp"
#include "Utils/Utils.hpp"
#include "StrategyComponent/Strategy.hpp"
#include "OrderManager/OrderManager.hpp"

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
static const std::vector<std::string> currencyPairs = {"XBTUSDT", "XBTETH", "ETHUSDT"};
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
  #if defined(USE_PORTFOLIO_122)
    static const std::vector<std::string> currencyPairs = {
      "KSM/EUR", "KSM/BTC", "KSM/DOT", "KSM/GBP", "KSM/ETH", "KSM/USD", "GBP/USD", "BTC/CAD", "BTC/EUR",
      "BTC/AUD", "BTC/JPY", "BTC/GBP", "BTC/CHF", "BTC/USDT", "BTC/USD", "BTC/USDC", "LTC/EUR", "LTC/BTC",
      "LTC/AUD", "LTC/JPY", "LTC/GBP", "LTC/ETH", "LTC/USDT", "LTC/USD", "SOL/EUR", "SOL/BTC", "SOL/GBP",
      "SOL/ETH", "SOL/USDT", "SOL/USD", "DOT/EUR", "DOT/BTC", "DOT/JPY", "DOT/GBP", "DOT/ETH", "DOT/USDT",
      "DOT/USD", "ETH/CAD", "ETH/EUR", "ETH/BTC", "ETH/AUD", "ETH/JPY", "ETH/GBP", "ETH/CHF", "ETH/USDT",
      "ETH/USD", "ETH/USDC", "LINK/EUR", "LINK/BTC", "LINK/AUD", "LINK/JPY", "LINK/GBP", "LINK/ETH",
      "LINK/USDT", "LINK/USD", "USDC/CAD", "USDC/EUR", "USDC/AUD", "USDC/GBP", "USDC/CHF", "USDC/USDT",
      "USDC/USD", "ADA/EUR", "ADA/BTC", "ADA/AUD", "ADA/GBP", "ADA/ETH", "ADA/USDT", "ADA/USD", "ATOM/EUR",
      "ATOM/BTC", "ATOM/GBP", "ATOM/ETH", "ATOM/USDT", "ATOM/USD", "USDT/EUR", "USDT/AUD", "USDT/JPY",
      "USDT/GBP", "USDT/CHF", "USDT/USD", "USDT/CAD", "AUD/JPY", "AUD/USD", "XRP/CAD", "XRP/EUR", "XRP/BTC",
      "XRP/AUD", "XRP/GBP", "XRP/ETH", "XRP/USDT", "XRP/USD", "EUR/CAD", "EUR/AUD", "EUR/JPY", "EUR/GBP",
      "EUR/CHF", "EUR/USD", "BCH/EUR", "BCH/BTC", "BCH/AUD", "BCH/JPY", "BCH/GBP", "BCH/ETH", "BCH/USDT",
      "BCH/USD", "USD/CHF", "USD/JPY", "USD/CAD", "ALGO/EUR", "ALGO/BTC", "ALGO/GBP", "ALGO/ETH", "ALGO/USDT",
      "ALGO/USD"
    };
  #elif defined(USE_PORTFOLIO_92)
    static const std::vector<std::string> currencyPairs = {
      "BCH/USD", "BCH/BTC", "BCH/EUR", "BCH/AUD", "BCH/GBP", "BCH/ETH", "BCH/USDT", "BCH/JPY", "BTC/USD",
      "BTC/EUR", "BTC/USDC", "BTC/AUD", "BTC/GBP", "BTC/CAD", "BTC/USDT", "BTC/JPY", "USD/CAD", "USD/JPY",
      "XRP/USD", "XRP/BTC", "XRP/EUR", "XRP/AUD", "XRP/GBP", "XRP/ETH", "XRP/CAD", "XRP/USDT", "EUR/USD",
      "EUR/AUD", "EUR/GBP", "EUR/CAD", "EUR/JPY", "LTC/USD", "LTC/EUR", "LTC/BTC", "LTC/AUD", "LTC/GBP",
      "LTC/ETH", "LTC/USDT", "LTC/JPY", "ETH/USD", "ETH/EUR", "ETH/BTC", "ETH/USDC", "ETH/AUD", "ETH/GBP",
      "ETH/CAD", "ETH/USDT", "ETH/JPY", "LINK/USD", "LINK/BTC", "LINK/EUR", "LINK/AUD", "LINK/GBP", "LINK/ETH",
      "LINK/USDT", "LINK/JPY", "ADA/USD", "ADA/BTC", "ADA/EUR", "ADA/AUD", "ADA/GBP", "ADA/ETH", "ADA/USDT",
      "USDC/USD", "USDC/EUR", "USDC/AUD", "USDC/GBP", "USDC/CAD", "USDC/USDT", "GBP/USD", "DOT/USD", "DOT/BTC",
      "DOT/EUR", "DOT/GBP", "DOT/ETH", "DOT/USDT", "DOT/JPY", "USDT/USD", "USDT/EUR", "USDT/AUD", "USDT/GBP",
      "USDT/CAD", "USDT/JPY", "AUD/USD", "AUD/JPY"
    };
  #elif defined(USE_PORTFOLIO_50)
    static const std::vector<std::string> currencyPairs = {
      "BCH/JPY", "BCH/ETH", "BCH/GBP", "BCH/AUD", "BCH/BTC", "BCH/USDT", "BCH/EUR", "BCH/USD", "USDT/JPY",
      "USDT/GBP", "USDT/AUD", "USDT/EUR", "USDT/USD", "BTC/JPY", "BTC/GBP", "BTC/AUD", "BTC/USDT", "BTC/EUR",
      "BTC/USD", "EUR/GBP", "EUR/JPY", "EUR/AUD", "EUR/USD", "ETH/JPY", "ETH/EUR", "ETH/AUD", "ETH/BTC",
      "ETH/USDT", "ETH/GBP", "ETH/USD", "USD/JPY", "LINK/JPY", "LINK/ETH", "LINK/EUR", "LINK/AUD", "LINK/BTC",
      "LINK/USDT", "LINK/GBP", "LINK/USD", "LTC/JPY", "LTC/ETH", "LTC/GBP", "LTC/AUD", "LTC/BTC", "LTC/USDT",
      "LTC/EUR", "LTC/USD", "GBP/USD", "AUD/JPY", "AUD/USD"
    };
  #elif defined(USE_PORTFOLIO_3)
    static const std::vector<std::string> currencyPairs = {"USDT/USD", "SOL/USDT", "SOL/USD"};
  #endif
#endif

int main(int argc, char *argv[]) {
    const size_t queueSize = 10000;

    SPSCQueue<BookBuilderGatewayToComponentQueueEntry> bookBuilderGatewayToComponentQueue(queueSize);
    SPSCQueue<OrderBook> builderToStrategyQueue(queueSize);
    SPSCQueue<StrategyComponentToOrderManagerQueueEntry> strategyToOrderManagerQueue(queueSize);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 1;
    }

    int bookBuilderPipeEnd = pipefd[0];
    int orderManagerPipeEnd = pipefd[1];

    auto strategyThread = std::thread([&builderToStrategyQueue, &strategyToOrderManagerQueue] {
        strategy(builderToStrategyQueue, strategyToOrderManagerQueue);
    });

    auto bookBuilderGatewayThread = std::thread([&bookBuilderGatewayToComponentQueue, orderManagerPipeEnd, currencyPairs = currencyPairs] {
        bookBuilderGateway(bookBuilderGatewayToComponentQueue, currencyPairs, orderManagerPipeEnd);
    });

    auto bookBuilderComponentThread = std::thread([&bookBuilderGatewayToComponentQueue, &builderToStrategyQueue, currencyPairs = currencyPairs] {
        bookBuilderComponent(bookBuilderGatewayToComponentQueue, builderToStrategyQueue, currencyPairs);
    });

    auto orderManagerThread = std::thread([&strategyToOrderManagerQueue, bookBuilderPipeEnd] {
        orderManager(strategyToOrderManagerQueue, bookBuilderPipeEnd);
    });

    bookBuilderGatewayThread.join();
    bookBuilderComponentThread.join();
    strategyThread.join();
    orderManagerThread.join();

    return 0;
}
