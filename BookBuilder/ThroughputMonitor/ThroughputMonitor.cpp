#include <iostream>
#include "ThroughputMonitor.hpp"

ThroughputMonitor::ThroughputMonitor(std::string id, const std::chrono::high_resolution_clock::time_point& startTime)
    : id(id), operationCount(0), startTime(startTime) {}

void ThroughputMonitor::operationCompleted() {
    operationCount++;
    
    // Print average throughput every 1000 operations
    if (operationCount % 1000 == 0) {
        auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::high_resolution_clock::now() - startTime
        ).count();
        // std::cout << elapsedTime << std::endl;
        double averageThroughput = static_cast<double>(operationCount) / elapsedTime;
        std::cout << id << " - Average Throughput (for last 1000 operations): " << averageThroughput << " operations per second" << std::endl;
        operationCount = 0;  // Reset trade count for the next interval
        startTime = std::chrono::high_resolution_clock::now(); // Reset startTime to now
    }
}
