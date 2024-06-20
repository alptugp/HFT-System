#include "OrderManager.hpp"
#include "OrderManagerUtils.hpp"

#define TX_DEFAULT_BUF_SIZE 128
#define CPU_CORE_INDEX_FOR_ORDER_MANAGER_THREAD 4
#define HEARTBEAT_SENDER_PERIOD_IN_SECONDS 80 
#define NUMBER_OF_IO_URING_SQ_ENTRIES 300
#define REST_API_ADD_ORDER_REQUEST_METHOD "POST"

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
    #define EXCHANGE_HOST_NAME "bitmex.com"
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    #define EXCHANGE_HOST_NAME "api.kraken.com"
#elif defined(USE_BITMEX_TESTNET_EXCHANGE)
    #define EXCHANGE_HOST_NAME "testnet.bitmex.com"
#endif

#if defined(USE_BITMEX_TESTNET_EXCHANGE) || defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
    #define REST_API_ADD_ORDER_REQUEST_URI "/api/v1/order"
    #define API_KEY "63ObNQpYqaCVrjTuBbhgFm2p"
    #define API_SECRET "D2OBzpfW-i6FfgmqGnrhpYqKPrxCvIYnu5KZKsZQW_09XkF-"
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
    #define REST_API_ADD_ORDER_REQUEST_URI "/0/private/AddOrder"
    #define API_KEY "lKfIwMQfaY32k3Wk90+Ia2CRgvw3IjsFQIUPIJmRU+wShkZc0BqAZuCW"
    #define API_SECRET "DC9Na40OX2gf8a1bwbSvCwTKnadc1XcuBnWIkwLBRqbD5xMGEDNQTqoOrAwCAotFqppDQuedI6+/cI3S+5FJdg=="
#endif

using namespace std::chrono;

int sockfds[ARBITRAGE_BATCH_SIZE];
struct OrderManagerClient orderManagerClients[ARBITRAGE_BATCH_SIZE];

static std::ofstream orderManagerDataFile;
static std::ofstream systemDataFile;

// Sends a heartbeat-kind message for each connection each 80 seconds 
void sendPeriodicHeartbeat() {
    for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) { 
        char unencrypted_signature[ARBITRAGE_BATCH_SIZE][512];
        char unencrypted_request[ARBITRAGE_BATCH_SIZE][2048];
    
#if defined(USE_BITMEX_TESTNET_EXCHANGE) || defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
        char *signature;
        char expires[ARBITRAGE_BATCH_SIZE][32];
        time_t now = time(NULL);
        time_t tenSecondsLater = now + 10;
        strftime(expires[i], sizeof(expires[i]), "%s", localtime(&tenSecondsLater));
        snprintf(unencrypted_signature[i], sizeof(unencrypted_signature[i]), "%s%s%s", "GET", "/api/v1/address", expires[i]);
        signature = generateBitmexApiSignature(API_SECRET, strlen(API_SECRET), unencrypted_signature[i], strlen(unencrypted_signature[i]));
        
        sprintf(unencrypted_request[i], "GET /api/v1/address HTTP/1.1\r\n"
                                        "Host: %s\r\n"
                                        "api-key: %s\r\n"
                                        "api-expires: %s\r\n"
                                        "api-signature: %s\r\n"
                                        "Connection: keep-alive\r\n"
                                        "\r\n",
                                        EXCHANGE_HOST_NAME, API_KEY, expires[i], signature);
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
        std::string nonce = generateNonce();
        std::string apiSignature = generateKrakenApiSignature(REST_API_ADD_ORDER_REQUEST_URI, nonce, "", API_SECRET);
        
        sprintf(unencrypted_request[i], "GET /0/private/Balance HTTP/1.1\r\n"
                                        "Host: %s\r\n"
                                        "API-Key: %s\r\n"
                                        "API-Sign: %s\r\n"
                                        "Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n"
                                        "Connection: keep-alive\r\n"
                                        "\r\n"
                                        "%s",
                                        EXCHANGE_HOST_NAME, API_KEY, apiSignature.c_str(), nonce.c_str());
#endif
        send_unencrypted_bytes(&orderManagerClients[i], unencrypted_request[i], strlen(unencrypted_request[i]));
        do_encrypt(&orderManagerClients[i]);
        do_sock_write(&orderManagerClients[i]);
        int res = do_sock_read(&orderManagerClients[i], false);
        if (res == 0) 
            std::cout << "HEARTBEAT MESSAGE SENT FOR CONNECTION " << i << std::endl;
        else 
            std::cerr << "HEARTBEAT MESSAGE WAS NOT ABLE TO BE SENT FOR CONNECTION " << i << std::endl;
    }
}

