# PublicHFT: An Open-Source HFT for Triangular Arbitrage Opportunities on Centralized Cryptocurrency Exchanges 

## About
BEng Project - Development of a High-Frequency Trading System for Triangular Arbitrage on Centralized Cryptocurrency Exchanges

## Overview
PublicHFT is an open-source high-frequency trading (HFT) system designed to democratize access to HFT strategies. It enables retail investors to capitalize on triangular arbitrage opportunities on centralized cryptocurrency exchanges such as Kraken and Bitmex. The trading system continuously operates by receiving market updates from the selected exchange, reconstructing the limit order books, identifying triangular arbitrage opportunities, and asynchronously executing batches of three orders for the detected opportunities.

To maintain high performance, PublicHFT avoids any context switching once it starts receiving order book updates. It employs `io_uring` with submission queue polling for reading market update packets and asynchronously sending the three orders required for a triangular arbitrage opportunity, thereby eliminating system calls for network I/O operations. This approach is achieved without the need for custom hardware, making PublicHFT highly accessible and easily runnable on a standard Linux machine with at least six physical cores.

The project extends the Bellman-Ford algorithm to enable real-time detection of currency conversion sequences and market orders corresponding to triangular arbitrage opportunities. This developed algorithm illustrates the trade-off between identifying more profitable opportunities by processing a larger portfolio of order books and reacting quickly enough to avoid missing an opportunity. Furthermore, a controllable mock exchange was developed to facilitate rigorous end-to-end testing of the trading system, ensuring reproducible experimental results and enabling further optimizations.

Our experiments demonstrate that PublicHFT achieves sub-millisecond tick-to-trade latency for various optimized portfolios, processing 3, 50, 92, and 122 order books. With success rates of 5.51% and 11.6% for executed triangular arbitrage orders on Bitmex and Kraken, respectively, the experiments also show that PublicHFT can successfully profit from at least one triangular arbitrage opportunity on centralized cryptocurrency exchanges.

## Dependencies

The project depends on the following libraries, make sure you have them installed on your system:

- **liburing** (version: `2.6`)
- **pthread**
- **crypto**
- **ssl**
- **websockets** (libwebsockets version: `4.3.99-v4.3.0-368-g1e0953ff`)
- **ev** (libev version: `4.31`)
- **rapidjson** (version: `1.1`)

## Getting Started
Follow these instructions to get PublicHFT up and running on your local Linux machine.

### Installation

1. Clone the repository:

    ```bash
    git clone https://github.com/yourusername/PublicHFT.git
    ```

2. Navigate to the project directory:

    ```bash
    cd PublicHFT
    ```

3. Set up the build environment using the provided build script. You need to specify the portfolio and exchange options:

    ```bash
    ./build_script.sh -p USE_PORTFOLIO_3 -e USE_KRAKEN_EXCHANGE
    ```

    Exchange options:
    - Bitmex: `USE_BITMEX_EXCHANGE`
    - Kraken: `USE_KRAKEN_EXCHANGE`
    - Bitmex Testnet: `USE_BITMEX_TESTNET_EXCHANGE`
    - Kraken-configured mock exchange: `USE_KRAKEN_MOCK_EXCHANGE`
    - Bitmex-configured mock exchange: `USE_BITMEX_MOCK_EXCHANGE`

    The mock exchange for emulating the centralized cryptocurrency exchanges Kraken and Bitmex is found at [Mock CCE Repository](https://github.com/alptugp/mock-cce/tree/main).

    Optimized portfolio options:
    - `USE_PORTFOLIO_122`
    - `USE_PORTFOLIO_92`
    - `USE_PORTFOLIO_50`
    - `USE_PORTFOLIO_3`

4. **Root Privileges for io_uring**: PublicHFT requires root privileges for submission queue polling with `io_uring`. Ensure you run the application with appropriate permissions.

5. For verbose output (optional), use the following flags:

    ```bash
    --verbose-book-builder
    --verbose-strategy
    ```

### Run PublicHFT
After building the project, run the executable to start the trading system. Ensure your configuration matches the desired exchange and portfolio setup.

Use the following command to run the project:

    ```bash
    ./build/main
    ```

By following these steps, you will have PublicHFT running on your local machine.
