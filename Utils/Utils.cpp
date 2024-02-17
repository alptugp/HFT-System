// Utils.cpp

#include "Utils.hpp"
#include <iostream>
#include <iomanip> 
#include <sstream>

std::chrono::time_point<std::chrono::high_resolution_clock> convertTimestampToTimePoint(const std::string& timestamp) {
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    // If timestamp includes milliseconds, parse and add them
    long milliseconds = 0;
    if (ss && ss.peek() == '.') {
        ss.ignore(); // Ignore the dot
        ss >> milliseconds;
    }

    auto time_point = std::chrono::high_resolution_clock::from_time_t(std::mktime(&tm)) + std::chrono::milliseconds(milliseconds);
    return time_point;
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

