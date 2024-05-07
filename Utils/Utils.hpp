// Utils.h

#ifndef UTILS_H
#define UTILS_H

#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <string>

long convertTimestampToTimePoint(const std::string& timestamp);
long long getTimeDifferenceInMillis(const std::string& strTime1, const std::string& strTime2);
void setThreadAffinity(pthread_t thread, int cpuCore);

#endif // UTILS_H