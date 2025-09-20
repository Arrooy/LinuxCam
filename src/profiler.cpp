#include "LinuxFace/profiler.h"

#include <algorithm>
#include <chrono>

#include "LinuxFace/common.h"

using linuxface::Profiler;

std::string Profiler::makeKey(const std::string& sourceName, const std::string& name)
{
    return sourceName + "::" + name;
}

void Profiler::start(const std::string& sourceName, const std::string& name)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = makeKey(sourceName, name);
    auto& data = timingData_[key];
    data.startTime = std::chrono::high_resolution_clock::now();
    data.isActive = true;
}

void Profiler::stop(const std::string& sourceName, const std::string& name)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = makeKey(sourceName, name);
    auto it = timingData_.find(key);
    if (it != timingData_.end() && it->second.isActive)
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - it->second.startTime);

        // Update current duration
        it->second.duration = duration;
        it->second.lastUpdated = now;
        it->second.isActive = false;

        // Update statistics
        updateStatistics(it->second, duration);
    }
}

bool Profiler::getDuration(const std::string& sourceName, const std::string& name,
                           std::chrono::microseconds& duration) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = timingData_.find(makeKey(sourceName, name));
    if (it != timingData_.end() && !it->second.isActive)
    {
        duration = it->second.duration;
        return true;
    }
    return false;
}

std::unordered_map<std::string, std::chrono::microseconds> Profiler::getAllDurations() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, std::chrono::microseconds> result;

    for (const auto& pair : timingData_)
    {
        if (!pair.second.isActive)
        {
            result[pair.first] = pair.second.duration;
        }
    }

    return result;
}

std::unordered_map<std::string, std::chrono::microseconds>
Profiler::getDurationsBySource(const std::string& sourceName) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, std::chrono::microseconds> result;
    const std::string prefix = sourceName + "::";

    for (const auto& pair : timingData_)
    {
        if (!pair.second.isActive && pair.first.size() >= prefix.size()
            && pair.first.substr(0, prefix.size()) == prefix)
        {
            const std::string operationName = pair.first.substr(prefix.length());
            result[operationName] = pair.second.duration;
        }
    }

    return result;
}

std::vector<std::pair<std::string, std::chrono::microseconds>>
Profiler::getDurationsSorted(const std::string& sourceName) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::chrono::microseconds>> result;
    result.reserve(timingData_.size());

    const std::string prefix = sourceName.empty() ? "" : sourceName + "::";

    for (const auto& pair : timingData_)
    {
        if (!pair.second.isActive)
        {
            // If sourceName is specified, filter by prefix
            if (!sourceName.empty())
            {
                if (pair.first.size() >= prefix.size() && pair.first.substr(0, prefix.size()) == prefix)
                {
                    result.emplace_back(pair.first, pair.second.duration);
                }
            }
            else
            {
                // Include all entries
                result.emplace_back(pair.first, pair.second.duration);
            }
        }
    }

    std::sort(result.begin(), result.end(),
              [](const std::pair<std::string, std::chrono::microseconds>& a,
                 const std::pair<std::string, std::chrono::microseconds>& b) { return a.second > b.second; });

    return result;
}

void Profiler::updateStatistics(TimingData& data, std::chrono::microseconds duration)
{
    // Initialize samples vector if needed
    if (data.samples.empty())
    {
        data.samples.resize(windowSize_, std::chrono::microseconds{0});
        data.sampleIndex = 0;
    }
    else if (data.samples.size() != windowSize_)
    {
        // Window size changed, resize and reset
        data.samples.resize(windowSize_, std::chrono::microseconds{0});
        data.sampleIndex = 0;
        data.minimumDuration = std::chrono::microseconds::max();
        data.maximumDuration = std::chrono::microseconds{0};
    }

    // Add new sample to circular buffer
    data.samples[data.sampleIndex] = duration;
    data.sampleIndex = (data.sampleIndex + 1) % windowSize_;

    // Recalculate min/max from all valid samples in the current window
    // This ensures min/max are consistent with the moving average window
    data.minimumDuration = std::chrono::microseconds::max();
    data.maximumDuration = std::chrono::microseconds{0};
    
    for (const auto& sample : data.samples)
    {
        if (sample.count() > 0)  // Only consider valid samples
        {
            if (sample < data.minimumDuration)
            {
                data.minimumDuration = sample;
            }
            if (sample > data.maximumDuration)
            {
                data.maximumDuration = sample;
            }
        }
    }

    // Reset min if no valid samples in the window
    if (data.minimumDuration == std::chrono::microseconds::max())
    {
        data.minimumDuration = std::chrono::microseconds{0};
    }
}

