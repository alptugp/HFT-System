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

    // If there are fractional seconds in the updateExchangeTimestamp, parse and add them
    long microseconds = 0;
    if (ss.peek() == '.') {
        ss.ignore(); // Ignore the dot
        std::string fractional;
        ss >> fractional;
        if (ss.fail()) {
            // Failed to parse fractional seconds
            return -1; // Indicate failure
        }
        
        // Pad or trim the fractional part to 6 digits for microseconds
        if (fractional.length() > 6) {
            fractional = fractional.substr(0, 6);
        } else if (fractional.length() < 6) {
            fractional.append(6 - fractional.length(), '0');
        }
        
        microseconds = std::stol(fractional);
    }

    // Convert std::tm to std::chrono::system_clock::time_point
    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    // Convert std::chrono::system_clock::time_point to duration since epoch
    auto duration = time_point.time_since_epoch() + std::chrono::microseconds(microseconds);

    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
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

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setw(6) << std::setfill('0') << us.count();
    ss << 'Z';
    return ss.str();
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

