#include <libwebsockets.h>
#include <rapidjson/document.h>
#include <string>
#include <signal.h>
#include <chrono>
#include <ev.h>
#include <liburing.h>
#include <fstream>
#include "./OrderBook/OrderBook.hpp"
#include "./ThroughputMonitor/ThroughputMonitor.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

#define CPU_CORE_INDEX_FOR_BOOK_BUILDER_THREAD 1
#define CPU_CORE_INDEX_FOR_SQ_POLL_THREAD 0
#define NUMBER_OF_IO_URING_SQ_ENTRIES 64
#define WEBSOCKET_CLIENT_RX_BUFFER_SIZE 16378

#ifdef USE_BITMEX_EXCHANGE
    #define JSON_START_PATTERN "{\"table\""
#elif defined(USE_KRAKEN_EXCHANGE)
    #define JSON_START_PATTERN "{\"channel\":\"book\""
#endif

#define JSON_END_PATTERN "}]}"

#define NUMBER_OF_CONNECTIONS 1

using namespace rapidjson;
using namespace std::chrono;
using Clock = std::chrono::high_resolution_clock;

std::unordered_map<std::string, OrderBook> orderBookMap;
SPSCQueue<OrderBook>* bookBuilderToStrategyQueue = nullptr;
ThroughputMonitor* updateThroughputMonitor = nullptr; 

#ifdef USE_BITMEX_EXCHANGE  
const std::vector<std::string> currencyPairs = {"XBTETH", "XBTUSDT", "ETHUSDT"};
#elif defined(USE_KRAKEN_EXCHANGE) 
const std::vector<std::string> currencyPairs = {"ETH/BTC", "BTC/USD", "ETH/USD"};
#endif

static int interrupted, rx_seen, test;
static struct lws *client_wsi;

static struct ev_loop *loop_ev; 
ev_io socket_watcher;
ev_timer timeout_watcher;

SSL *ssl;
BIO *rbio;
int sockfds[NUMBER_OF_CONNECTIONS];

struct io_uring ring;
struct io_uring_sqe *sqe;
struct io_uring_cqe *cqe;

std::ofstream outFile;

// static const struct lws_extension extensions[] = {
//         {
//                 "permessage-deflate",
//                 lws_extension_callback_pm_deflate,
//                       "permessage-deflate"
//                       "; client_no_context_takeover"
//                       "; client_max_window_bits"
//         },
//         { NULL, NULL, NULL /* terminator */ }
// };

static int
book_builder_lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
	      void *user, void *in, size_t len)
{	
	lwsl_user("protocol called\n");
	switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err("CLIENT_CONNECTION_ERROR: %s\n",
                in ? (char *)in : "(null)");
            client_wsi = NULL;
            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            printf("LWS_CALLBACK_CLIENT_ESTABLISHED\n");
            lwsl_user("%s: established\n", __func__);
#ifndef USE_MOCK_EXCHANGE
            // Send subscription message 
    #ifdef USE_BITMEX_EXCHANGE   
            for (const std::string& currencyPair : currencyPairs) { 
                    std::string subscriptionMessage = "{\"op\":\"subscribe\",\"args\":[\"orderBookL2_25:" + currencyPair + "\"]}";
                    // Allocate buffer with LWS_PRE bytes before the data
                    unsigned char buf[LWS_PRE + subscriptionMessage.size()];

                    // Copy subscription message to buffer after LWS_PRE bytes
                    memcpy(&buf[LWS_PRE], subscriptionMessage.c_str(), subscriptionMessage.size());
                    
                    // Send data using lws_write
                    lws_write(wsi, &buf[LWS_PRE], subscriptionMessage.size(), LWS_WRITE_TEXT);
            }
    #elif defined(USE_KRAKEN_EXCHANGE)
            std::string subscriptionMessage = R"({
                                                "method": "subscribe",
                                                "params": {
                                                    "channel": "book",
                                                    "depth": 10,
                                                    "snapshot": true,
                                                    "symbol": [)";

            for (size_t i = 0; i < currencyPairs.size(); ++i) {
                subscriptionMessage += "\"" + currencyPairs[i] + "\"";
                if (i < currencyPairs.size() - 1) {
                    subscriptionMessage += ",";
                }
            }

            subscriptionMessage += R"(]
                                        },
                                        "req_id": 1234567890
                                        }
                                    )";

            unsigned char buf[LWS_PRE + subscriptionMessage.size()];

            // Copy subscription message to buffer after LWS_PRE bytes
            memcpy(&buf[LWS_PRE], subscriptionMessage.c_str(), subscriptionMessage.size());
                    
            // Send data using lws_write
            lws_write(wsi, &buf[LWS_PRE], subscriptionMessage.size(), LWS_WRITE_TEXT);
    #endif
