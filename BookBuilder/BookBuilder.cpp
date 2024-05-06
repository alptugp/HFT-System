#include <libwebsockets.h>
#include <rapidjson/document.h>
#include <string>
#include <signal.h>
#include <chrono>
#include "./OrderBook/OrderBook.hpp"
#include "./ThroughputMonitor/ThroughputMonitor.hpp"
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

#define MAX_JSON_SIZE 8192

using namespace rapidjson;
using namespace std::chrono;
using Clock = std::chrono::high_resolution_clock;

std::unordered_map<std::string, OrderBook> orderBookMap;
SPSCQueue<OrderBook>* bookBuilderToStrategyQueue = nullptr;
ThroughputMonitor* updateThroughputMonitor = nullptr; 
/*const std::vector<std::string> currencyPairs = {"BTCUSD", "ETHBTC", "ETHUSD"};*/
const std::vector<std::string> currencyPairs = {"XBTETH", "XBTUSDT", "ETHUSDT"};
// const std::vector<std::string> currencyPairs = {"XBTUSD", "ETHUSD", "XBTETH", "XBTUSDT", "SOLUSD", "XRPUSD", "LINKUSD", "SOLUSD", "XRPUSD"};
// const std::vector<std::string> currencyPairs = {"XBTUSD", "ETHUSD", "SOLUSD", "XBTUSDT", "DOGEUSD", "XBTH24", "LINKUSD", "XRPUSD", "SOLUSDT", "BCHUSD"};

typedef struct range {
    uint64_t		sum;
    uint64_t		lowest;
    uint64_t		highest;

    unsigned int		samples;
} range_t;

static struct my_conn {
    lws_sorted_usec_list_t	sul;	     /* schedule connection retry */
    lws_sorted_usec_list_t	sul_hz;	     /* 1hz summary */

    range_t			e_lat_range;
    range_t			price_range;

    struct lws		*wsi;	     /* related wsi if any */
    uint16_t		retry_count; /* count of consequetive retries */
} mco;

static struct lws_context *context;
static int interrupted;

static const uint32_t backoff_ms[] = { 1000, 2000, 3000, 4000, 5000 };

static const lws_retry_bo_t retry = {
        .retry_ms_table			= backoff_ms,
        .retry_ms_table_count		= LWS_ARRAY_SIZE(backoff_ms),
        .conceal_count			= LWS_ARRAY_SIZE(backoff_ms),

        .secs_since_valid_ping		= 400,  /* force PINGs after secs idle */
        .secs_since_valid_hangup	= 400, /* hangup after secs idle */

        .jitter_percent			= 0,
}; 

static const struct lws_extension extensions[] = {
        {
                "permessage-deflate",
                lws_extension_callback_pm_deflate,
                      "permessage-deflate"
                      "; client_no_context_takeover"
                      "; client_max_window_bits"
        },
        { NULL, NULL, NULL /* terminator */ }
};

/*
 * Scheduled sul callback that starts the connection attempt
 */

static void
connect_client(lws_sorted_usec_list_t *sul)
{
    struct my_conn *mco = lws_container_of(sul, struct my_conn, sul);
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = context;
    i.port = 443;

    // i.address = "ws.bitmex.com";
    i.address = "ws.testnet.bitmex.com";
    i.path = "/realtime";
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_PRIORITIZE_READS;
    i.protocol = NULL;
    i.local_protocol_name = "lws-minimal-client";
    i.pwsi = &mco->wsi;
    i.retry_and_idle_policy = &retry;
    i.userdata = mco;

    if (!lws_client_connect_via_info(&i))
        /*
         * Failed... schedule a retry... we can't use the _retry_wsi()
         * convenience wrapper api here because no valid wsi at this
         * point.
         */
        if (lws_retry_sul_schedule(context, 0, sul, &retry,
                                   connect_client, &mco->retry_count)) {
            lwsl_err("%s: connection attempts exhausted\n", __func__);
            interrupted = 1;
        }
}

static char partial_ob_json_buffer[MAX_JSON_SIZE];
static size_t partial_ob_json_buffer_len = 0;

static void
range_reset(range_t *r)
{
    r->sum = r->highest = 0;
    r->lowest = 999999999999ull;
    r->samples = 0;
}

