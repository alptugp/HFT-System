#include <iostream>
#include <string>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <string_view>
#include <array>
#include <cstring>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <liburing.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "./ThreadPool.hpp"

void orderManager(int cpu, SPSCQueue<std::string>& strategyToOrderManagerQueue);
void testRoundTripTime2(io_uring* ring, struct io_uring_sqe* sqe, const std::string& requestVerb, const std::string& requestPath);
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output);
