#include "LinuxFace/profiler.h"

#include <algorithm>
#include <chrono>

using linuxface::Profiler;

std::string Profiler::makeKey(const std::string& sourceName, const std::string& name)
{
    return sourceName + "::" + name;
}

void Profiler::start(const std::string& sourceName, const std::string& name)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    timers_[makeKey(sourceName, name)] = std::chrono::high_resolution_clock::now();
}

void Profiler::stop(const std::string& sourceName, const std::string& name)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = makeKey(sourceName, name);
    auto it = timers_.find(key);
    if (it != timers_.end())
    {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()
                                                                              - it->second);
        durations_[key] = duration;
        timers_.erase(it);
    }
}

bool Profiler::duration(const std::string& sourceName, const std::string& name,
                        std::chrono::microseconds& duration) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
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
    const std::lock_guard<std::mutex> lock(mutex_);
    return durations_;
}

std::unordered_map<std::string, std::chrono::microseconds> Profiler::getDurations(const std::string& sourceName) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, std::chrono::microseconds> result;
    const std::string prefix = sourceName + "::";

    for (const auto& pair : durations_)
    {
        if (pair.first.size() >= prefix.size() && pair.first.substr(0, prefix.size()) == prefix)
        {
            const std::string operationName = pair.first.substr(prefix.length());
            result[operationName] = pair.second;
        }
    }

    return result;
}

// Sorting should be immediate if we are dealing with small lists
std::vector<std::pair<std::string, std::chrono::microseconds>> Profiler::getDurationsSorted() const
{
    std::vector<std::pair<std::string, std::chrono::microseconds>> result;
    const std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(durations_.size());
    for (const auto& pair : durations_)
    {
        result.emplace_back(pair);
    }
    std::sort(result.begin(), result.end(),
              [](const std::pair<std::string, std::chrono::microseconds>& a,
                 const std::pair<std::string, std::chrono::microseconds>& b) { return a.second > b.second; });
    return result;
}

void Profiler::reset()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    timers_.clear();
    durations_.clear();
}
