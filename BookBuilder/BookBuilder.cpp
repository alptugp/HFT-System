#include <libwebsockets.h>
#include <rapidjson/document.h>
#include <string>
#include <signal.h>
#include <chrono>
#include <ev.h>
#include <liburing.h>
#include <fstream>
#include <sys/socket.h>
#include "./OrderBook/OrderBook.hpp"
#include "./ThroughputMonitor/ThroughputMonitor.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

#define CPU_CORE_INDEX_FOR_BOOK_BUILDER_THREAD 1
#define CPU_CORE_INDEX_FOR_SQ_POLL_THREAD 0
#define NUMBER_OF_IO_URING_SQ_ENTRIES 64
#define WEBSOCKET_CLIENT_RX_BUFFER_SIZE 16378

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
    #define JSON_START_PATTERN "{\"table\""
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)
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

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)  
const std::vector<std::string> currencyPairs = {"XBTETH", "XBTUSDT", "ETHUSDT"};
#elif defined(USE_KRAKEN_EXCHANGE) || defined(USE_KRAKEN_MOCK_EXCHANGE)  
const std::vector<std::string> currencyPairs = {"ETH/BTC", "BTC/USD", "ETH/USD"};
#endif

static int interrupted, rx_seen, test;
// static struct lws *client_wsi;
static struct lws *client_wsi[NUMBER_OF_CONNECTIONS];

static struct ev_loop *loop_ev; 
ev_timer timeout_watcher;

struct ws_client_io
{
  ev_io socket_watcher;
  int sockfd;
  uint connection_idx;
  char undecryptedReadBuffer[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];	
  int undecryptedReadBufferSize = sizeof(undecryptedReadBuffer);
  char decryptedReadBuffer[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];	
  int decryptedReadBufferSize = sizeof(decryptedReadBuffer);
  struct msghdr msg;
  struct iovec iov[1];
  struct timeval tv;
  char ctrl_buf[CMSG_SPACE(sizeof(tv))];
  struct cmsghdr* cmsg;
};

static struct ws_client_io *ws_clients_io[NUMBER_OF_CONNECTIONS];

SSL *ssls[NUMBER_OF_CONNECTIONS];
BIO *rbios[NUMBER_OF_CONNECTIONS];
int sockfds[NUMBER_OF_CONNECTIONS];

struct io_uring ring;
struct io_uring_sqe *sqe;
struct io_uring_cqe *cqe;

std::ofstream latencyDataFile;
std::ofstream historicalDataFile;

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

    int n, connection_idx = (int)(intptr_t)lws_get_opaque_user_data(wsi);

	lwsl_user("protocol called\n");
	switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err("CLIENT_CONNECTION_ERROR: %s\n",
                in ? (char *)in : "(null)");
            // client_wsi = NULL;
            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            printf("LWS_CALLBACK_CLIENT_ESTABLISHED for %d\n", connection_idx);
            lwsl_user("%s: established\n", __func__);
#ifndef USE_KRAKEN_MOCK_EXCHANGE
#ifndef USE_BITMEX_MOCK_EXCHANGE
            // Send subscription message 
    #ifdef USE_BITMEX_EXCHANGE   
            // std::string currencyPair = currencyPairs[connection_idx];
            
            // std::string subscriptionMessage = "{\"op\":\"subscribe\",\"args\":[\"orderBookL2_25:" + currencyPair + "\"]}";
            // // Allocate buffer with LWS_PRE bytes before the data
            // unsigned char buf[LWS_PRE + subscriptionMessage.size()];

            // // Copy subscription message to buffer after LWS_PRE bytes
            // memcpy(&buf[LWS_PRE], subscriptionMessage.c_str(), subscriptionMessage.size());
                    
            // // Send data using lws_write
            // lws_write(wsi, &buf[LWS_PRE], subscriptionMessage.size(), LWS_WRITE_TEXT);

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

            // subscriptionMessage += "\"" + currencyPairs[connection_idx] + "\"";
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
#endif
			interrupted = 1;
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
            // client_wsi = NULL;
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

