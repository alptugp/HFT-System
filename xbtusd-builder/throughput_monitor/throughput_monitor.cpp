#include <iostream>
#include "throughput_monitor.hpp"

ThroughputMonitor::ThroughputMonitor(const std::chrono::high_resolution_clock::time_point& startTime)
    : tradeCount(0), startTime(startTime) {}

void ThroughputMonitor::onTradeReceived() {
    tradeCount++;
    
    // Print average throughput every 1000 trades
    if (tradeCount % 1000 == 0) {
        auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::high_resolution_clock::now() - startTime
        ).count();
        double averageThroughput = static_cast<double>(tradeCount) / elapsedTime;
        std::cout << "Average Throughput (last 1000 trades): " << averageThroughput << " trades per second" << std::endl;
        tradeCount = 0;  // Reset trade count for the next interval
        startTime = std::chrono::high_resolution_clock::now(); // Reset startTime to now
    }
}
