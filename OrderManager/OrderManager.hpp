#include <iostream>
#include <string>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <string_view>
#include <array>
#include <cstring>
#include <iomanip>
#include <thread>
#include <mutex>
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"
#include "./ThreadPool.hpp"

void orderManager(SPSCQueue<StrategyComponentToOrderManagerQueueEntry>& strategyToOrderManagerQueue, int bookBuilderPipeEnd);