#endif
			interrupted = 1;
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
            client_wsi = NULL;
            break;

        default:
            break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols protocols[] = {
        { "book-builder-lws-client", book_builder_lws_callback, 0, 0, 0, NULL, 0 },
        LWS_PROTOCOL_LIST_TERM
};

static void
sigint_handler(int sig)
{
    interrupted = 1;
}

void removeIncorrectNullCharacters(char* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] == '\0') {
            buffer[i] = ' ';
        }
    }
}

void socket_cb (EV_P_ ev_io *w, int revents) {
  	if (revents & EV_READ) { 
        puts ("SOCKET ready for reading\n");
		int decryptedBytesRead;
		
		do {
            char buffer[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];	
            int bufferSize = sizeof(buffer);
            printf("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");

			sqe = io_uring_get_sqe(&ring);

            if (!sqe) {
                perror("io_uring_get_sqe");
                io_uring_queue_exit(&ring);
                return;
            }
			
			// printf("Reading for sockfd %d\n", sockfd);
			io_uring_prep_read(sqe, 0, buffer, bufferSize, 0);  // use offset on same buffer later?
            sqe->flags |= IOSQE_FIXED_FILE;

			if (io_uring_submit(&ring) < 0) {
				perror("io_uring_submit");
				io_uring_queue_exit(&ring);
				return;
			}

			int ret = io_uring_wait_cqe(&ring, &cqe);
            system_clock::time_point marketUpdateReceiveTimestamp = high_resolution_clock::now();
            if (ret < 0) {
                perror("Error waiting for completion: %s\n");
                return;
            }

            if (cqe->res < 0) {
                perror("Async operation error");
                return;
            } 

			int encryptedBytesRead = cqe->res;
				
            io_uring_cqe_seen(&ring, cqe);

			int bytesBioWritten = BIO_write(rbio, buffer, encryptedBytesRead);

			memset(buffer, 0, sizeof(buffer));
    
            decryptedBytesRead = SSL_read(ssl, buffer, sizeof(buffer));
            
            // printf("BYTES READ: %d\n", decryptedBytesRead);
            if (decryptedBytesRead > 0) {
                removeIncorrectNullCharacters(buffer, decryptedBytesRead);

                std::cout << "BUFFER: " << buffer << std::endl;
				const char* currentPos = buffer;
                int isAllDataRead = false;
                while (currentPos < buffer + strlen(buffer)) {
                    const char* startPos = strstr(currentPos, JSON_START_PATTERN);
                    if (!startPos) 
                        break;

                    const char* endPos = strstr(startPos, JSON_END_PATTERN);
                    if (!endPos) 
                        break;
                    
                    // Extract the substring containing the JSON object
                    size_t jsonLen = endPos - startPos + strlen(JSON_END_PATTERN);
                    
                    isAllDataRead = true;

                    if (jsonLen >= WEBSOCKET_CLIENT_RX_BUFFER_SIZE) {
                        std::cerr << "JSON string too long\n";
                        break;
                    }

                    char jsonStr[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];
                    strncpy(jsonStr, startPos, jsonLen);
                    jsonStr[jsonLen] = '\0';
					
                    // if (outFile.is_open()) {
                    //     outFile << jsonStr << std::endl;
                    // } else {
                    //     std::cerr << "Unable to open file: " << "bitmex_data.txt" << std::endl;
                    // }

                    Document doc;
                    doc.Parse(jsonStr);

                    if (doc.HasParseError()) {
                        std::cerr << "JSON parsing error\n";
                        break;
                    }

#ifdef USE_BITMEX_EXCHANGE
                    GenericValue<rapidjson::UTF8<>>::MemberIterator data = doc.FindMember("data");
                    const char* action = doc["action"].GetString();
                    for (SizeType i = 0; i < doc["data"].Size(); i++) {
                        const Value& data_i = data->value[i];
                        const char* symbol = data_i["symbol"].GetString();
                        uint64_t id = data_i["id"].GetInt64();
                        const char* side = data_i["side"].GetString();
                        double size;
                        if (data->value[i].HasMember("size")) 
                            size = data_i["size"].GetInt64();
                        
                        double price = data_i["price"].GetDouble();
                        const char* timestamp = data_i["timestamp"].GetString();
                        long exchangeUpdateTimestamp = convertTimestampToTimePoint(timestamp);
                       // std::cout << "exchangeUpdateTimestamp: " << exchangeUpdateTimestamp << ", marketUpdateReceiveTimestamp: " << std::to_string(duration_cast<milliseconds>(marketUpdateReceiveTimestamp.time_since_epoch()).count()) << std::endl;
                        if (strcmp(side, "Buy") == 0) {
                            switch (action[0]) {
                                case 'p':
                                case 'i':
                                    orderBookMap[symbol].insertBuy(id, price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                    break;
                                case 'u':
                                    orderBookMap[symbol].updateBuy(id, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                    break;
                                case 'd':
                                    orderBookMap[symbol].removeBuy(id, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                    break;
                                default:
                                    break;
                            }
                        } else if (strcmp(side, "Sell") == 0) {
                            switch (action[0]) {
                                case 'p':
                                case 'i':
                                    orderBookMap[symbol].insertSell(id, price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                    break;
                                case 'u':
                                    orderBookMap[symbol].updateSell(id, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                    break;
                                case 'd':
                                    orderBookMap[symbol].removeSell(id, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                    break;
                                default:
                                    break;
                            }
                        }
                        while (!bookBuilderToStrategyQueue->push(orderBookMap[symbol]));  
                    }
#elif defined(USE_KRAKEN_EXCHANGE)
                    GenericValue<rapidjson::UTF8<>>::MemberIterator data = doc.FindMember("data");
                    const char* type = doc["type"].GetString();
                    std::vector<std::string> updatedCurrencies(3);

                    for (SizeType i = 0; i < doc["data"].Size(); i++) {
                        const Value& data_i = data->value[i];
                        GenericValue<rapidjson::UTF8<>>::ConstMemberIterator asks = data_i.FindMember("asks");
                        GenericValue<rapidjson::UTF8<>>::ConstMemberIterator bids = data_i.FindMember("bids");
                        const char* symbol = data_i["symbol"].GetString();    
                        uint64_t checksum = data_i["checksum"].GetInt64();
                        if (type[0] == 's') {
                            for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                                const Value& ask_i = asks->value[i];
                                double price = ask_i["price"].GetDouble();
                                double size = ask_i["qty"].GetDouble();
                                orderBookMap[symbol].insertSell(price, price, size, 0, marketUpdateReceiveTimestamp);
                            }

                            for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                                const Value& bid_i = bids->value[i];
                                double price = bid_i["price"].GetDouble();
                                double size = bid_i["qty"].GetDouble();
                                orderBookMap[symbol].insertBuy(price, price, size, 0, marketUpdateReceiveTimestamp);
                            }
                        } else if (type[0] == 'u') {
                            const char* timestamp = data_i["timestamp"].GetString();
                            long exchangeUpdateTimestamp = convertTimestampToTimePoint(timestamp);
                            GenericValue<rapidjson::UTF8<>>::ConstMemberIterator asks = data_i.FindMember("asks");
                            GenericValue<rapidjson::UTF8<>>::ConstMemberIterator bids = data_i.FindMember("bids");
                            const char* symbol = data_i["symbol"].GetString();    
                            uint64_t checksum = data_i["checksum"].GetInt64();

                            for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                                const Value& ask_i = asks->value[i]; 
                                double price = ask_i["price"].GetDouble();
                                double size = ask_i["qty"].GetDouble();
                                if (size == 0)
                                    orderBookMap[symbol].removeSell(price, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                else if (orderBookMap[symbol].checkSellPriceLevel(price))
                                    orderBookMap[symbol].updateSell(price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                else 
                                    orderBookMap[symbol].insertSell(price, price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                            }
                            
                            for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                                const Value& bid_i = bids->value[i]; 
                                double price = bid_i["price"].GetDouble();
                                double size = bid_i["qty"].GetDouble();
                                if (size == 0)
                                    orderBookMap[symbol].removeBuy(price, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                else if (orderBookMap[symbol].checkBuyPriceLevel(price))
                                    orderBookMap[symbol].updateBuy(price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                                else 
                                    orderBookMap[symbol].insertBuy(price, price, size, exchangeUpdateTimestamp, marketUpdateReceiveTimestamp);
                            }
                        }
                        updatedCurrencies.push_back(std::string(symbol));
                    }

                    for (std::string updatedCurrency : updatedCurrencies) { 
                        std::cout << "UPDATED CURRENCY: " << updatedCurrency << std::endl;
                        while (!bookBuilderToStrategyQueue->push(orderBookMap[updatedCurrency]));    
                    }
#endif

                // Move to the next JSON object
                    currentPos = endPos + 1;
                }

                if (isAllDataRead) 
                    break;   
			}	

			memset(buffer, 0, sizeof(buffer));
        } while (decryptedBytesRead > 0);
    }
}

// another callback, this time for a time-out
static void
timeout_cb (EV_P_ ev_timer *w, int revents)
{
  puts ("timeout");
  // this causes the innermost ev_run to stop iterating
  ev_break (EV_A_ EVBREAK_ONE);
}

void bookBuilder(SPSCQueue<OrderBook>& bookBuilderToStrategyQueue_, int orderManagerPipeEnd) {
    int numCores = std::thread::hardware_concurrency();
    
    if (numCores == 0) {
        std::cerr << "Error: Unable to determine the number of CPU cores." << std::endl;
        return;
    } else if (numCores < CPU_CORE_INDEX_FOR_BOOK_BUILDER_THREAD) {
        std::cerr << "Error: Not enough cores to run the system." << std::endl;
        return;
    }

    int cpuCoreNumberForBookBuilderThread = CPU_CORE_INDEX_FOR_BOOK_BUILDER_THREAD;
    setThreadAffinity(pthread_self(), cpuCoreNumberForBookBuilderThread);

    // Set the current thread's real-time priority to highest value
    // struct sched_param schedParams;
    // schedParams.sched_priority = sched_get_priority_max(SCHED_FIFO);
    // pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParams);

    for (std::string currencyPair : currencyPairs) { 
        orderBookMap[currencyPair] = OrderBook(currencyPair);
    }

    bookBuilderToStrategyQueue = &bookBuilderToStrategyQueue_;

    ThroughputMonitor updateThroughputMonitorBookBuilder("Book Builder Throughput Monitor", std::chrono::high_resolution_clock::now());
    updateThroughputMonitor = &updateThroughputMonitorBookBuilder;

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
        params.flags |= IORING_SETUP_SQ_AFF;
        params.sq_thread_idle = 200000;
        params.sq_thread_cpu = CPU_CORE_INDEX_FOR_SQ_POLL_THREAD;
        int ret = io_uring_queue_init_params(NUMBER_OF_IO_URING_SQ_ENTRIES, &ring, &params);
        
        if (ret) {
            perror("io_uring_queue_init");
            return;
        }
    
        int bookBuilderRingFd = ring.ring_fd;
        if (write(orderManagerPipeEnd, &bookBuilderRingFd, sizeof(bookBuilderRingFd)) != sizeof(bookBuilderRingFd)) {
            perror("Pipe write error in Book Builder");
            return;
        }

        printf("WEB SOCKET CLIENT RING FD: %d\n", bookBuilderRingFd);
    }

    struct lws_context_creation_info info;
	struct lws_client_connect_info i;
	struct lws_context *context;
	const char *p;
	int n = 0, logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
		/* for LLL_ verbosity above NOTICE to be built into lws, lws
		 * must have been configured with -DCMAKE_BUILD_TYPE=DEBUG
		 * instead of =RELEASE */
		/* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
		/* | LLL_EXT */ /* | LLL_CLIENT */ /* | LLL_LATENCY */
		/* | LLL_DEBUG */;

	signal(SIGINT, sigint_handler);
	// if ((p = lws_cmdline_option(argc, argv, "-d")))
	// 	logs = atoi(p);

	// test = !!lws_cmdline_option(argc, argv, "-t");

	lws_set_log_level(logs, NULL);
	lwsl_user("LWS Book Builder ws client rx [-d <logs>] [--h2] [-t (test)]\n");

	memset(&info, 0, sizeof info);
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_WITH_LIBEV;
	info.port = CONTEXT_PORT_NO_LISTEN; 
	info.protocols = protocols;

    loop_ev = ev_default_loop(EVBACKEND_EPOLL);
    void *foreign_loops[1];
    foreign_loops[0] = loop_ev;
    info.foreign_loops = foreign_loops;

	info.fd_limit_per_thread = 1 + 1 + 1;

	context = lws_create_context(&info);
	if (!context) {
		lwsl_err("lws init failed\n");
		return;
	}

	memset(&i, 0, sizeof i);
	i.context = context;
#ifdef USE_MOCK_EXCHANGE
    i.port = 7681;
    i.address = "146.169.41.107";
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_PRIORITIZE_READS | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_INSECURE |  LWS_SERVER_OPTION_IGNORE_MISSING_CERT | LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
#elif defined(USE_KRAKEN_EXCHANGE)
    i.port = 443;
	i.address = "ws.kraken.com";
    i.path = "/v2";
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_PRIORITIZE_READS;
#elif defined(USE_BITMEX_EXCHANGE)
    i.port = 443;
	i.address = "ws.bitmex.com";
    i.path = "/realtime";
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_PRIORITIZE_READS;
#endif
	i.host = i.address;
	i.origin = i.address;
	i.protocol = NULL; 
	i.pwsi = &client_wsi;

	lws_client_connect_via_info(&i);

    std::cout << "SOCKET FD:" << lws_get_socket_fd(client_wsi) << std::endl;

    outFile.open("bitmex_data.txt", std::ios_base::out); 

	while (n >= 0 && client_wsi && !interrupted) {
        n = lws_service(context, 0);
        std::cout << "LWS SERVICE EXECUTING" << std::endl;
    }

    sockfds[0] = lws_get_socket_fd(client_wsi);
	ssl = lws_get_ssl(client_wsi);
	rbio = BIO_new(BIO_s_mem());
	SSL_set_bio(ssl, rbio, NULL);


    if (io_uring_register_files(&ring, sockfds, NUMBER_OF_CONNECTIONS) < 0) {
        perror("io_uring_register_files");
        return;
    }

    ev_io_init (&socket_watcher, socket_cb, sockfds[0], EV_READ);
    ev_io_start (loop_ev, &socket_watcher);
    
    // initialise a timer watcher, then start it
    // simple non-repeating 5.5 second timeout
    ev_timer_init (&timeout_watcher, timeout_cb, 600, 0.);
    ev_timer_start (loop_ev, &timeout_watcher); 

    ev_run (loop_ev, 0);

	lws_context_destroy(context);
    close(orderManagerPipeEnd);    
}