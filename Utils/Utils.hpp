// Utils.h

#ifndef UTILS_H
#define UTILS_H

#include <iomanip> 
#include <sstream>
#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <string>
#include <iostream>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <vector>
#include <stdexcept>

#define WEBSOCKET_CLIENT_RX_BUFFER_SIZE 16378

using namespace std::chrono;

struct BookBuilderGatewayToComponentQueueEntry {
    char decryptedReadBuffer[WEBSOCKET_CLIENT_RX_BUFFER_SIZE];	
    int decryptedReadBufferSize = sizeof(decryptedReadBuffer);
    int decryptedBytesRead;
    system_clock::time_point marketUpdatePollTimestamp;
    system_clock::time_point marketUpdateReadCompletionTimestamp;
    system_clock::time_point marketUpdateSocketRxTimestamp;
    system_clock::time_point marketUpdateDecryptionCompletionTimestamp;
};

struct StrategyComponentToOrderManagerQueueEntry {
    std::string order;
    system_clock::time_point strategyOrderPushTimestamp;
    system_clock::time_point marketUpdateExchangeTimestamp;
    system_clock::time_point orderBookFinalChangeTimestamp;
    system_clock::time_point updateSocketRxTimeStamp;
};

std::chrono::system_clock::time_point convertTimestampToTimePoint(const std::string& timestamp);
double getTimeDifference(const std::chrono::system_clock::time_point& time1, const std::chrono::system_clock::time_point& time2);
std::string getCurrentTimestamp();
void removeIncorrectNullCharacters(char* buffer, size_t size);
long long timePointToMicroseconds(const std::chrono::system_clock::time_point& tp);
void setThreadAffinity(pthread_t thread, int cpuCore);

#endif // UTILS_H