#ifndef PROFILER_H
#define PROFILER_H
#include <atomic>
#include <chrono>
#include <deque>
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

    /**
     * Get a single duration for a specific timer.
     * @param sourceName The source name of the timer
     * @param name The name of the timer
     * @param duration Output parameter to store the duration if found
     * @return true if timer was found, false otherwise
     */
    bool getDuration(const std::string& sourceName, const std::string& name, std::chrono::microseconds& duration) const;

    /**
     * Get all durations as a map (full keys: "source::name" -> duration).
     * @return Map of full timer keys to durations
     */
    std::unordered_map<std::string, std::chrono::microseconds> getAllDurations() const;

    /**
     * Get durations as a map for a specific source (operation names only).
     * @param sourceName The source name to filter by
     * @return Map of operation names to durations (without source prefix)
     */
    std::unordered_map<std::string, std::chrono::microseconds>
    getDurationsBySource(const std::string& sourceName) const;

    /**
     * Get durations sorted by value for debugging/display purposes.
     * @param sourceName Source filter (empty string = all sources)
     * @return Vector of name-duration pairs sorted by duration (descending)
     */
    std::vector<std::pair<std::string, std::chrono::microseconds>>
    getDurationsSorted(const std::string& sourceName = "") const;

    static std::string formatDuration(std::chrono::microseconds duration) noexcept;
    static std::string formatDuration(int64_t micros) noexcept;
    static std::string formatDuration(std::chrono::high_resolution_clock::time_point start,
                                      std::chrono::high_resolution_clock::time_point end) noexcept;

    void reset();

    /**
     * Cleans up stale profiling data that hasn't been updated for more than 3 seconds.
     * Logs all deleted data with their last values before deletion.
     */
    void cleanupStaleData();

    /**
     * Print a summary of all profiling data to the console.
     */
    void printSummary() const;

    /**
     * Updates the profiler, handling periodic cleanup automatically.
     * Should be called regularly from the main application loop.
     */
    void update();

    /**
     * Statistical data for a timer over a time window.
     */
    struct TimerStatistics
    {
        std::chrono::microseconds current{0}; // Most recent measurement
        std::chrono::microseconds minimum{0}; // Minimum value in window
        std::chrono::microseconds maximum{0}; // Maximum value in window
        std::chrono::microseconds average{0}; // Average value in window
        size_t sampleCount{0};                // Number of samples in current window
        std::chrono::high_resolution_clock::time_point lastUpdated{};
    };

    /**
     * Color range configuration for profiler display.
     * Defines the thresholds for green (good) and red (bad) performance.
     */
    struct ColorRange
    {
        std::chrono::microseconds greenThreshold{1000}; // 1ms - good performance (green)
        std::chrono::microseconds redThreshold{10000};  // 10ms - poor performance (red)

        ColorRange() = default;
        ColorRange(std::chrono::microseconds green, std::chrono::microseconds red)
            : greenThreshold(green), redThreshold(red)
        {
        }
    };

    /**
     * Get statistical data for a specific timer.
     * @param sourceName The source name of the timer
     * @param name The name of the timer
     * @param stats Output parameter to store the statistics if found
     * @return true if timer was found, false otherwise
     */
    bool getTimerStatistics(const std::string& sourceName, const std::string& name, TimerStatistics& stats) const;

    /**
     * Get all timer statistics as a map.
     * @return Map of full timer keys to statistics
     */
    std::unordered_map<std::string, TimerStatistics> getAllTimerStatistics() const;

    /**
     * Get timer statistics for a specific source.
     * @param sourceName The source name to filter by
     * @return Map of operation names to statistics
     */
    std::unordered_map<std::string, TimerStatistics> getTimerStatisticsBySource(const std::string& sourceName) const;

    /**
     * Configure the statistics collection window.
     * @param windowSize Size of the moving average window (default: 100 samples)
     */
    void configureStatistics(size_t windowSize = 100);

    /**
     * Reset all statistics, clearing all collected samples.
     */
    void resetStatistics();

    /**
     * Configure color range for a specific timer key.
     * @param timerKey Full timer key (format: "source::name")
     * @param greenThreshold Duration threshold for good performance (green color)
     * @param redThreshold Duration threshold for poor performance (red color)
     */
    void setColorRange(const std::string& timerKey, std::chrono::microseconds greenThreshold,
                       std::chrono::microseconds redThreshold);

    /**
     * Configure color range for a specific timer.
     * @param sourceName The source name of the timer
     * @param name The name of the timer
     * @param greenThreshold Duration threshold for good performance (green color)
     * @param redThreshold Duration threshold for poor performance (red color)
     */
    void setColorRange(const std::string& sourceName, const std::string& name, std::chrono::microseconds greenThreshold,
                       std::chrono::microseconds redThreshold);

    /**
     * Get color range for a specific timer key.
     * @param timerKey Full timer key (format: "source::name")
     * @return ColorRange for the timer, or default range if not configured
     */
    ColorRange getColorRange(const std::string& timerKey) const;

    /**
     * Reset color ranges to defaults for all timers.
     */
    void resetColorRanges();

    /**
     * Event structure for hierarchical profiling.
     */
    struct ProfileEvent
    {
        std::string name;
        std::string event; // "start" or "end"
        int64_t timestamp_ms;
        uint64_t thread_id{0};
    };

    /**
     * Node in the call tree hierarchy.
     */
    struct CallTreeNode
    {
        std::string name;
        int64_t inclusive_ms{0};
        int64_t exclusive_ms{0};
        std::vector<CallTreeNode> children;

        CallTreeNode() = default;
        CallTreeNode(std::string n) : name(std::move(n)) {}

        /**
         * Convert the call tree to JSON string representation.
         * @return JSON string of the call tree
         */
        std::string toJson() const;
    };

    /**
     * Get the current call tree hierarchy from collected events.
     * @return Const reference to the current call tree
     */
    const CallTreeNode& getCurrentCallTree() const;

    /**
     * Force an immediate rebuild of the call tree from collected events.
     * This is intended for on-demand UI actions (e.g. a "Build" button).
     * It is safe to call from the main thread; it acquires the profiler
     * mutex and updates the cached tree.
     */
    void forceRebuildCallTree();

    /**
     * Reset the collected profiling events.
     */
    void resetCallTree();

    /**
     * Start capturing profiling events for a single-loop snapshot.
     * Clears any previously collected events to start fresh.
     * Call this at the beginning of the loop you want to profile.
     */
    void startLoopCapture();

    /**
     * End single-loop capture and build the call tree from events collected since startLoopCapture().
     * This builds the tree and clears events, so the tree represents only the captured loop.
     * Call this at the end of the loop you want to profile.
     */
    void endLoopCapture();

    /**
     * Build a hierarchical call tree from a list of profile events.
     * @param events List of profile events in chronological order
     * @return Root node of the call tree
     */
    static CallTreeNode buildProfileHierarchy(const std::deque<ProfileEvent>& events);

    /**
     * A small RAII helper for scoped profiling spans. Usage:
     * {
     *   ScopedProfilerSpan span("Source", "Operation");
     *   // work
     * }
     * The destructor will call stop() automatically.
     */
    class ScopedProfilerSpan
    {
      public:
        ScopedProfilerSpan(const std::string& source, const std::string& name)
            : source_(source), name_(name), moved_(false)
        {
            Profiler::getInstance().start(source_, name_);
        }

        // Non-copyable
        ScopedProfilerSpan(const ScopedProfilerSpan&) = delete;
        ScopedProfilerSpan& operator=(const ScopedProfilerSpan&) = delete;

        // Movable
        ScopedProfilerSpan(ScopedProfilerSpan&& other) noexcept
            : source_(std::move(other.source_)), name_(std::move(other.name_)), moved_(other.moved_)
        {
            other.moved_ = true;
        }

        ScopedProfilerSpan& operator=(ScopedProfilerSpan&& other) noexcept
        {
            if (this != &other)
            {
                source_ = std::move(other.source_);
                name_ = std::move(other.name_);
                moved_ = other.moved_;
                other.moved_ = true;
            }
            return *this;
        }

        ~ScopedProfilerSpan()
        {
            if (!moved_)
            {
                try
                {
                    Profiler::getInstance().stop(source_, name_);
                }
                catch (...)
                {
                }
            }
        }

        // Manually stop early
        void stop()
        {
            if (!moved_)
            {
                Profiler::getInstance().stop(source_, name_);
                moved_ = true;
            }
        }

      private:
        std::string source_;
        std::string name_;
        bool moved_{false};
    };

  private:
    Profiler() = default;

    struct TimingData
    {
        std::chrono::microseconds duration{0};
        std::chrono::high_resolution_clock::time_point lastUpdated{};
        std::chrono::high_resolution_clock::time_point startTime{}; // Only used while timing is active
        bool isActive{false};                                       // True when timing is in progress

        // Statistical data collection - moving average window
        std::vector<std::chrono::microseconds> samples;
        std::chrono::microseconds minimumDuration{std::chrono::microseconds::max()};
        std::chrono::microseconds maximumDuration{0};
        size_t sampleIndex{0}; // Current position in circular buffer
    };

    static std::string makeKey(const std::string& sourceName, const std::string& name);

    /**
     * Updates statistical data for a timer with a new sample using moving average.
     */
    void updateStatistics(TimingData& data, std::chrono::microseconds duration);

    /**
     * Calculates the moving average from the current sample window.
     */
    std::chrono::microseconds calculateMovingAverage(const TimingData& data) const;

    std::unordered_map<std::string, TimingData> timingData_;
    mutable std::mutex mutex_;

    // Color range configuration for different timer keys
    std::unordered_map<std::string, ColorRange> colorRanges_;

    // Cleanup timing
    std::chrono::high_resolution_clock::time_point lastCleanup_{};
    static constexpr std::chrono::seconds CleanupInterval{5}; // Check every 5 seconds

    // Statistics configuration
    size_t windowSize_{150}; // Size of the moving average window

    // Hierarchical profiling data - FIFO queue with fixed size
    std::deque<ProfileEvent> collectedEvents_;
    static constexpr size_t MaxCollectedEvents = 750; // Fixed queue size
    CallTreeNode currentCallTree_;
    bool callTreeDirty_{true}; // Flag to track if call tree needs rebuilding

    // Single-loop capture mode
    std::atomic<bool> loopCaptureActive_{false};

    // Profiler self-profiling overhead tracking (microseconds)
    std::atomic<int64_t> profilerOverheadUs_{0};
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
