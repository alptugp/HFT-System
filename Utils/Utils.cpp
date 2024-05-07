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
        // Failed to parse updateExchangeTimestamp
        return -1; // Indicate failure
    }

    // If there are milliseconds in the updateExchangeTimestamp, parse and add them
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

long long getTimeDifferenceInMillis(const std::string& strTime1, const std::string& strTime2) {
    long long time1 = std::stoll(strTime1);
    long long time2 = std::stoll(strTime2);

    // Convert the string representations to time points or durations
    auto timePoint1 = std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(time1));
    auto timePoint2 = std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(time2));

    // Calculate the duration between the two time points
    auto duration = timePoint2 - timePoint1;

    // Convert the duration to milliseconds
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}


// Function to set CPU affinity of a thread
void setThreadAffinity(pthread_t thread, int cpuCore) {
  if (cpuCore < 0) {
    return;
  }

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpuCore, &cpuset);
  
  if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1) {
    perror("pthread_setaffinity_no");
    exit(1);
  };
}

