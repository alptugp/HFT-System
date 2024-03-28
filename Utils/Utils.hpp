// Utils.h

#ifndef UTILS_H
#define UTILS_H

#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <string>

long convertTimestampToTimePoint(const std::string& timestamp);
void pinThread(int cpu);

#endif // UTILS_H