static void
sul_hz_cb(lws_sorted_usec_list_t *sul)
{
    struct my_conn *mco = lws_container_of(sul, struct my_conn, sul_hz);

    /*
     * We are called once a second to dump statistics on the connection
     */

    lws_sul_schedule(lws_get_context(mco->wsi), 0, &mco->sul_hz,
                     sul_hz_cb, LWS_US_PER_SEC);

    if (mco->price_range.samples)
        lwsl_notice("%s: price: min: %llu¢, max: %llu¢, avg: %llu¢, "
                    "(%d prices/s)\n",
                    __func__,
                    (unsigned long long)mco->price_range.lowest,
                    (unsigned long long)mco->price_range.highest,
                    (unsigned long long)(mco->price_range.sum / mco->price_range.samples),
                    mco->price_range.samples);
    if (mco->e_lat_range.samples)
        lwsl_notice("%s: elatency: min: %llums, max: %llums, "
                    "avg: %llums, (%d msg/s)\n", __func__,
                    (unsigned long long)mco->e_lat_range.lowest / 1000,
                    (unsigned long long)mco->e_lat_range.highest / 1000,
                    (unsigned long long)(mco->e_lat_range.sum /
                                         mco->e_lat_range.samples) / 1000,
                    mco->e_lat_range.samples);

    range_reset(&mco->e_lat_range);
    range_reset(&mco->price_range);
}

static int
callback_minimal(struct lws *wsi, enum lws_callback_reasons reason,
                 void *user, void *in, size_t len)
{
    struct my_conn *mco = (struct my_conn *)user;
    uint64_t latency_us, now_us;
    const char *p;
    size_t alen;
    Document doc;
    GenericValue<rapidjson::UTF8<>>::MemberIterator data;
    const char* symbol; 
    uint64_t id;
    const char* action;
    const char* side;
    double size;
    double price;
    const char* timestamp;
    system_clock::time_point marketUpdateReceiveTimestamp;
    long exchangeUpdateTimestamp;
    

    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err("CLIENT_CONNECTION_ERROR: %s\n",
                     in ? (char *)in : "(null)");
            goto do_retry;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            /*
             * The messages are a few 100 bytes of JSON each
             */

            // lwsl_hexdump_notice(in, len);

            updateThroughputMonitor->operationCompleted();

            marketUpdateReceiveTimestamp = high_resolution_clock::now();
            /*std::cout << updateExchangeTimestamp << std::endl;*/
            // auto networkLatency = std::chrono::duration_cast<std::chrono::microseconds>(programReceiveTime - exchangeUnixTimestamp);

            // printf("\nReceived size: %ld, %d, JSON: %s\n", len, (int)len, (const char *)in);

            if (((const char *)in)[0] == '{' && ((const char *)in)[len - 1] == '}') {
                if (((const char *)in)[2] == 'i' || ((const char *)in)[2] == 's') {
                    break;
                }

                // std::cout << "CASE 1 HIT" << std::endl;
                doc.Parse((const char *)in, len);
            } else if (((const char *)in)[0] == '{') {
                // std::cout << "CASE 2 HIT" << std::endl;
                memset(partial_ob_json_buffer, 0, MAX_JSON_SIZE);
                partial_ob_json_buffer_len = 0;

                memcpy(partial_ob_json_buffer + partial_ob_json_buffer_len, in, len);
                partial_ob_json_buffer_len += len;
                break;
            } else if (((const char *)in)[len - 1] == '}') {
                // std::cout << "CASE 3 HIT" << std::endl;
                memcpy(partial_ob_json_buffer + partial_ob_json_buffer_len, in, len);
                partial_ob_json_buffer_len += len;

                doc.Parse(partial_ob_json_buffer, partial_ob_json_buffer_len);
            } else {
                // std::cout << "CASE 4 HIT" << std::endl;
                memcpy(partial_ob_json_buffer + partial_ob_json_buffer_len, in, len);
                partial_ob_json_buffer_len += len;
                break;
            }

            if (doc.HasParseError()) {
                lwsl_err("%s: no E JSON\n", __func__);
                std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;
                break;
            }

            data = doc.FindMember("data");
            action = doc.FindMember("action")->value.GetString();

            for (SizeType i = 0; i < data->value.Size(); i++) {
                symbol = data->value[i].FindMember("symbol")->value.GetString();
                id = data->value[i].FindMember("id")->value.GetInt64();
                side = data->value[i].FindMember("side")->value.GetString();

                if (data->value[i].HasMember("size")) 
                    size = data->value[i].FindMember("size")->value.GetInt64();
                
                price = data->value[i].FindMember("price")->value.GetDouble();
                timestamp = data->value[i].FindMember("timestamp")->value.GetString();
                exchangeUpdateTimestamp = convertTimestampToTimePoint(timestamp);

                std::string sideStr(side);

                if (sideStr == "Buy") {
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
                } else if (sideStr == "Sell") {
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

                // auto bookBuildingEndTime = Clock::now();
                // auto bookBuildingDuration = std::chrono::duration_cast<std::chrono::microseconds>(bookBuildingEndTime - programReceiveTime);
                // std::cout << "Symbol: " << symbol << " - Action: " << action << " - Size: " << size << " - Price: " << price << " (" << side << ")" << " - id: " << id << " - Timestamp: " << updateExchangeTimestamp << std::endl;
                // std::cout << "Time taken to receive market update: " << networkLatency.count() << " microseconds" << std::endl;
                // std::cout << "Time taken to process market update: " << bookBuildingDuration.count() << " microseconds" << std::endl;
                // std::cout << "Timestamp: " << updateExchangeTimestamp << std::endl;
                // orderBook.printOrderBook();
                // orderBook.updateOrderBookMemoryUsage();
            }

            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_user("%s: established\n", __func__);

            // Send subscription message here
            for (const std::string& currencyPair : currencyPairs) { 
                std::string subscriptionMessage = "{\"op\":\"subscribe\",\"args\":[\"orderBookL2_25:" + currencyPair + "\"]}";
                // Allocate buffer with LWS_PRE bytes before the data
                unsigned char buf[LWS_PRE + subscriptionMessage.size()];

                // Copy subscription message to buffer after LWS_PRE bytes
                memcpy(&buf[LWS_PRE], subscriptionMessage.c_str(), subscriptionMessage.size());
                
                // Send data using lws_write
                lws_write(wsi, &buf[LWS_PRE], subscriptionMessage.size(), LWS_WRITE_TEXT);
            }
            
            lws_sul_schedule(lws_get_context(wsi), 0, &mco->sul_hz,
                             sul_hz_cb, LWS_US_PER_SEC);
            mco->wsi = wsi;
            range_reset(&mco->e_lat_range);
            range_reset(&mco->price_range);
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            lws_sul_cancel(&mco->sul_hz);
            goto do_retry;

        default:
            break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);

    do_retry:
    if (lws_retry_sul_schedule_retry_wsi(wsi, &mco->sul, connect_client,
                                         &mco->retry_count)) {
        lwsl_err("%s: connection attempts exhausted\n", __func__);
        interrupted = 1;
    }

    return 0;
}

