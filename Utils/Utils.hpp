// Utils.h

#ifndef UTILS_H
#define UTILS_H

#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <string>

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
    system_clock::time_point strategyComponentArbitrageDetectionTimestamp;
    system_clock::time_point marketUpdateExchangeTimestamp;
    system_clock::time_point orderBookTimestamp;


};

long convertTimestampToTimePoint(const std::string& timestamp);
long long getTimeDifferenceInMillis(const std::string& strTime1, const std::string& strTime2);
std::string getCurrentTimestamp();
void removeIncorrectNullCharacters(char* buffer, size_t size);
long long timePointToMicroseconds(const std::chrono::system_clock::time_point& tp);
void setThreadAffinity(pthread_t thread, int cpuCore);

#endif // UTILS_H