std::chrono::microseconds Profiler::calculateMovingAverage(const TimingData& data) const
{
    if (data.samples.empty())
    {
        return std::chrono::microseconds{0};
    }

    std::chrono::microseconds total{0};
    size_t validSamples = 0;

    for (const auto& sample : data.samples)
    {
        if (sample.count() > 0)  // Only count valid samples
        {
            total += sample;
            validSamples++;
        }
    }

    return validSamples > 0 ? std::chrono::microseconds{total.count() / validSamples} 
                           : std::chrono::microseconds{0};
}

bool Profiler::getTimerStatistics(const std::string& sourceName, const std::string& name, TimerStatistics& stats) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = timingData_.find(makeKey(sourceName, name));
    if (it != timingData_.end() && !it->second.isActive)
    {
        const auto& data = it->second;
        stats.current = data.duration;
        stats.minimum = data.minimumDuration == std::chrono::microseconds::max() ? std::chrono::microseconds{0}
                                                                                 : data.minimumDuration;
        stats.maximum = data.maximumDuration;
        
        // Count valid samples for display
        size_t validSamples = 0;
        for (const auto& sample : data.samples)
        {
            if (sample.count() > 0)
            {
                validSamples++;
            }
        }
        stats.sampleCount = validSamples;
        stats.average = calculateMovingAverage(data);
        stats.lastUpdated = data.lastUpdated;
        return true;
    }
    return false;
}

std::unordered_map<std::string, Profiler::TimerStatistics> Profiler::getAllTimerStatistics() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, TimerStatistics> result;

    for (const auto& pair : timingData_)
    {
        if (!pair.second.isActive)
        {
            TimerStatistics stats;
            const auto& data = pair.second;
            stats.current = data.duration;
            stats.minimum = data.minimumDuration == std::chrono::microseconds::max() ? std::chrono::microseconds{0}
                                                                                     : data.minimumDuration;
            stats.maximum = data.maximumDuration;
            
            // Count valid samples for display
            size_t validSamples = 0;
            for (const auto& sample : data.samples)
            {
                if (sample.count() > 0)
                {
                    validSamples++;
                }
            }
            stats.sampleCount = validSamples;
            stats.average = calculateMovingAverage(data);
            stats.lastUpdated = data.lastUpdated;
            result[pair.first] = stats;
        }
    }

    return result;
}

std::unordered_map<std::string, Profiler::TimerStatistics>
Profiler::getTimerStatisticsBySource(const std::string& sourceName) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, TimerStatistics> result;
    const std::string prefix = sourceName + "::";

    for (const auto& pair : timingData_)
    {
        if (!pair.second.isActive && pair.first.size() >= prefix.size()
            && pair.first.substr(0, prefix.size()) == prefix)
        {
            const std::string operationName = pair.first.substr(prefix.length());
            TimerStatistics stats;
            const auto& data = pair.second;
            stats.current = data.duration;
            stats.minimum = data.minimumDuration == std::chrono::microseconds::max() ? std::chrono::microseconds{0}
                                                                                     : data.minimumDuration;
            stats.maximum = data.maximumDuration;
            
            // Count valid samples for display
            size_t validSamples = 0;
            for (const auto& sample : data.samples)
            {
                if (sample.count() > 0)
                {
                    validSamples++;
                }
            }
            stats.sampleCount = validSamples;
            stats.average = calculateMovingAverage(data);
            stats.lastUpdated = data.lastUpdated;
            result[operationName] = stats;
        }
    }

    return result;
}

void Profiler::configureStatistics(size_t windowSize)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    windowSize_ = windowSize;

    // Reset all sample buffers to match new window size
    for (auto& pair : timingData_)
    {
        auto& data = pair.second;
        data.samples.clear();
        data.samples.resize(windowSize_, std::chrono::microseconds{0});
        data.sampleIndex = 0;
        data.minimumDuration = std::chrono::microseconds::max();
        data.maximumDuration = std::chrono::microseconds{0};
    }
}

void Profiler::resetStatistics()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    
    // Reset statistics for all timers but keep the timer names
    for (auto& pair : timingData_)
    {
        auto& data = pair.second;
        data.samples.clear();
        data.samples.resize(windowSize_, std::chrono::microseconds{0});
        data.sampleIndex = 0;
        data.minimumDuration = std::chrono::microseconds::max();
        data.maximumDuration = std::chrono::microseconds{0};
    }
}

void Profiler::reset()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    timingData_.clear();
}