static const struct lws_protocols protocols[] = {
        { "lws-minimal-client", callback_minimal, 0, 0, 0, NULL, 0 },
        LWS_PROTOCOL_LIST_TERM
};

static void
sigint_handler(int sig)
{
    interrupted = 1;
}

void bookBuilder(int cpu, SPSCQueue<OrderBook>& bookBuilderToStrategyQueue_) {
    pinThread(cpu);

    for (std::string currencyPair : currencyPairs) { 
        orderBookMap[currencyPair] = OrderBook(currencyPair);
    }

    bookBuilderToStrategyQueue = &bookBuilderToStrategyQueue_;

    ThroughputMonitor updateThroughputMonitorBookBuilder("Book Builder Throughput Monitor", std::chrono::high_resolution_clock::now());
    updateThroughputMonitor = &updateThroughputMonitorBookBuilder;

    struct lws_context_creation_info info;
    int n = 0;

    signal(SIGINT, sigint_handler);
    memset(&info, 0, sizeof info);

    lwsl_user("LWS bitmex client\n");

    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_WITH_LIBEV;
    info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
    info.protocols = protocols;
    info.fd_limit_per_thread = 1 + 1 + 1;
    info.extensions = extensions;

    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return;
    }

    /* schedule the first client connection attempt to happen immediately */
    lws_sul_schedule(context, 0, &mco.sul, connect_client, 1);

    while (n >= 0 && !interrupted)
        n = lws_service(context, 0);

    lws_context_destroy(context);
    lwsl_user("Completed\n");    
}