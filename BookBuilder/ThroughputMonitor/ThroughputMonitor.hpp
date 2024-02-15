#ifndef THROUGHPUT_MONITOR_HPP
#define THROUGHPUT_MONITOR_HPP

#include <chrono>

class ThroughputMonitor {
private:
    int tradeCount;
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

public:
    ThroughputMonitor(const std::chrono::high_resolution_clock::time_point& startTime);
    void onTradeReceived();
};

#endif // THROUGHPUT_MONITOR_HPP