void socket_cb (EV_P_ ev_io *w_, int revents) {
    struct ws_client_io *w = (struct ws_client_io *) w_;
    uint connection_idx = w->connection_idx;

  	if (revents & EV_READ) { 
        system_clock::time_point marketUpdateReadyToReadTimestamp = high_resolution_clock::now();
        // printf("SOCKET ready for reading for connection %d\n", connection_idx);
        int decryptedBytesRead;
        
		do {
            // printf("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
			sqe = io_uring_get_sqe(&ring);

            if (!sqe) {
                perror("io_uring_get_sqe");
                io_uring_queue_exit(&ring);
                return;
            }

			// printf("Reading for sockfd %d\n", sockfd);
            io_uring_prep_recvmsg(sqe, connection_idx, &w->msg, 0);
			// io_uring_prep_recv(sqe, connection_idx, w->undecryptedReadBuffer, w->undecryptedReadBufferSize, 0);  // use offset on same buffer later?
            sqe->flags |= IOSQE_FIXED_FILE;

			if (io_uring_submit(&ring) < 0) {
				perror("io_uring_submit");
				io_uring_queue_exit(&ring);
				return;
			}

            int ret = io_uring_wait_cqe(&ring, &cqe);
            system_clock::time_point marketUpdateReadFinishTimestamp = high_resolution_clock::now();

            if (ret < 0) {
                perror("Error waiting for completion: %s\n");
                return;
            }

            if (cqe->res < 0) {
                perror("Async operation error");
                return;
            } 
            
			int undecryptedBytesRead = cqe->res;				
			int bytesBioWritten = BIO_write(rbios[connection_idx], w->undecryptedReadBuffer, undecryptedBytesRead);
            decryptedBytesRead = SSL_read(ssls[connection_idx], w->decryptedReadBuffer, w->decryptedReadBufferSize);

            system_clock::time_point marketUpdateDecryptionFinishTimestamp = high_resolution_clock::now();
            io_uring_cqe_seen(&ring, cqe);
            
            // printf("BYTES READ from ssl: %d\n", decryptedBytesRead);
            if (decryptedBytesRead > 0) {
                removeIncorrectNullCharacters(w->decryptedReadBuffer, decryptedBytesRead);

                // std::cout << "BUFFER: " << w->decryptedReadBuffer << std::endl;
   
                const char* currentPos = w->decryptedReadBuffer;
                int isAllDataRead = false;
                while (currentPos < w->decryptedReadBuffer + strlen(w->decryptedReadBuffer)) {
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
					
                    // if (historicalDataFile.is_open()) {
                    //     historicalDataFile << jsonStr << std::endl;
                    // } else {
                    //     std::cerr << "Unable to open file: " << "kraken_data.txt" << std::endl;
                    // }

                    Document doc;
                    doc.Parse(jsonStr);

                    if (doc.HasParseError()) {
                        std::cerr << "JSON parsing error\n";
                        break;
                    }

                    system_clock::time_point marketUpdateJsonParsingFinishTimestamp = high_resolution_clock::now();
                    long exchangeUpdateRxTimestamp;

#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) 
                    GenericValue<rapidjson::UTF8<>>::MemberIterator data = doc.FindMember("data");
                    const char* action = doc["action"].GetString();
                    std::vector<std::string> updatedCurrencies;

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
                        exchangeUpdateRxTimestamp = convertTimestampToTimePoint(timestamp);
                       // std::cout << "exchangeUpdateTimestamp: " << exchangeUpdateTimestamp << ", marketUpdateReceiveTimestamp: " << std::to_string(duration_cast<milliseconds>(marketUpdateReceiveTimestamp.time_since_epoch()).count()) << std::endl;
                        if (strcmp(side, "Buy") == 0) {
                            switch (action[0]) {
                                case 'p':
                                case 'i':
                                    orderBookMap[symbol].insertBuy(id, price, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                    break;
                                case 'u':
                                    orderBookMap[symbol].updateBuy(id, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                    break;
                                case 'd':
                                    orderBookMap[symbol].removeBuy(id, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                    break;
                                default:
                                    break;
                            }
                        } else if (strcmp(side, "Sell") == 0) {
                            switch (action[0]) {
                                case 'p':
                                case 'i':
                                    orderBookMap[symbol].insertSell(id, price, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                    break;
                                case 'u':
                                    orderBookMap[symbol].updateSell(id, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                    break;
                                case 'd':
                                    orderBookMap[symbol].removeSell(id, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                    break;
                                default:
                                    break;
                            }
                        }
                        updatedCurrencies.push_back(std::string(symbol));
                    } 

#elif defined(USE_KRAKEN_EXCHANGE) || defined (USE_KRAKEN_MOCK_EXCHANGE)
                    GenericValue<rapidjson::UTF8<>>::MemberIterator data = doc.FindMember("data");
                    const char* type = doc["type"].GetString();
                    std::vector<std::string> updatedCurrencies;

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
                                orderBookMap[symbol].insertSell(price, price, size, 0, marketUpdateReadFinishTimestamp);
                            }

                            for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                                const Value& bid_i = bids->value[i];
                                double price = bid_i["price"].GetDouble();
                                double size = bid_i["qty"].GetDouble();
                                orderBookMap[symbol].insertBuy(price, price, size, 0, marketUpdateReadFinishTimestamp);
                            }
                        } else if (type[0] == 'u') {
                            const char* timestamp = data_i["timestamp"].GetString();
                            exchangeUpdateRxTimestamp = convertTimestampToTimePoint(timestamp);
                            GenericValue<rapidjson::UTF8<>>::ConstMemberIterator asks = data_i.FindMember("asks");
                            GenericValue<rapidjson::UTF8<>>::ConstMemberIterator bids = data_i.FindMember("bids");
                            const char* symbol = data_i["symbol"].GetString();    
                            uint64_t checksum = data_i["checksum"].GetInt64();

                            for (SizeType i = 0; i < data_i["asks"].Size(); i++) {
                                const Value& ask_i = asks->value[i]; 
                                double price = ask_i["price"].GetDouble();
                                double size = ask_i["qty"].GetDouble();
                                if (size == 0)
                                    orderBookMap[symbol].removeSell(price, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                else if (orderBookMap[symbol].checkSellPriceLevel(price))
                                    orderBookMap[symbol].updateSell(price, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                else 
                                    orderBookMap[symbol].insertSell(price, price, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                            }
                            
                            for (SizeType i = 0; i < data_i["bids"].Size(); i++) {
                                const Value& bid_i = bids->value[i]; 
                                double price = bid_i["price"].GetDouble();
                                double size = bid_i["qty"].GetDouble();
                                if (size == 0)
                                    orderBookMap[symbol].removeBuy(price, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                else if (orderBookMap[symbol].checkBuyPriceLevel(price))
                                    orderBookMap[symbol].updateBuy(price, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                                else 
                                    orderBookMap[symbol].insertBuy(price, price, size, exchangeUpdateRxTimestamp, marketUpdateReadFinishTimestamp);
                            }
                        }
                        updatedCurrencies.push_back(std::string(symbol));
                    }
#endif          
                    system_clock::time_point marketUpdateBookBuildingFinishTimestamp = high_resolution_clock::now();
                    
                    for (std::string updatedCurrency : updatedCurrencies) { 
                        std::cout << "UPDATED CURRENCY: " << updatedCurrency << std::endl;
                        while (!bookBuilderToStrategyQueue->push(orderBookMap[updatedCurrency]));    
                    }

                    system_clock::time_point marketUpdateSocketRxTimestamp;
                    w->cmsg = CMSG_FIRSTHDR(&w->msg);
                    if (w->cmsg->cmsg_level == SOL_SOCKET && w->cmsg->cmsg_type == SCM_TIMESTAMP) {
                        memcpy(&w->tv, CMSG_DATA(w->cmsg), sizeof(w->tv));
                        std::cout << "Received packet at timestamp: " << w->tv.tv_sec << " seconds and " << w->tv.tv_usec << " microseconds" << std::endl;
                        marketUpdateSocketRxTimestamp = std::chrono::system_clock::from_time_t((long)w->tv.tv_sec);
                        marketUpdateSocketRxTimestamp += std::chrono::microseconds((long) w->tv.tv_usec);
                    }

                    // Convert time_point to microseconds since epoch
                    auto socketRxUs = timePointToMicroseconds(marketUpdateSocketRxTimestamp);
                    auto readyToReadUs = timePointToMicroseconds(marketUpdateReadyToReadTimestamp);
                    auto readFinishUs = timePointToMicroseconds(marketUpdateReadFinishTimestamp);
                    auto decryptionFinishUs = timePointToMicroseconds(marketUpdateDecryptionFinishTimestamp);
                    auto jsonParsingFinishUs = timePointToMicroseconds(marketUpdateJsonParsingFinishTimestamp);
                    auto bookBuildingFinishUs = timePointToMicroseconds(marketUpdateBookBuildingFinishTimestamp);

                    double networkLatency = (socketRxUs - exchangeUpdateRxTimestamp) / 1000.0; 
                    double socketWaitLatency = (readyToReadUs - socketRxUs) / 1000.0;
                    double readLatency = (readFinishUs - readyToReadUs) / 1000.0;
                    double decryptionLatency = (decryptionFinishUs - readFinishUs) / 1000.0;
                    double jsonParsingLatency = (jsonParsingFinishUs - decryptionFinishUs) / 1000.0;
                    double bookBuildingLatency = (bookBuildingFinishUs - jsonParsingFinishUs) / 1000.0;

                    latencyDataFile 
                    << networkLatency << ", "
                    << socketWaitLatency << ", "
                    << readLatency << ", "
                    << decryptionLatency << ", "
                    << jsonParsingLatency << ", "
                    << bookBuildingLatency 
                    << std::endl;

                    // Move to the next JSON object
                    currentPos = endPos + 1;
                }

                if (isAllDataRead) {
                    memset(w->undecryptedReadBuffer, 0, w->undecryptedReadBufferSize);
                    memset(w->decryptedReadBuffer, 0, w->decryptedReadBufferSize);
                    memset(w->ctrl_buf, 0, sizeof(w->ctrl_buf));
                    break;   
                } 
			}	

			memset(w->undecryptedReadBuffer, 0, w->undecryptedReadBufferSize);
            memset(w->decryptedReadBuffer, 0, w->decryptedReadBufferSize);
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
#if defined(USE_KRAKEN_MOCK_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE)
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
	// i.pwsi = &client_wsi;
    
    for (int m = 0; m < NUMBER_OF_CONNECTIONS; m++) {
        i.pwsi = &client_wsi[m];
        i.opaque_user_data = (void *)(intptr_t)m;
        lws_client_connect_via_info(&i);

        std::cout << "SOCKET FD:" << lws_get_socket_fd(client_wsi[m]) << std::endl;

        while (n >= 0 && client_wsi[m] && !interrupted) {
            n = lws_service(context, 0);
            // std::cout << "LWS SERVICE EXECUTING" << std::endl;
        }

        interrupted = 0;

        sockfds[m] = lws_get_socket_fd(client_wsi[m]);
        int timestamp_option = 1;
        if (setsockopt(sockfds[m], SOL_SOCKET, SO_TIMESTAMP, &timestamp_option, sizeof(timestamp_option)) < 0) {
            perror("setsockopt SO_TIMESTAMP failed");
            // Handle error
        }
        
        ssls[m] = lws_get_ssl(client_wsi[m]);
        rbios[m] = BIO_new(BIO_s_mem());
	    SSL_set_bio(ssls[m], rbios[m], NULL);
    }
	
#if defined(USE_BITMEX_MOCK_EXCHANGE)    
    latencyDataFile.open("bitmex-book-builder-data/bitmex-book-builder-data.txt", std::ios_base::out); 
#elif defined(USE_KRAKEN_MOCK_EXCHANGE)
    latencyDataFile.open("mapping-kraken-book-builder/mapping-kraken-book-builder-data.txt", std::ios_base::out); 
    // latencyDataFile.open("conn-type-kraken-book-builder/conn-type-kraken-book-builder-data.txt", std::ios_base::out); 
#endif

#if defined(USE_BITMEX_EXCHANGE)    
    // historicalDataFile.open("bitmex_data.txt", std::ios_base::out);
#elif defined(USE_KRAKEN_EXCHANGE)
    // historicalDataFile.open("kraken_data.txt", std::ios_base::out);
#endif

    if (io_uring_register_files(&ring, sockfds, NUMBER_OF_CONNECTIONS) < 0) {
        perror("io_uring_register_files");
        return;
    }

    for (int m = 0; m < NUMBER_OF_CONNECTIONS; m++) {
        ws_clients_io[m] = new ws_client_io();
        ws_clients_io[m]->sockfd = sockfds[m];
        ws_clients_io[m]->connection_idx = m;

        ws_clients_io[m]->iov[0].iov_base = ws_clients_io[m]->undecryptedReadBuffer;
        ws_clients_io[m]->iov[0].iov_len = ws_clients_io[m]->undecryptedReadBufferSize;

        ws_clients_io[m]->msg.msg_control = ws_clients_io[m]->ctrl_buf;
        ws_clients_io[m]->msg.msg_controllen = sizeof(ws_clients_io[m]->ctrl_buf);
        ws_clients_io[m]->msg.msg_iov = ws_clients_io[m]->iov;
        ws_clients_io[m]->msg.msg_iovlen = 1;

        ev_io_init (&ws_clients_io[m]->socket_watcher, socket_cb, sockfds[m], EV_READ);

        ev_io_start (loop_ev, &ws_clients_io[m]->socket_watcher);
    }
    
    // initialise a timer watcher, then start it
    // simple non-repeating 5.5 second timeout
    ev_timer_init (&timeout_watcher, timeout_cb, 600, 0.);
    ev_timer_start (loop_ev, &timeout_watcher); 

    ev_run (loop_ev, 0);

	lws_context_destroy(context);
    close(orderManagerPipeEnd);    
}