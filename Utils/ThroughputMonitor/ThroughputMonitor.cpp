#include <iostream>
#include <atomic>
#include "ThroughputMonitor.hpp"

ThroughputMonitor::ThroughputMonitor(std::string id, const std::chrono::high_resolution_clock::time_point& startTime)
    : id(id), operationCount(0), startTime(startTime) {}

void ThroughputMonitor::operationCompleted() {
    operationCount.fetch_add(1);
    
    // Print average throughput every 1000 operations
    if (operationCount.load() % 10000 == 0) {
        auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::high_resolution_clock::now() - startTime
        ).count();
        // std::cout << elapsedTime << std::endl;
        std::cout << static_cast<double>(operationCount.load()) << elapsedTime << std::endl;
        double averageThroughput = static_cast<double>(operationCount.load()) / elapsedTime;
        std::cout << id << " - Average Throughput (for last 10000 operations): " << averageThroughput << " operations per second" << std::endl;
        operationCount.store(0); // Reset trade count for the next interval
        startTime = std::chrono::high_resolution_clock::now(); // Reset startTime to now
    }
}
