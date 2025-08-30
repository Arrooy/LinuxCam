#ifndef PROFILER_H
#define PROFILER_H
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace linuxface
{
// TODO(runner): Lets also use an enum to define type of profiling, e.g. CPU,
// memory, etc.
//  We can show profiling data ordered and clustered by type.
class Profiler
{
  public:
    ~Profiler() = default;

    // Disable copy constructor and assignment operator
    Profiler(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    Profiler& operator=(Profiler&&) = delete;

    static Profiler& getInstance() noexcept
    {
        static Profiler instance;
        return instance;
    }
    // TODO(runner): Instead of storing last time, store list and average? Min,
    // max? refresh every x seconds configurable.
    void start(const std::string& sourceName, const std::string& name);
    void stop(const std::string& sourceName, const std::string& name);

    bool duration(const std::string& sourceName, const std::string& name, std::chrono::microseconds& duration) const;
    const std::unordered_map<std::string, std::chrono::microseconds>& getDurations() const;

    /**
     * Returns all durations for a given source name.
     */
    std::unordered_map<std::string, std::chrono::microseconds> getDurations(const std::string& sourceName) const;

    std::vector<std::pair<std::string, std::chrono::microseconds>> getDurationsSorted() const;

    static std::string formatDuration(std::chrono::microseconds duration) noexcept;
    static std::string formatDuration(int64_t micros) noexcept;
    static std::string formatDuration(std::chrono::high_resolution_clock::time_point start,
                                      std::chrono::high_resolution_clock::time_point end) noexcept;

    void reset();

  private:
    Profiler() = default;

    static std::string makeKey(const std::string& sourceName, const std::string& name);

    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> timers_;
    std::unordered_map<std::string, std::chrono::microseconds> durations_;
    mutable std::mutex mutex_;
};

inline std::string Profiler::formatDuration(int64_t micros) noexcept
{
    char buffer[64];

    if (micros < 0)
    {
        if (snprintf(buffer, sizeof(buffer), "Invalid duration") == -1)
        {
            return "Error formatting duration";
        }
    }
    else if (micros < 1000)
    {
        if (snprintf(buffer, sizeof(buffer), "%ld µs", micros) == -1)
        {
            return "Error formatting duration";
        }
    }
    else if (micros < 1000 * 1000)
    {
        const double ms = micros / 1000.0;
        const double hz = 1e6 / micros;
        if (snprintf(buffer, sizeof(buffer), "%.2f ms (%.2f Hz)", ms, hz) == -1)
        {
            return "Error formatting duration";
        }
    }
    else if (micros < static_cast<int64_t>(60) * 1000 * 1000)
    {
        const double s = micros / 1e6;
        const double hz = 1e6 / micros;
        if (snprintf(buffer, sizeof(buffer), "%.2f s (%.2f Hz)", s, hz) == -1)
        {
            return "Error formatting duration";
        }
    }
    else
    {
        const int64_t totalSeconds = micros / 1000000;
        const int64_t minutes = totalSeconds / 60;
        const int64_t seconds = totalSeconds % 60;
        const double hz = 1e6 / micros;
        if (snprintf(buffer, sizeof(buffer), "%ld min %ld s (%.4f Hz)", minutes, seconds, hz) == -1)
        {
            return "Error formatting duration";
        }
    }

    return std::string(buffer);
}

inline std::string Profiler::formatDuration(std::chrono::high_resolution_clock::time_point start,
                                            std::chrono::high_resolution_clock::time_point end) noexcept
{
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return Profiler::formatDuration(duration);
}

inline std::string Profiler::formatDuration(std::chrono::microseconds duration) noexcept
{
    return Profiler::formatDuration(duration.count());
}

} // namespace linuxface

#endif // PROFILER_H