void Profiler::cleanupStaleData()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::high_resolution_clock::now();
    const auto staleThreshold = std::chrono::seconds(3);

    std::vector<std::string> keysToRemove;

    // Find stale entries (only check completed timings, not active ones)
    for (const auto& pair : timingData_)
    {
        const std::string& key = pair.first;
        const auto& data = pair.second;

        // Only remove inactive entries that are stale
        if (!data.isActive && (now - data.lastUpdated > staleThreshold))
        {
            keysToRemove.push_back(key);
        }
    }

    // Log and remove stale entries
    for (const std::string& key : keysToRemove)
    {
        auto it = timingData_.find(key);
        if (it != timingData_.end())
        {
            linuxface::common::logWarn("Profiler: Removing stale data for '%s' - Last value: %s", key.c_str(),
                                       formatDuration(it->second.duration).c_str());
            timingData_.erase(it);
        }
    }
}

void Profiler::update()
{
    auto now = std::chrono::high_resolution_clock::now();

    // Initialize lastCleanup_ on first call
    if (lastCleanup_.time_since_epoch().count() == 0)
    {
        lastCleanup_ = now;
        return;
    }

    // Check if cleanup interval has elapsed
    if (now - lastCleanup_ >= CLEANUP_INTERVAL)
    {
        cleanupStaleData();
        lastCleanup_ = now;
    }
}

void Profiler::printSummary() const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    if (timingData_.empty())
    {
        linuxface::common::logInfo("Profiler: No profiling data available");
        return;
    }

    linuxface::common::logInfo("=== PROFILER SUMMARY ===");

    // Group by source
    std::unordered_map<std::string, std::vector<std::pair<std::string, TimerStatistics>>> sourceGroups;

    for (const auto& pair : timingData_)
    {
        const std::string& fullKey = pair.first;
        const auto& data = pair.second;

        // Skip active timers
        if (data.isActive)
        {
            continue;
        }

        // Parse source and name from key (format: "source::name")
        size_t separatorPos = fullKey.find("::");
        if (separatorPos == std::string::npos)
        {
            continue;
        }

        std::string source = fullKey.substr(0, separatorPos);
        std::string name = fullKey.substr(separatorPos + 2);

        TimerStatistics stats;
        stats.current = data.duration;
        stats.minimum = data.minimumDuration;
        stats.maximum = data.maximumDuration;
        stats.average = calculateMovingAverage(data);
        stats.sampleCount = data.samples.size();
        stats.lastUpdated = data.lastUpdated;

        sourceGroups[source].emplace_back(name, stats);
    }

    // Print each source group
    for (const auto& sourcePair : sourceGroups)
    {
        const std::string& source = sourcePair.first;
        const auto& timers = sourcePair.second;

        linuxface::common::logInfo("Source: %s", source.c_str());

        // Sort timers by average duration (descending)
        std::vector<std::pair<std::string, TimerStatistics>> sortedTimers = timers;
        std::sort(sortedTimers.begin(), sortedTimers.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.average > b.second.average;
                  });

        for (const auto& timerPair : sortedTimers)
        {
            const std::string& name = timerPair.first;
            const TimerStatistics& stats = timerPair.second;

            linuxface::common::logInfo("  %-30s | Current: %-12s | Average: %-12s | Min: %-12s | Max: %-12s | Samples: %zu",
                                       name.c_str(),
                                       formatDuration(stats.current).c_str(),
                                       formatDuration(stats.average).c_str(),
                                       formatDuration(stats.minimum).c_str(),
                                       formatDuration(stats.maximum).c_str(),
                                       stats.sampleCount);
        }
        linuxface::common::logInfo("");
    }

    linuxface::common::logInfo("=== END PROFILER SUMMARY ===");
}

void Profiler::setColorRange(const std::string& timerKey, std::chrono::microseconds greenThreshold, 
                             std::chrono::microseconds redThreshold)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    colorRanges_[timerKey] = ColorRange(greenThreshold, redThreshold);
}

void Profiler::setColorRange(const std::string& sourceName, const std::string& name,
                             std::chrono::microseconds greenThreshold, std::chrono::microseconds redThreshold)
{
    setColorRange(makeKey(sourceName, name), greenThreshold, redThreshold);
}

Profiler::ColorRange Profiler::getColorRange(const std::string& timerKey) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = colorRanges_.find(timerKey);
    if (it != colorRanges_.end())
    {
        return it->second;
    }
    // Return default color range if not configured
    return ColorRange();
}

void Profiler::resetColorRanges()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    colorRanges_.clear();
}
