// Utils.cpp

#include "Utils.hpp"
#include <iostream>
#include <iomanip> 
#include <sstream>

long convertTimestampToTimePoint(const std::string& timestamp) {
    std::istringstream ss(timestamp);
    std::tm tm = {};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        // Failed to parse timestamp
        return -1; // Indicate failure
    }

    // If there are milliseconds in the timestamp, parse and add them
    long milliseconds = 0;
    if (ss.peek() == '.') {
        ss.ignore(); // Ignore the dot
        ss >> milliseconds;
        if (ss.fail()) {
            // Failed to parse milliseconds
            return -1; // Indicate failure
        }
    }

    // Convert std::tm to std::chrono::system_clock::time_point
    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    // Convert std::chrono::system_clock::time_point to duration since epoch
    auto duration = time_point.time_since_epoch() + std::chrono::milliseconds(milliseconds);

    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void pinThread(int cpu) {
  if (cpu < 0) {
    return;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == -1) {
    perror("pthread_setaffinity_no");
    exit(1);
  }
}