void orderManager(SPSCQueue<StrategyComponentToOrderManagerQueueEntry>& strategyToOrderManagerQueue, int bookBuilderPipeEnd) {
    int numCores = std::thread::hardware_concurrency();
    
    if (numCores == 0) {
        std::cerr << "Error: Unable to determine the number of CPU cores." << std::endl;
        return;
    } else if (numCores < CPU_CORE_INDEX_FOR_ORDER_MANAGER_THREAD) {
        std::cerr << "Error: Not enough cores to run the system." << std::endl;
        return;
    }

    int cpuCoreNumberForOrderManagerThread = CPU_CORE_INDEX_FOR_ORDER_MANAGER_THREAD;

    setThreadAffinity(pthread_self(), cpuCoreNumberForOrderManagerThread);

    // orderManagerDataFile.open("order-manager-data/new.txt", std::ios_base::out); 
    // if (!orderManagerDataFile.is_open()) {
    //     std::cerr << "Error: Unable to open file for " << std::endl;
    //     return;
    // }
    // systemDataFile.open("system-data/new.txt", std::ios_base::out); 
    // if (!systemDataFile.is_open()) {
    //     std::cerr << "Error: Unable to open file for " << std::endl;
    //     return;
    // }

#if defined(USE_BITMEX_EXCHANGE)
    int port = 443;
    const char* host_ip = "18.165.242.94";
    const char * host_name = "bitmex.com";
#elif defined(USE_BITMEX_TESTNET_EXCHANGE)
    int port = 443;
    const char* host_ip = "104.18.32.75";
    const char * host_name = "testnet.bitmex.com";
#elif defined(USE_KRAKEN_EXCHANGE) 
    int port = 443;
    const char* host_ip = "104.17.186.205";
    const char * host_name = "api.kraken.com";
#elif defined(USE_KRAKEN_MOCK_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
    int port = 12345;
    const char* host_ip = "146.169.41.107";
    const char * host_name;
#endif
    int ip_family = AF_INET;

    for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
        sockfds[i] = socket(ip_family, SOCK_STREAM, 0);

        if (sockfds[i] < 0)
            die("socket()");

        if (ip_family == AF_INET) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = ip_family;
            addr.sin_port = htons(port);

            if (inet_pton(ip_family, host_ip, &(addr.sin_addr)) <= 0)
                die("inet_pton()");

            if (connect(sockfds[i], (struct sockaddr *) &addr, sizeof(addr)) < 0)
                die("connect()");
        }
    }

    printf("sockets connected\n");

    struct pollfd fdset[ARBITRAGE_BATCH_SIZE];
    memset(&fdset, 0, sizeof(fdset));
    for (int i = 0; i < ARBITRAGE_BATCH_SIZE; i++) {
        fdset[i].fd = sockfds[i];
        fdset[i].events = POLLERR | POLLHUP | POLLNVAL | POLLIN;
    }

    ssl_init(0, 0);
    for (int i = 0; i < ARBITRAGE_BATCH_SIZE; i++) {
        ssl_client_init(&orderManagerClients[i], sockfds[i], SSLMODE_CLIENT);
        if (host_name)
            SSL_set_tlsext_host_name(orderManagerClients[i].ssl, host_name); // TLS SNI
    }

    for (int i = 0; i < ARBITRAGE_BATCH_SIZE; i++) {
        do_ssl_handshake(&orderManagerClients[i]);
        printf("Performing SSL handshake\n");
        while (true) {
            if (SSL_is_init_finished(orderManagerClients[i].ssl))
                break;

            fdset[i].events &= ~POLLOUT;
            fdset[i].events |= ssl_client_want_write(&orderManagerClients[i]) ? POLLOUT : 0;

            int nready = poll(&fdset[0], ARBITRAGE_BATCH_SIZE, -1);

            if (nready == 0)
                continue; /* no fd ready */

            int revents = fdset[i].revents;
            if (revents & POLLIN)
                if (do_sock_read(&orderManagerClients[i], true) == -1)
                    break;

            if (revents & POLLOUT)
                if (do_sock_write(&orderManagerClients[i]) == -1)
                    break;

            if (revents & (POLLERR | POLLHUP | POLLNVAL))
                break;

            if (orderManagerClients[i].encrypt_len > 0)
                if (do_encrypt(&orderManagerClients[i]) < 0)
                    break;
        }
    }

    printf("SSL handshake done for all sockets\n");

    struct io_uring ring;
    struct io_uring_params params;

    // Initialize io_uring
    // Use submission queue polling if user has root privileges
    if (geteuid()) {
        printf("You need root privileges to run the Order Manager with submission queue polling\n");
        int ret = io_uring_queue_init(NUMBER_OF_IO_URING_SQ_ENTRIES, &ring, 0);
        if (ret) {
            perror("io_uring_queue_init");
            return;
        }
    } else {
        printf("Running the Order Manager with submission queue polling\n");
        memset(&params, 0, sizeof(params));
        params.flags |= IORING_SETUP_SQPOLL;

        int bookBuilderRingFd;
        if (read(bookBuilderPipeEnd, &bookBuilderRingFd, sizeof(bookBuilderRingFd)) != sizeof(bookBuilderRingFd)) {
            perror("Pipe read error in Order Manager");
            return;
        }
        printf("Book Builder ring fd seen by Order Manager: %d\n", bookBuilderRingFd);
        params.wq_fd = bookBuilderRingFd;

        params.flags |= IORING_SETUP_ATTACH_WQ;
        params.sq_thread_idle = 1;
        int ret = io_uring_queue_init_params(NUMBER_OF_IO_URING_SQ_ENTRIES, &ring, &params);
        printf("ORDER MANAGER RING WQ_FD: %d\n", ring.ring_fd);
        if (ret) {
            perror("io_uring_queue_init");
            return;
        }
    }

    if (io_uring_register_files(&ring, sockfds, ARBITRAGE_BATCH_SIZE) < 0) {
        perror("io_uring_register_files");
        return;
    }

    int stop = 0;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    while (true) {
        char orderData[ARBITRAGE_BATCH_SIZE][TX_DEFAULT_BUF_SIZE];
        std::chrono::system_clock::time_point exchangeUpdateTxTimepoints[ARBITRAGE_BATCH_SIZE];
        std::chrono::system_clock::time_point orderBookFinalChangeTimestamps[ARBITRAGE_BATCH_SIZE];
        std::chrono::system_clock::time_point strategyComponentOrderPushTimstamps[ARBITRAGE_BATCH_SIZE];
        std::chrono::system_clock::time_point orderManagerOrderDetectionTimepoints[ARBITRAGE_BATCH_SIZE];
        char expires[ARBITRAGE_BATCH_SIZE][32];
        char unencrypted_signature[ARBITRAGE_BATCH_SIZE][256];
        char unencrypted_request[ARBITRAGE_BATCH_SIZE][1024];
        const char *signature;
        StrategyComponentToOrderManagerQueueEntry orderQueueEntries[ARBITRAGE_BATCH_SIZE];
        int cqeResults[ARBITRAGE_BATCH_SIZE];

        auto lastHeartbeatTransmissionTime = std::chrono::steady_clock::now();
        for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {  
            while (!strategyToOrderManagerQueue.pop(orderQueueEntries[i])) {};
        }
        std::chrono::system_clock::time_point arbitrageOrdersPopTimestamp = high_resolution_clock::now();
        std::chrono::system_clock::time_point arbitrageFirstOrderPushTimestamp = orderQueueEntries[0].strategyOrderPushTimestamp;
        
        for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {    
            std::string orderData_i = orderQueueEntries[i].order;
            system_clock::time_point orderDetectionTimepoint = high_resolution_clock::now();
            strcpy(orderData[i], orderData_i.c_str());
            orderManagerOrderDetectionTimepoints[i] = orderDetectionTimepoint;
            exchangeUpdateTxTimepoints[i] = orderQueueEntries[i].marketUpdateExchangeTimestamp;
            orderBookFinalChangeTimestamps[i] = orderQueueEntries[i].orderBookFinalChangeTimestamp;
            strategyComponentOrderPushTimstamps[i] = orderQueueEntries[i].strategyOrderPushTimestamp;

            time_t now = time(NULL);
            time_t tenSecondsLater = now + 10;
            strftime(expires[i], sizeof(expires[i]), "%s", localtime(&tenSecondsLater));

            snprintf(unencrypted_signature[i], sizeof(unencrypted_signature[i]), "%s%s%s%s", REST_API_ADD_ORDER_REQUEST_URI, REST_API_ADD_ORDER_REQUEST_METHOD, expires[i], orderData[i]);

            // printf("Unix Timestamp (expires): %s\n", expires[i]);
            // printf("Unencrypted signature form: %s, strlen: %ld, sizeof: %ld\n", unencrypted_signature[i], strlen(unencrypted_signature[i]), sizeof(unencrypted_signature[i]));
            // printf("Encrypted hexadecimal signature: %s\n", signature);

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
            signature = generateBitmexApiSignature(API_SECRET, strlen(API_SECRET), unencrypted_signature[i], strlen(unencrypted_signature[i]));
            sprintf(unencrypted_request[i], 
                                         "POST /api/v1/order HTTP/1.1\r\n"
                                         "Host: %s\r\n"
                                         "api-key: %s\r\n"
                                         "api-expires: %s\r\n"
                                         "api-signature: %s\r\n"
                                         "Content-Type: application/x-www-form-urlencoded\r\n"
                                         "Content-Length: %zu\r\n"
                                         "Connection: keep-alive\r\n"
                                         "\r\n"
                                         "%s", EXCHANGE_HOST_NAME, API_KEY, expires[i], signature, strlen(orderData[i]), orderData[i]);
            send_unencrypted_bytes(&orderManagerClients[i], unencrypted_request[i], strlen(unencrypted_request[i]));                                            
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
            std::string nonce = generateNonce();
            std::string postData = "nonce=" + nonce + "&" + orderData[i];
            std::string apiSignature = generateKrakenApiSignature(REST_API_ADD_ORDER_REQUEST_URI, nonce, postData, API_SECRET);

            std::string unencrypted_request = "POST /0/private/AddOrder HTTP/1.1\r\n"
                                            "Host: " + std::string(EXCHANGE_HOST_NAME) + "\r\n"
                                            "API-Key: " + std::string(API_KEY) + "\r\n"
                                            "API-Sign: " + apiSignature + "\r\n"
                                            "Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n"
                                            "Content-Length: " + std::to_string(postData.size()) + "\r\n"
                                            "Connection: keep-alive\r\n"
                                            "\r\n" + postData;
            send_unencrypted_bytes(&orderManagerClients[i], unencrypted_request.c_str(), unencrypted_request.size());
#endif
            
        }
        std::chrono::system_clock::time_point requestsPreparationCompletionTimestamp = high_resolution_clock::now();

        for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
            do_encrypt(&orderManagerClients[i]);
        }
        std::chrono::system_clock::time_point requestsEncryptionCompletionTimestamp = high_resolution_clock::now();

        for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
            sqe = io_uring_get_sqe(&ring);

            if (!sqe) {
                perror("io_uring_get_sqe");
                io_uring_queue_exit(&ring);
                return;
            }

            printf("Sending for sockfd %d\n", sockfds[i]);
            io_uring_prep_write(sqe, i, orderManagerClients[i].writeBuffer, orderManagerClients[i].writeLen, 0);
            sqe->flags |= IOSQE_FIXED_FILE;
        }

        if (io_uring_submit(&ring) < 0) {
            perror("io_uring_submit");
            io_uring_queue_exit(&ring);
            return;
        }
        
        // std::cout << "IO_URING SUBMISSION TIME: " << getCurrentTime(submissionTimestamp) << std::endl;

        // Wait for completions
        for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                perror("Error waiting for completion: %s\n");
                return;
            }
            cqeResults[i] = cqe->res;
            io_uring_cqe_seen(&ring, cqe);
        }

        system_clock::time_point socketWritesCompletionTimestamp = high_resolution_clock::now();

        for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
            if (cqeResults[i] <= 0) {
                perror("io_uring completion error");
                return;
            }
            
            // Read response
            if (cqeResults[i] > 0) {
                if ((size_t)cqeResults[i] < orderManagerClients[i].writeLen)
                    memmove(orderManagerClients[i].writeBuffer, orderManagerClients[i].writeBuffer+cqeResults[i], orderManagerClients[i].writeLen - cqeResults[i]);
                orderManagerClients[i].writeLen -= cqeResults[i];
                orderManagerClients[i].writeBuffer = (char*)realloc(orderManagerClients[i].writeBuffer, orderManagerClients[i].writeLen);
            }
            else
                return;   
        }

        std::chrono::system_clock::time_point exchangeExecutionTimestamps[ARBITRAGE_BATCH_SIZE];
        int break_polling = 0;
        while (true) {
            int nready = poll(&fdset[0], ARBITRAGE_BATCH_SIZE, -1);
            /*printf("nready: %d %d ", nready, break_polling);*/

            if (nready == 0)
                continue; /* no fd ready */

            for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
                int revents = fdset[i].revents;
                if (revents & POLLIN) {
                    int bytes_read = do_sock_read(&orderManagerClients[i], false);
                    size_t last_char_index = strlen(orderManagerClients[i].response_buf) - 1;
                    // printf("exits %d %c\n", bytes_read, orderManagerClients[i].response_buf[strlen(orderManagerClients[i].response_buf) - 1]);
                    if (orderManagerClients[i].response_buf[last_char_index] == '}') {
                        std::cout << "RESPONSE BUF: " << orderManagerClients[i].response_buf << std::endl;

                        exchangeExecutionTimestamps[i] = convertTimestampToTimePoint(extract_json(std::string(orderManagerClients[i].response_buf)).FindMember("transactTime")->value.GetString());

                        std::cout
                        << "\n===========================================================================================\n"
                        << "NEW ORDER EXECUTED\n"
                        << "\nExchange Update Occurence to Update Receival (Book Builder) (ms): "
                        << getTimeDifference(exchangeUpdateTxTimepoints[i], orderBookFinalChangeTimestamps[i]) << "      "
                        << "\nUpdate Receival (Book Builder) to Arbitrage Detection (Strategy Component) (ms): "
                        << getTimeDifference(orderBookFinalChangeTimestamps[i], strategyComponentOrderPushTimstamps[i]) << "      "
                        << "\nArbitrage Detection (Strategy Component) to io_uring Submission (Order Manager) (ms): "
                        << getTimeDifference(strategyComponentOrderPushTimstamps[i], socketWritesCompletionTimestamp) << "      "
                        << "\nio_uring Submission (Order Manager) to Exchange Order Execution (ms): "
                        << getTimeDifference(socketWritesCompletionTimestamp, exchangeExecutionTimestamps[i]) << "     \n "

                        << "\nOrder Manager Latency (ms): "
                        << getTimeDifference(orderManagerOrderDetectionTimepoints[i], socketWritesCompletionTimestamp) << "      "
                        
                        << "\nTotal Latency: " << getTimeDifference(exchangeUpdateTxTimepoints[i], exchangeExecutionTimestamps[i])
                        << "\n===========================================================================================\n"
                        << std::endl;

                        memset(orderManagerClients[i].response_buf, 0, sizeof(orderManagerClients[i].response_buf));
                        break_polling++;
                    }
                }

                if (revents & (POLLERR | POLLHUP | POLLNVAL))
                    break;

            }

            if (break_polling >= ARBITRAGE_BATCH_SIZE)
                break;
        }
        std::chrono::system_clock::time_point lastOrderExecutionTimestamp = exchangeExecutionTimestamps[ARBITRAGE_BATCH_SIZE - 1];

        // auto arbitrageFirstOrderPushUs = timePointToMicroseconds(arbitrageFirstOrderPushTimestamp);
        // auto arbitrageOrdersPopUs = timePointToMicroseconds(arbitrageOrdersPopTimestamp);
        // auto requestsPreparationCompletionUs = timePointToMicroseconds(requestsPreparationCompletionTimestamp);
        // auto requestsEncryptionCompletionUs = timePointToMicroseconds(requestsEncryptionCompletionTimestamp);
        // auto socketWritesCompletionUs = timePointToMicroseconds(socketWritesCompletionTimestamp);
        // auto lastOrderExecutionUs = timePointToMicroseconds(lastOrderExecutionTimestamp);
        // double queueLatency = (arbitrageOrdersPopUs - arbitrageFirstOrderPushUs) / 1000.0; 
        // double requestsPreparationLatency = (requestsPreparationCompletionUs - arbitrageOrdersPopUs) / 1000.0;
        // double requestsEncrytpionLatency = (requestsEncryptionCompletionUs - requestsPreparationCompletionUs) / 1000.0;
        // double socketWritesLatency = (socketWritesCompletionUs - requestsEncryptionCompletionUs) / 1000.0;
        // double networkAndExchangeLatency = (lastOrderExecutionUs - socketWritesCompletionUs) / 1000.0;
    
        // orderManagerDataFile 
        // << queueLatency << ", "
        // << requestsPreparationLatency << ", "
        // << requestsEncrytpionLatency << ", "
        // << socketWritesLatency << ", "
        // << networkAndExchangeLatency 
        // << std::endl;
        
        // auto marketUpdateExchangeUs = timePointToMicroseconds(orderQueueEntries[0].marketUpdateExchangeTimestamp);
        // auto updateSocketRxUs = timePointToMicroseconds(orderQueueEntries[0].updateSocketRxTimeStamp);
        // auto orderBookFinalChangeUs = timePointToMicroseconds(orderQueueEntries[0].orderBookFinalChangeTimestamp);
        // auto arbitrageFirstOrderPushUs = timePointToMicroseconds(arbitrageFirstOrderPushTimestamp);
        // auto socketWritesCompletionUs = timePointToMicroseconds(socketWritesCompletionTimestamp);
        // auto lastOrderExecutionUs = timePointToMicroseconds(lastOrderExecutionTimestamp);
        
        // double downstreamNetworkLatency = (updateSocketRxUs - marketUpdateExchangeUs) / 1000.0; 
        // double bookBuilderLatency = (orderBookFinalChangeUs - updateSocketRxUs) / 1000.0;
        // double strategyComponentLatency = (arbitrageFirstOrderPushUs - orderBookFinalChangeUs) / 1000.0;
        // double orderManagerLatency = (socketWritesCompletionUs - arbitrageFirstOrderPushUs) / 1000.0;
        // double upstreamNetworkAndExchangeLatency = (lastOrderExecutionUs - socketWritesCompletionUs) / 1000.0;

        // systemDataFile 
        // << downstreamNetworkLatency << ", "
        // << bookBuilderLatency << ", "
        // << strategyComponentLatency << ", "
        // << orderManagerLatency << ", "
        // << upstreamNetworkAndExchangeLatency 
        // << std::endl;
    }

    for (int i = 0; i < ARBITRAGE_BATCH_SIZE; ++i) {
        close(fdset[i].fd);
        print_ssl_state(&orderManagerClients[i]);
        print_ssl_error();
        ssl_client_cleanup(&orderManagerClients[i]); 
    }
}
