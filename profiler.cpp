#include "profiler.h"

#include <chrono>

using namespace funnyface;


void Profiler::start(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    timers_[name] = std::chrono::high_resolution_clock::now();
}

void Profiler::stop(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(name);
    if (it != timers_.end())
    {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()
                                                                              - it->second);
        durations_[name] = duration;
        timers_.erase(it);
    }
}

bool Profiler::duration(const std::string& name, std::chrono::microseconds& duration)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = durations_.find(name);
    if (it != durations_.end())
    {
        duration = it->second;
        return true;
    }
    return false;
}

std::unordered_map<std::string, std::chrono::microseconds> Profiler::getDurations() const {
    return durations_;
}
