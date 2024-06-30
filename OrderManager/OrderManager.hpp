#include <iostream>
#include <string>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <string_view>
#include <array>
#include <cstring>
#include <iomanip>
#include <thread>
#include <mutex>
#include <liburing.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sys/epoll.h>
#include <unistd.h>
#include "../SPSCQueue/SPSCQueue.hpp"
#include "../Utils/Utils.hpp"

void orderManager(SPSCQueue<StrategyComponentToOrderManagerQueueEntry>& strategyToOrderManagerQueue, int bookBuilderPipeEnd);
