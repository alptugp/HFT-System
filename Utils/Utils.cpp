// Utils.cpp

#include "Utils.hpp"

std::chrono::system_clock::time_point convertTimestampToTimePoint(const std::string& timestamp) {
    std::istringstream ss(timestamp);
    std::tm tm = {};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    // if (ss.fail()) {
    //     // Failed to parse the main part of the timestamp
    //     return; // Indicate failure
    // }

    // If there are fractional seconds in the timestamp, parse and add them
    long microseconds = 0;
    if (ss.peek() == '.') {
        ss.ignore(); // Ignore the dot
        std::string fractional;
        std::getline(ss, fractional, 'Z'); // Read up to the 'Z' character
        if (fractional.length() > 6) {
            fractional = fractional.substr(0, 6);
        } else if (fractional.length() < 6) {
            fractional.append(6 - fractional.length(), '0');
        }
        microseconds = std::stol(fractional);
    }

    // Convert std::tm to std::chrono::system_clock::time_point
    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    // Adjust for the microseconds part
    time_point += std::chrono::microseconds(microseconds);

    return time_point;
}

void removeIncorrectNullCharacters(char* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] == '\0') {
            buffer[i] = ' ';
        }
    }
}

long long timePointToMicroseconds(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

double getTimeDifference(const std::chrono::system_clock::time_point& time1, const std::chrono::system_clock::time_point& time2) {
    long long time1_us = timePointToMicroseconds(time1);
    long long time2_us = timePointToMicroseconds(time2);

    // Convert the string representations to time points or durations
#if defined(USE_BITMEX_EXCHANGE) || defined(USE_BITMEX_MOCK_EXCHANGE) || defined(USE_BITMEX_TESTNET_EXCHANGE)
    auto timePoint1 = std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(time1_us));
    auto timePoint2 = std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(time2_us));
    // Convert the duration to milliseconds
    return std::chrono::duration_cast<std::chrono::milliseconds>(timePoint2 - timePoint1).count();
#elif defined(USE_KRAKEN_MOCK_EXCHANGE) || defined (USE_BITMEX_MOCK_EXCHANGE)
    auto timePoint1 = std::chrono::time_point<std::chrono::system_clock>(std::chrono::microseconds(time1_us));
    auto timePoint2 = std::chrono::time_point<std::chrono::system_clock>(std::chrono::microseconds(time2_us));
    // Convert the duration to milliseconds
    return std::chrono::duration_cast<std::chrono::microseconds>(timePoint2 - timePoint1).count() / 1000.0;
#endif
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

