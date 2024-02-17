// Utils.h

#ifndef UTILS_H
#define UTILS_H

#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <string>

std::chrono::time_point<std::chrono::high_resolution_clock> convertTimestampToTimePoint(const std::string& timestamp);
void pinThread(int cpu);

#endif // UTILS_H