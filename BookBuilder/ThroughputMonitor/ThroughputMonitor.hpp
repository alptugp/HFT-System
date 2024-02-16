#ifndef THROUGHPUT_MONITOR_HPP
#define THROUGHPUT_MONITOR_HPP

#include <chrono>

class ThroughputMonitor {
private:
    std::string id;
    int operationCount;
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

public:
    ThroughputMonitor(std::string id, const std::chrono::high_resolution_clock::time_point& startTime);
    void operationCompleted();
};

#endif // THROUGHPUT_MONITOR_HPP
