#include "profiler.h"

#include <chrono>

using namespace funnyface;

std::string Profiler::makeKey(const std::string& sourceName, const std::string& name) const
{
    return sourceName + "::" + name;
}

void Profiler::start(const std::string& sourceName, const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    timers_[makeKey(sourceName, name)] = std::chrono::high_resolution_clock::now();
}

void Profiler::stop(const std::string& sourceName, const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = makeKey(sourceName, name);
    auto it = timers_.find(key);
    if (it != timers_.end())
    {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()
                                                                              - it->second);
        durations_[key] = duration;
        timers_.erase(it);
    }
}

bool Profiler::duration(const std::string& sourceName, const std::string& name, std::chrono::microseconds& duration) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = durations_.find(makeKey(sourceName, name));
    if (it != durations_.end())
    {
        duration = it->second;
        return true;
    }
    return false;
}

const std::unordered_map<std::string, std::chrono::microseconds>& Profiler::getDurations() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return durations_;
}

std::unordered_map<std::string, std::chrono::microseconds> Profiler::getDurations(const std::string& sourceName) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, std::chrono::microseconds> result;
    std::string prefix = sourceName + "::";

    for (const auto& pair : durations_)
    {
        if (pair.first.size() >= prefix.size() && pair.first.substr(0, prefix.size()) == prefix)
        {
            std::string operationName = pair.first.substr(prefix.length());
            result[operationName] = pair.second;
        }
    }

    return result;
}
