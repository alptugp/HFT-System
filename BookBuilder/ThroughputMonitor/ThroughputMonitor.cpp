#include <iostream>
#include "ThroughputMonitor.hpp"

ThroughputMonitor::ThroughputMonitor(const std::chrono::high_resolution_clock::time_point& startTime)
    : tradeCount(0), startTime(startTime) {}

void ThroughputMonitor::onTradeReceived() {
    tradeCount++;
    
    // Print average throughput every 1000 trades
    if (tradeCount % 1000 == 0) {
        auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::high_resolution_clock::now() - startTime
        ).count();
        // std::cout << elapsedTime << std::endl;
        double averageThroughput = static_cast<double>(tradeCount) / elapsedTime;
        std::cout << "Average Throughput (for last 1000 trades): " << averageThroughput << " operations per second" << std::endl;
        tradeCount = 0;  // Reset trade count for the next interval
        startTime = std::chrono::high_resolution_clock::now(); // Reset startTime to now
    }
}
