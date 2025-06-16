#ifndef PROFILER_H
#define PROFILER_H
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace funnyface
{
//TODO: Lets also use an enum to define type of profiling, e.g. CPU, memory, etc.
// We can show profiling data ordered and clustered by type.
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
    // TODO: Instead of storing last time, store list and average? Min, max? refresh every 20sec
    void start(const std::string& sourceName, const std::string& name);
    void stop(const std::string& sourceName, const std::string& name);

    bool duration(const std::string& sourceName, const std::string& name, std::chrono::microseconds& duration) const;
    const std::unordered_map<std::string, std::chrono::microseconds>& getDurations() const;

    /**
     * Returns all durations for a given source name.
     */
    std::unordered_map<std::string, std::chrono::microseconds> getDurations(const std::string& sourceName) const;

    std::vector<std::pair<std::string, std::chrono::microseconds>> getDurationsSorted() const;


    static std::string format_duration(std::chrono::microseconds duration) noexcept;
    static std::string format_duration(int64_t micros) noexcept;
    static std::string format_duration(std::chrono::high_resolution_clock::time_point start,
                                      std::chrono::high_resolution_clock::time_point end) noexcept;
  private:
    Profiler() = default;

    std::string makeKey(const std::string& sourceName, const std::string& name) const;

    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> timers_;
    std::unordered_map<std::string, std::chrono::microseconds> durations_;
    mutable std::mutex mutex_;
};


inline std::string Profiler::format_duration(int64_t micros) noexcept
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
        if (micros == 0)
        {
            if (snprintf(buffer, sizeof(buffer), "%ld µs", micros) == -1)
            {
                return "Error formatting duration";
            }
        }
        else{
            double hz = 1e6 / micros;
            if (snprintf(buffer, sizeof(buffer), "%ld µs (%.2f Hz)", micros, hz) == -1)
            {
                return "Error formatting duration";
            }
        }
    }
    else if (micros < 1000 * 1000)
    {
        double ms = micros / 1000.0;
        double hz = 1e6 / micros;
        if (snprintf(buffer, sizeof(buffer), "%.2f ms (%.2f Hz)", ms, hz) == -1)
        {
            return "Error formatting duration";
        }
    }
    else if (micros < int64_t(60) * 1000 * 1000)
    {
        double s = micros / 1e6;
        double hz = 1e6 / micros;
        if (snprintf(buffer, sizeof(buffer), "%.2f s (%.2f Hz)", s, hz) == -1)
        {
            return "Error formatting duration";
        }
    }
    else
    {
        int64_t total_seconds = micros / 1000000;
        int64_t minutes = total_seconds / 60;
        int64_t seconds = total_seconds % 60;
        double hz = 1e6 / micros;
        if (snprintf(buffer, sizeof(buffer), "%ld min %ld s (%.4f Hz)", minutes, seconds, hz) == -1)
        {
            return "Error formatting duration";
        }
    }

    return std::string(buffer);
}

inline std::string Profiler::format_duration(std::chrono::high_resolution_clock::time_point start,
                                             std::chrono::high_resolution_clock::time_point end) noexcept
{
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return Profiler::format_duration(duration);
}

inline std::string Profiler::format_duration(std::chrono::microseconds duration) noexcept
{
    return Profiler::format_duration(duration.count());
}


} // namespace funnyface

#endif // PROFILER_H
