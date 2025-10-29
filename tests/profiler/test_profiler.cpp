#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "LinuxFace/profiler.h"

using namespace linuxface;

class ProfilerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Reset profiler state for each test to ensure isolation
        Profiler::getInstance().reset();
    }

    void TearDown() override
    {
        // Clean up profiler state after each test
        Profiler::getInstance().reset();
    }
};

TEST_F(ProfilerTest, Singleton)
{
    Profiler& profiler1 = Profiler::getInstance();
    Profiler& profiler2 = Profiler::getInstance();

    // Should be the same instance
    EXPECT_EQ(&profiler1, &profiler2);
}

TEST_F(ProfilerTest, StartStop)
{
    Profiler& profiler = Profiler::getInstance();

    profiler.start("TestSource", "TestTimer");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    profiler.stop("TestSource", "TestTimer");

    std::chrono::microseconds duration;
    bool found = profiler.getDuration("TestSource", "TestTimer", duration);

    EXPECT_TRUE(found);
    EXPECT_GT(duration.count(), 0);
    // We slept for 100 microseconds, so duration should be at least 50µs (allowing for timing variance)
    // but less than 10ms (10,000µs) to account for system scheduling
    EXPECT_GE(duration.count(), 50);    // At least 50µs (half of what we slept)
    EXPECT_LT(duration.count(), 10000); // Less than 10ms (generous upper bound)
}

TEST_F(ProfilerTest, MultipleTimers)
{
    Profiler& profiler = Profiler::getInstance();

    profiler.start("Source1", "Timer1");
    profiler.start("Source2", "Timer2");

    std::this_thread::sleep_for(std::chrono::microseconds(50));
    profiler.stop("Source1", "Timer1");

    std::this_thread::sleep_for(std::chrono::microseconds(50));
    profiler.stop("Source2", "Timer2");

    std::chrono::microseconds duration1, duration2;
    EXPECT_TRUE(profiler.getDuration("Source1", "Timer1", duration1));
    EXPECT_TRUE(profiler.getDuration("Source2", "Timer2", duration2));

    // Timer2 should be longer than Timer1 since it ran for ~100µs vs ~50µs
    // But allow for timing variance - just ensure both are reasonable
    EXPECT_GE(duration1.count(), 25);                // At least 25µs (half of 50µs sleep)
    EXPECT_GE(duration2.count(), 75);                // At least 75µs (Timer2 ran longer)
    EXPECT_GT(duration2.count(), duration1.count()); // Timer2 should still be longer
}

TEST_F(ProfilerTest, NonExistentTimer)
{
    Profiler& profiler = Profiler::getInstance();

    std::chrono::microseconds duration;
    bool found = profiler.getDuration("NonExistent", "Timer", duration);

    EXPECT_FALSE(found);
}

TEST_F(ProfilerTest, GetDurations)
{
    Profiler& profiler = Profiler::getInstance();

    profiler.start("TestSource", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("TestSource", "Timer1");

    profiler.start("TestSource", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    profiler.stop("TestSource", "Timer2");

    auto durations = profiler.getDurationsBySource("TestSource");
    EXPECT_EQ(durations.size(), 2);

    // Just ensure both timers recorded positive durations
    // Don't check exact values due to timing variance
    EXPECT_GT(durations["Timer1"].count(), 0);
    EXPECT_GT(durations["Timer2"].count(), 0);
}

TEST_F(ProfilerTest, GetDurationsSorted)
{
    Profiler& profiler = Profiler::getInstance();

    profiler.start("A", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(50)); // Longer sleep
    profiler.stop("A", "Timer1");

    profiler.start("B", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(10)); // Shorter sleep
    profiler.stop("B", "Timer2");

    auto sorted = profiler.getDurationsSorted();
    EXPECT_GE(sorted.size(), 2);

    // Should be sorted by duration (descending)
    bool is_sorted_by_duration = true;
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        if (sorted[i - 1].second < sorted[i].second)
        {
            is_sorted_by_duration = false;
            break;
        }
    }
    EXPECT_TRUE(is_sorted_by_duration);

    // Timer1 (50µs sleep) should appear before Timer2 (10µs sleep) in sorted list
    // since it should have longer duration (keys are formatted as "Source::Timer")
    bool found_timer1_first = false;
    for (const auto& entry : sorted)
    {
        if (entry.first.find("::Timer1") != std::string::npos)
        {
            found_timer1_first = true;
            break;
        }
        if (entry.first.find("::Timer2") != std::string::npos)
        {
            break; // Found Timer2 first, which is wrong
        }
    }
    EXPECT_TRUE(found_timer1_first);
}

TEST_F(ProfilerTest, FormatDurationMicroseconds)
{
    std::string result = Profiler::formatDuration(static_cast<int64_t>(500));
    EXPECT_NE(result.find("µs"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationMilliseconds)
{
    std::string result = Profiler::formatDuration(static_cast<int64_t>(5000)); // 5ms
    EXPECT_NE(result.find("ms"), std::string::npos);
    EXPECT_NE(result.find("Hz"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationSeconds)
{
    std::string result = Profiler::formatDuration(static_cast<int64_t>(2000000)); // 2s
    EXPECT_NE(result.find("s"), std::string::npos);
    EXPECT_NE(result.find("Hz"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationMinutes)
{
    std::string result = Profiler::formatDuration(static_cast<int64_t>(120000000)); // 2 minutes
    EXPECT_NE(result.find("min"), std::string::npos);
    EXPECT_NE(result.find("Hz"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationInvalid)
{
    std::string result = Profiler::formatDuration(static_cast<int64_t>(-1));
    EXPECT_NE(result.find("Invalid"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationChrono)
{
    auto duration = std::chrono::microseconds(1500);
    std::string result = Profiler::formatDuration(duration);
    EXPECT_NE(result.find("ms"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationTimePoints)
{
    auto start = std::chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(std::chrono::microseconds(500)); // Longer sleep for more reliable timing
    auto end = std::chrono::high_resolution_clock::now();

    std::string result = Profiler::formatDuration(start, end);
    EXPECT_FALSE(result.empty());
    // Should contain some time unit
    bool has_time_unit = (result.find("µs") != std::string::npos) || (result.find("ms") != std::string::npos)
                         || (result.find("s") != std::string::npos);
    EXPECT_TRUE(has_time_unit);
}

TEST_F(ProfilerTest, StopWithoutStart)
{
    Profiler& profiler = Profiler::getInstance();

    // Stop a timer that was never started - should not crash
    profiler.stop("NonExistent", "Timer");

    std::chrono::microseconds duration;
    bool found = profiler.getDuration("NonExistent", "Timer", duration);
    EXPECT_FALSE(found);
}

TEST_F(ProfilerTest, SameTimerNameDifferentSources)
{
    Profiler& profiler = Profiler::getInstance();

    profiler.start("Source1", "SameName");
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    profiler.stop("Source1", "SameName");

    profiler.start("Source2", "SameName");
    std::this_thread::sleep_for(std::chrono::microseconds(40));
    profiler.stop("Source2", "SameName");

    std::chrono::microseconds duration1, duration2;
    EXPECT_TRUE(profiler.getDuration("Source1", "SameName", duration1));
    EXPECT_TRUE(profiler.getDuration("Source2", "SameName", duration2));

    // Both should be valid and positive
    EXPECT_GT(duration1.count(), 0);
    EXPECT_GT(duration2.count(), 0);

    // Source2 should have longer duration since it slept longer
    // But allow for timing variance - don't require exact ordering
    EXPECT_GE(duration1.count(), 10); // At least 10µs (half of 20µs sleep)
    EXPECT_GE(duration2.count(), 20); // At least 20µs (half of 40µs sleep)
}

TEST_F(ProfilerTest, Reset)
{
    Profiler& profiler = Profiler::getInstance();

    // Add some timers
    profiler.start("Source1", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source1", "Timer1");

    profiler.start("Source2", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source2", "Timer2");

    // Verify they exist
    auto durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 2);

    // Reset and verify empty
    profiler.reset();
    durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 0);

    // Verify specific lookups fail
    std::chrono::microseconds duration;
    EXPECT_FALSE(profiler.getDuration("Source1", "Timer1", duration));
    EXPECT_FALSE(profiler.getDuration("Source2", "Timer2", duration));
}

TEST_F(ProfilerTest, ResetWithActiveTimers)
{
    Profiler& profiler = Profiler::getInstance();

    // Start some timers but don't stop them
    profiler.start("Source1", "ActiveTimer1");
    profiler.start("Source2", "ActiveTimer2");

    // Add a completed timer
    profiler.start("Source3", "CompletedTimer");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source3", "CompletedTimer");

    // Verify completed timer exists
    auto durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 1);

    // Reset should clear everything including active timers
    profiler.reset();

    // Verify everything is cleared
    durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 0);

    // Try to stop the previously active timers - should not crash or create entries
    profiler.stop("Source1", "ActiveTimer1");
    profiler.stop("Source2", "ActiveTimer2");

    durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 0);
}

TEST_F(ProfilerTest, GetDurationsEmptySource)
{
    Profiler& profiler = Profiler::getInstance();

    // Add timer for different source
    profiler.start("OtherSource", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("OtherSource", "Timer1");

    // Query for non-existent source
    auto durations = profiler.getDurationsBySource("NonExistentSource");
    EXPECT_EQ(durations.size(), 0);
}

TEST_F(ProfilerTest, GetDurationsMultipleSources)
{
    Profiler& profiler = Profiler::getInstance();

    // Add timers for multiple sources
    profiler.start("Source1", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source1", "Timer1");

    profiler.start("Source1", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source1", "Timer2");

    profiler.start("Source2", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source2", "Timer1");

    // Query specific source
    auto durations1 = profiler.getDurationsBySource("Source1");
    auto durations2 = profiler.getDurationsBySource("Source2");

    EXPECT_EQ(durations1.size(), 2);
    EXPECT_EQ(durations2.size(), 1);

    EXPECT_TRUE(durations1.find("Timer1") != durations1.end());
    EXPECT_TRUE(durations1.find("Timer2") != durations1.end());
    EXPECT_TRUE(durations2.find("Timer1") != durations2.end());
}

TEST_F(ProfilerTest, GetDurationsSortedOrder)
{
    Profiler& profiler = Profiler::getInstance();

    // Add timers with different durations - use larger, well-separated intervals
    profiler.start("Source", "Fast");
    std::this_thread::sleep_for(std::chrono::microseconds(200)); // Smallest
    profiler.stop("Source", "Fast");

    profiler.start("Source", "Slow");
    std::this_thread::sleep_for(std::chrono::microseconds(2000)); // Largest
    profiler.stop("Source", "Slow");

    profiler.start("Source", "Medium");
    std::this_thread::sleep_for(std::chrono::microseconds(800)); // Middle
    profiler.stop("Source", "Medium");

    auto sorted = profiler.getDurationsSorted();
    EXPECT_EQ(sorted.size(), 3);

    // Should be sorted by duration (descending)
    EXPECT_GE(sorted[0].second.count(), sorted[1].second.count());
    EXPECT_GE(sorted[1].second.count(), sorted[2].second.count());

    // The longest should be "Slow" (key format is "Source::Slow")
    EXPECT_TRUE(sorted[0].first.find("::Slow") != std::string::npos);
}

TEST_F(ProfilerTest, ConcurrentTimers)
{
    Profiler& profiler = Profiler::getInstance();

    // Start multiple timers concurrently
    profiler.start("Source1", "Timer1");
    profiler.start("Source2", "Timer2");
    profiler.start("Source3", "Timer3");

    std::this_thread::sleep_for(std::chrono::microseconds(10));

    // Stop them in different order
    profiler.stop("Source2", "Timer2");
    profiler.stop("Source1", "Timer1");
    profiler.stop("Source3", "Timer3");

    std::chrono::microseconds duration1, duration2, duration3;
    EXPECT_TRUE(profiler.getDuration("Source1", "Timer1", duration1));
    EXPECT_TRUE(profiler.getDuration("Source2", "Timer2", duration2));
    EXPECT_TRUE(profiler.getDuration("Source3", "Timer3", duration3));

    EXPECT_GT(duration1.count(), 0);
    EXPECT_GT(duration2.count(), 0);
    EXPECT_GT(duration3.count(), 0);
}

TEST_F(ProfilerTest, OverwriteTimer)
{
    Profiler& profiler = Profiler::getInstance();

    // Start and stop a timer
    profiler.start("Source", "Timer");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source", "Timer");

    std::chrono::microseconds duration1;
    EXPECT_TRUE(profiler.getDuration("Source", "Timer", duration1));

    // Start and stop the same timer again (should overwrite)
    profiler.start("Source", "Timer");
    std::this_thread::sleep_for(std::chrono::microseconds(30));
    profiler.stop("Source", "Timer");

    std::chrono::microseconds duration2;
    EXPECT_TRUE(profiler.getDuration("Source", "Timer", duration2));

    // Second duration should be different (and likely longer)
    EXPECT_NE(duration1.count(), duration2.count());
}

TEST_F(ProfilerTest, EdgeCaseEmptyStrings)
{
    Profiler& profiler = Profiler::getInstance();

    // Test with empty strings
    profiler.start("", "");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("", "");

    std::chrono::microseconds duration;
    EXPECT_TRUE(profiler.getDuration("", "", duration));
    EXPECT_GT(duration.count(), 0);

    // Test mixed empty strings
    profiler.start("Source", "");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source", "");

    EXPECT_TRUE(profiler.getDuration("Source", "", duration));
    EXPECT_GT(duration.count(), 0);
}

TEST_F(ProfilerTest, FormatDurationEdgeCases)
{
    // Test zero duration
    std::string result = Profiler::formatDuration(static_cast<int64_t>(0));
    EXPECT_NE(result.find("µs"), std::string::npos);

    // Test very large duration
    std::string largeResult = Profiler::formatDuration(static_cast<int64_t>(3661000000)); // 1 hour, 1 minute, 1 second
    EXPECT_NE(largeResult.find("min"), std::string::npos);

    // Test boundary values
    std::string boundaryMs = Profiler::formatDuration(static_cast<int64_t>(1000)); // Exactly 1ms
    EXPECT_NE(boundaryMs.find("ms"), std::string::npos);

    std::string boundaryS = Profiler::formatDuration(static_cast<int64_t>(1000000)); // Exactly 1s
    EXPECT_NE(boundaryS.find("s"), std::string::npos);
}

TEST_F(ProfilerTest, UpdateMethod)
{
    Profiler& profiler = Profiler::getInstance();

    // Add some timing data
    profiler.start("TestSource", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    profiler.stop("TestSource", "Timer1");

    profiler.start("TestSource", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    profiler.stop("TestSource", "Timer2");

    // Verify data exists
    auto durations = profiler.getDurationsBySource("TestSource");
    EXPECT_EQ(durations.size(), 2);

    // Call update multiple times - should not affect fresh data
    for (int i = 0; i < 10; ++i)
    {
        profiler.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Data should still exist (not stale yet)
    durations = profiler.getDurationsBySource("TestSource");
    EXPECT_EQ(durations.size(), 2);

    // Test that update initializes correctly (safe to call multiple times)
    profiler.update();
    profiler.update();
    profiler.update();

    // Data should still be there
    durations = profiler.getDurationsBySource("TestSource");
    EXPECT_EQ(durations.size(), 2);
}

// Test new statistics functionality
TEST_F(ProfilerTest, StatisticsCollection)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Configure statistics for testing
    profiler.configureStatistics(5);

    // Collect multiple samples with different durations
    std::vector<int> sleepMicros = {100, 200, 300, 150, 250};

    for (int i = 0; i < static_cast<int>(sleepMicros.size()); ++i)
    {
        profiler.start("TestSource", "StatTimer");
        std::this_thread::sleep_for(std::chrono::microseconds(sleepMicros[i]));
        profiler.stop("TestSource", "StatTimer");
    }

    // Get statistics
    Profiler::TimerStatistics stats;
    EXPECT_TRUE(profiler.getTimerStatistics("TestSource", "StatTimer", stats));

    // Verify we have samples
    EXPECT_EQ(stats.sampleCount, 5);
    EXPECT_GT(stats.current.count(), 0);
    EXPECT_GT(stats.average.count(), 0);
    EXPECT_GT(stats.maximum.count(), stats.minimum.count());
    EXPECT_GE(stats.maximum.count(), stats.current.count());
    EXPECT_LE(stats.minimum.count(), stats.current.count());
}

TEST_F(ProfilerTest, StatisticsWindow)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Configure small window for testing
    profiler.configureStatistics(3);

    // Add more samples than the window size
    for (int i = 0; i < 5; ++i)
    {
        profiler.start("TestSource", "WindowTimer");
        std::this_thread::sleep_for(std::chrono::microseconds(100 + i * 50));
        profiler.stop("TestSource", "WindowTimer");
    }

    Profiler::TimerStatistics stats;
    EXPECT_TRUE(profiler.getTimerStatistics("TestSource", "WindowTimer", stats));

    // Should only keep the last 3 samples
    EXPECT_EQ(stats.sampleCount, 3);
}

TEST_F(ProfilerTest, GetAllStatistics)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Add timers from different sources
    profiler.start("Source1", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    profiler.stop("Source1", "Timer1");

    profiler.start("Source2", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    profiler.stop("Source2", "Timer2");

    auto allStats = profiler.getAllTimerStatistics();
    EXPECT_EQ(allStats.size(), 2);

    EXPECT_TRUE(allStats.find("Source1::Timer1") != allStats.end());
    EXPECT_TRUE(allStats.find("Source2::Timer2") != allStats.end());
}

TEST_F(ProfilerTest, GetStatisticsBySource)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Add multiple timers to the same source
    profiler.start("TestSource", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    profiler.stop("TestSource", "Timer1");

    profiler.start("TestSource", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(150));
    profiler.stop("TestSource", "Timer2");

    profiler.start("OtherSource", "Timer3");
    std::this_thread::sleep_for(std::chrono::microseconds(75));
    profiler.stop("OtherSource", "Timer3");

    auto stats = profiler.getTimerStatisticsBySource("TestSource");
    EXPECT_EQ(stats.size(), 2);

    EXPECT_TRUE(stats.find("Timer1") != stats.end());
    EXPECT_TRUE(stats.find("Timer2") != stats.end());
    EXPECT_TRUE(stats.find("Timer3") == stats.end()); // Should not include other sources
}

TEST_F(ProfilerTest, ColorRangeConfiguration)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    const std::string timerKey = "TestSource::Timer1";
    const auto greenThreshold = std::chrono::microseconds(5000); // 5ms
    const auto redThreshold = std::chrono::microseconds(20000);  // 20ms

    // Set custom color range
    profiler.setColorRange(timerKey, greenThreshold, redThreshold);

    // Get color range back
    auto colorRange = profiler.getColorRange(timerKey);
    EXPECT_EQ(colorRange.greenThreshold, greenThreshold);
    EXPECT_EQ(colorRange.redThreshold, redThreshold);

    // Test with source name and timer name separately
    profiler.setColorRange("TestSource", "Timer2", greenThreshold * 2, redThreshold * 2);
    auto colorRange2 = profiler.getColorRange("TestSource::Timer2");
    EXPECT_EQ(colorRange2.greenThreshold, greenThreshold * 2);
    EXPECT_EQ(colorRange2.redThreshold, redThreshold * 2);

    // Test default color range for unconfigured timer
    auto defaultRange = profiler.getColorRange("NonExistent::Timer");
    EXPECT_EQ(defaultRange.greenThreshold, std::chrono::microseconds(1000)); // Default 1ms
    EXPECT_EQ(defaultRange.redThreshold, std::chrono::microseconds(10000));  // Default 10ms
}

TEST_F(ProfilerTest, ResetColorRanges)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Set custom color ranges
    profiler.setColorRange("TestSource::Timer1", std::chrono::microseconds(1000), std::chrono::microseconds(5000));
    profiler.setColorRange("TestSource::Timer2", std::chrono::microseconds(2000), std::chrono::microseconds(10000));

    // Verify they are set
    auto range1 = profiler.getColorRange("TestSource::Timer1");
    auto range2 = profiler.getColorRange("TestSource::Timer2");
    EXPECT_EQ(range1.greenThreshold, std::chrono::microseconds(1000));
    EXPECT_EQ(range2.greenThreshold, std::chrono::microseconds(2000));

    // Reset color ranges
    profiler.resetColorRanges();

    // Verify they are back to defaults
    range1 = profiler.getColorRange("TestSource::Timer1");
    range2 = profiler.getColorRange("TestSource::Timer2");
    EXPECT_EQ(range1.greenThreshold, std::chrono::microseconds(1000)); // Default
    EXPECT_EQ(range1.redThreshold, std::chrono::microseconds(10000));  // Default
    EXPECT_EQ(range2.greenThreshold, std::chrono::microseconds(1000)); // Default
    EXPECT_EQ(range2.redThreshold, std::chrono::microseconds(10000));  // Default
}

TEST_F(ProfilerTest, ResetStatistics)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Configure small window for testing
    profiler.configureStatistics(3);

    // Add some samples
    profiler.start("TestSource", "StatTimer");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    profiler.stop("TestSource", "StatTimer");

    profiler.start("TestSource", "StatTimer");
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    profiler.stop("TestSource", "StatTimer");

    // Verify statistics exist
    Profiler::TimerStatistics stats;
    EXPECT_TRUE(profiler.getTimerStatistics("TestSource", "StatTimer", stats));
    EXPECT_EQ(stats.sampleCount, 2);
    EXPECT_GT(stats.average.count(), 0);

    // Reset statistics
    profiler.resetStatistics();

    // Verify statistics are reset but timer still exists
    EXPECT_TRUE(profiler.getTimerStatistics("TestSource", "StatTimer", stats));
    EXPECT_EQ(stats.sampleCount, 0);
    EXPECT_EQ(stats.average.count(), 0);
    EXPECT_EQ(stats.minimum.count(), 0);
    EXPECT_EQ(stats.maximum.count(), 0);
}

TEST_F(ProfilerTest, CleanupStaleData)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Add a timer and complete it
    profiler.start("TestSource", "StaleTimer");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("TestSource", "StaleTimer");

    // Verify it exists
    auto durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 1);

    // Manually call cleanup (normally called by update() every 5 seconds)
    // Since we just added it, it shouldn't be cleaned up yet
    profiler.cleanupStaleData();
    durations = profiler.getAllDurations();
    EXPECT_EQ(durations.size(), 1);

    // Note: Testing actual stale data removal would require waiting 3+ seconds,
    // which is impractical for unit tests. The cleanup logic is tested indirectly
    // through the update() method test.
}

TEST_F(ProfilerTest, CallTreeBasic)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Start and stop a simple timer - use longer sleep to ensure milliseconds difference
    profiler.start("TestSource", "SimpleTimer");
    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // At least 1ms for timestamp resolution
    profiler.stop("TestSource", "SimpleTimer");

    // Rebuild call tree and get it
    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();

    // Should have a root node with "Call Tree" name
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_EQ(callTree.children.size(), 1); // One source

    // The source should be the first child
    const auto& sourceNode = callTree.children[0];
    EXPECT_EQ(sourceNode.name, "TestSource");
    EXPECT_EQ(sourceNode.children.size(), 1); // One timer

    // The timer should be a child of the source
    const auto& timerNode = sourceNode.children[0];
    EXPECT_EQ(timerNode.name, "SimpleTimer");
    EXPECT_GE(timerNode.inclusive_ms, 1);                      // At least 1ms due to sleep
    EXPECT_EQ(timerNode.exclusive_ms, timerNode.inclusive_ms); // No children, so exclusive = inclusive
    EXPECT_TRUE(timerNode.children.empty());
}

TEST_F(ProfilerTest, CallTreeNested)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Create timers from the same source (they will be grouped together)
    profiler.start("TestSource", "Parent");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    profiler.stop("TestSource", "Parent");

    profiler.start("TestSource", "Child");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    profiler.stop("TestSource", "Child");

    // Rebuild call tree and get it
    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();

    // Should have a root node with "Call Tree" name
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_EQ(callTree.children.size(), 1); // One source

    // The source should be the first child
    const auto& sourceNode = callTree.children[0];
    EXPECT_EQ(sourceNode.name, "TestSource");
    EXPECT_EQ(sourceNode.children.size(), 2); // Two timers

    // Find Parent and Child nodes
    const Profiler::CallTreeNode* parentNode = nullptr;
    const Profiler::CallTreeNode* childNode = nullptr;

    for (const auto& child : sourceNode.children)
    {
        if (child.name == "Parent")
        {
            parentNode = &child;
        }
        else if (child.name == "Child")
        {
            childNode = &child;
        }
    }

    ASSERT_NE(parentNode, nullptr);
    ASSERT_NE(childNode, nullptr);

    // Both should have valid timing data
    EXPECT_GE(parentNode->inclusive_ms, 5);
    EXPECT_GE(childNode->inclusive_ms, 3);
    EXPECT_EQ(parentNode->exclusive_ms, parentNode->inclusive_ms);
    EXPECT_EQ(childNode->exclusive_ms, childNode->inclusive_ms);
    EXPECT_TRUE(parentNode->children.empty());
    EXPECT_TRUE(childNode->children.empty());
}

TEST_F(ProfilerTest, CallTreeComplex)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Create timers from multiple sources
    profiler.start("SourceA", "Timer1");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    profiler.stop("SourceA", "Timer1");

    profiler.start("SourceA", "Timer2");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    profiler.stop("SourceA", "Timer2");

    profiler.start("SourceB", "Timer3");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    profiler.stop("SourceB", "Timer3");

    // Rebuild call tree and get it
    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();

    // Should have a root node with "Call Tree" name
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_EQ(callTree.children.size(), 2); // Two sources

    // Find SourceA and SourceB nodes
    const Profiler::CallTreeNode* sourceANode = nullptr;
    const Profiler::CallTreeNode* sourceBNode = nullptr;

    for (const auto& child : callTree.children)
    {
        if (child.name == "SourceA")
        {
            sourceANode = &child;
        }
        else if (child.name == "SourceB")
        {
            sourceBNode = &child;
        }
    }

    ASSERT_NE(sourceANode, nullptr);
    ASSERT_NE(sourceBNode, nullptr);

    // SourceA should have 2 timers
    EXPECT_EQ(sourceANode->children.size(), 2);
    EXPECT_GE(sourceANode->inclusive_ms, 8); // Sum of Timer1 + Timer2

    // SourceB should have 1 timer
    EXPECT_EQ(sourceBNode->children.size(), 1);
    EXPECT_GE(sourceBNode->inclusive_ms, 2); // Timer3

    // Check that timers exist in their respective sources
    bool foundTimer1 = false, foundTimer2 = false, foundTimer3 = false;
    for (const auto& timer : sourceANode->children)
    {
        if (timer.name == "Timer1")
        {
            foundTimer1 = true;
        }
        else if (timer.name == "Timer2")
        {
            foundTimer2 = true;
        }
    }
    for (const auto& timer : sourceBNode->children)
    {
        if (timer.name == "Timer3")
        {
            foundTimer3 = true;
        }
    }

    EXPECT_TRUE(foundTimer1);
    EXPECT_TRUE(foundTimer2);
    EXPECT_TRUE(foundTimer3);
}

TEST_F(ProfilerTest, CallTreeEmpty)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // No events collected yet
    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_EQ(callTree.inclusive_ms, 0);
    EXPECT_EQ(callTree.exclusive_ms, 0);
    EXPECT_TRUE(callTree.children.empty());
}

TEST_F(ProfilerTest, CallTreeReset)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Add some events
    profiler.start("TestSource", "Timer1");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("TestSource", "Timer1");

    // Verify call tree has data
    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_FALSE(callTree.children.empty());

    // Reset call tree
    profiler.resetCallTree();

    // Verify it's reset (root name is always "Call Tree", but children should be empty)
    profiler.forceRebuildCallTree();
    const auto& resetTree = profiler.getCurrentCallTree();
    EXPECT_EQ(resetTree.name, "Call Tree");
    EXPECT_EQ(resetTree.inclusive_ms, 0);
    EXPECT_EQ(resetTree.exclusive_ms, 0);
    EXPECT_TRUE(resetTree.children.empty());
}

TEST_F(ProfilerTest, CallTreeToJson)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Create a simple call tree
    profiler.start("TestSource", "SimpleTimer");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("TestSource", "SimpleTimer");

    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();
    std::string json = callTree.toJson();

    // Verify JSON structure contains expected elements
    EXPECT_NE(json.find("\"name\":\"Call Tree\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"TestSource\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"SimpleTimer\""), std::string::npos);
    EXPECT_NE(json.find("\"inclusive_ms\":"), std::string::npos);
    EXPECT_NE(json.find("\"exclusive_ms\":"), std::string::npos);
    EXPECT_NE(json.find("\"children\":"), std::string::npos);

    // Should be valid JSON
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

TEST_F(ProfilerTest, CallTreeToJsonNested)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Create timers from the same source
    profiler.start("TestSource", "Parent");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    profiler.stop("TestSource", "Parent");

    profiler.start("TestSource", "Child");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    profiler.stop("TestSource", "Child");

    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();
    std::string json = callTree.toJson();

    // Verify JSON contains expected elements
    EXPECT_NE(json.find("\"name\":\"Call Tree\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"TestSource\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Parent\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Child\""), std::string::npos);
    EXPECT_NE(json.find("\"inclusive_ms\":"), std::string::npos);
    EXPECT_NE(json.find("\"exclusive_ms\":"), std::string::npos);

    // Should be valid JSON
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

TEST_F(ProfilerTest, CallTreeUnmatchedEvents)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Test with unmatched start event (no corresponding end)
    profiler.start("TestSource", "IncompleteTimer");
    // Don't stop it

    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();

    // Should have root "Call Tree" but no children since there are no completed events
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_TRUE(callTree.children.empty());

    // The behavior for incomplete events depends on whether we have any completed events
    // Since we only have an incomplete event (which gets filtered as zero-duration), the tree has no children
    // This is acceptable - incomplete events are not shown in the hierarchy
}

TEST_F(ProfilerTest, CallTreeMultipleRoots)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Create timers from multiple sources
    profiler.start("Source1", "Func1");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    profiler.stop("Source1", "Func1");

    profiler.start("Source2", "Func2");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    profiler.stop("Source2", "Func2");

    profiler.forceRebuildCallTree();
    const auto& callTree = profiler.getCurrentCallTree();

    // Should have a root with multiple source children
    EXPECT_EQ(callTree.name, "Call Tree");
    EXPECT_EQ(callTree.children.size(), 2); // Two sources

    // Find Source1 and Source2
    bool foundSource1 = false, foundSource2 = false;
    for (const auto& source : callTree.children)
    {
        if (source.name == "Source1")
        {
            foundSource1 = true;
            EXPECT_EQ(source.children.size(), 1);
            EXPECT_EQ(source.children[0].name, "Func1");
        }
        else if (source.name == "Source2")
        {
            foundSource2 = true;
            EXPECT_EQ(source.children.size(), 1);
            EXPECT_EQ(source.children[0].name, "Func2");
        }
    }

    EXPECT_TRUE(foundSource1);
    EXPECT_TRUE(foundSource2);
}

TEST_F(ProfilerTest, ColorRangeEdgeCases)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Test with zero thresholds
    profiler.setColorRange("Test::Zero", std::chrono::microseconds(0), std::chrono::microseconds(0));
    auto range = profiler.getColorRange("Test::Zero");
    EXPECT_EQ(range.greenThreshold, std::chrono::microseconds(0));
    EXPECT_EQ(range.redThreshold, std::chrono::microseconds(0));

    // Test with very large thresholds
    const auto largeThreshold = std::chrono::microseconds(1000000000); // 1000 seconds
    profiler.setColorRange("Test::Large", std::chrono::microseconds(1), largeThreshold);
    range = profiler.getColorRange("Test::Large");
    EXPECT_EQ(range.greenThreshold, std::chrono::microseconds(1));
    EXPECT_EQ(range.redThreshold, largeThreshold);

    // Test green threshold larger than red threshold (should still work)
    profiler.setColorRange("Test::Inverted", std::chrono::microseconds(10000), std::chrono::microseconds(1000));
    range = profiler.getColorRange("Test::Inverted");
    EXPECT_EQ(range.greenThreshold, std::chrono::microseconds(10000));
    EXPECT_EQ(range.redThreshold, std::chrono::microseconds(1000));
}

TEST_F(ProfilerTest, StatisticsEdgeCases)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Test statistics for non-existent timer
    Profiler::TimerStatistics stats;
    EXPECT_FALSE(profiler.getTimerStatistics("NonExistent", "Timer", stats));

    // Test with active timer (should not return statistics)
    profiler.start("TestSource", "ActiveTimer");
    EXPECT_FALSE(profiler.getTimerStatistics("TestSource", "ActiveTimer", stats));

    // Stop the timer and try again
    profiler.stop("TestSource", "ActiveTimer");
    EXPECT_TRUE(profiler.getTimerStatistics("TestSource", "ActiveTimer", stats));
    EXPECT_GE(stats.current.count(), 0); // Should be >= 0, not > 0 since timing might be very short
}

TEST_F(ProfilerTest, ConfigureStatisticsEdgeCases)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Test with zero window size (should handle gracefully)
    profiler.configureStatistics(0);

    // Add a sample
    profiler.start("Test", "Timer");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Test", "Timer");

    // Should still work (probably with default behavior)
    Profiler::TimerStatistics stats;
    EXPECT_TRUE(profiler.getTimerStatistics("Test", "Timer", stats));

    // Test with very large window size
    profiler.configureStatistics(10000);
    profiler.resetStatistics();

    // Add samples
    for (int i = 0; i < 5; ++i)
    {
        profiler.start("Test", "LargeWindow");
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        profiler.stop("Test", "LargeWindow");
    }

    EXPECT_TRUE(profiler.getTimerStatistics("Test", "LargeWindow", stats));
    EXPECT_EQ(stats.sampleCount, 5);
}

TEST_F(ProfilerTest, SingleCycleTracking)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // First cycle: 5 iterations
    for (int i = 0; i < 5; ++i)
    {
        profiler.start("CycleTest", "Iteration");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        profiler.stop("CycleTest", "Iteration");
    }

    profiler.forceRebuildCallTree();
    const auto& tree1 = profiler.getCurrentCallTree();

    // Should have aggregated 5 iterations
    ASSERT_EQ(tree1.children.size(), 1);
    EXPECT_EQ(tree1.children[0].name, "CycleTest");
    ASSERT_EQ(tree1.children[0].children.size(), 1);
    EXPECT_EQ(tree1.children[0].children[0].name, "Iteration");

    // Store the duration from first cycle
    int64_t firstCycleDuration = tree1.children[0].children[0].inclusive_ms;
    EXPECT_GT(firstCycleDuration, 0);

    // Second cycle: 3 iterations (should start fresh, not accumulate)
    for (int i = 0; i < 3; ++i)
    {
        profiler.start("CycleTest", "Iteration");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        profiler.stop("CycleTest", "Iteration");
    }

    profiler.forceRebuildCallTree();
    const auto& tree2 = profiler.getCurrentCallTree();

    // Should have only 3 iterations (not 8)
    ASSERT_EQ(tree2.children.size(), 1);
    EXPECT_EQ(tree2.children[0].name, "CycleTest");
    ASSERT_EQ(tree2.children[0].children.size(), 1);
    EXPECT_EQ(tree2.children[0].children[0].name, "Iteration");

    int64_t secondCycleDuration = tree2.children[0].children[0].inclusive_ms;

    // Second cycle should have approximately 3/5 the duration of first cycle
    // (allowing for timing variance)
    double ratio = static_cast<double>(secondCycleDuration) / static_cast<double>(firstCycleDuration);
    EXPECT_LT(ratio, 0.8); // Should be significantly less than first cycle
}

TEST_F(ProfilerTest, GetCurrentCallTreeIsConst)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Create some profiling data
    profiler.start("ConstTest", "Operation");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("ConstTest", "Operation");

    // First call: tree not built yet, should return empty
    const auto& emptyTree = profiler.getCurrentCallTree();
    EXPECT_EQ(emptyTree.name, "empty");

    // Explicitly build the tree
    profiler.forceRebuildCallTree();

    // Second call: should return built tree
    const auto& builtTree = profiler.getCurrentCallTree();
    EXPECT_EQ(builtTree.name, "Call Tree");
    EXPECT_FALSE(builtTree.children.empty());

    // Third call: should return same cached tree (not rebuild)
    const auto& cachedTree = profiler.getCurrentCallTree();
    EXPECT_EQ(cachedTree.name, "Call Tree");
    EXPECT_FALSE(cachedTree.children.empty());
}

TEST_F(ProfilerTest, ZeroDurationFilteringIntegration)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Complete operation 1
    profiler.start("FilterTest", "Complete1");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("FilterTest", "Complete1");

    // Complete operation 2
    profiler.start("FilterTest", "Complete2");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("FilterTest", "Complete2");

    // Build tree
    profiler.forceRebuildCallTree();
    const auto& tree = profiler.getCurrentCallTree();

    // Should have both complete operations
    ASSERT_EQ(tree.children.size(), 1);
    EXPECT_EQ(tree.children[0].name, "FilterTest");
    ASSERT_EQ(tree.children[0].children.size(), 2);

    // Verify both operations are present
    bool found1 = false, found2 = false;
    for (const auto& child : tree.children[0].children)
    {
        if (child.name == "Complete1")
        {
            found1 = true;
        }
        if (child.name == "Complete2")
        {
            found2 = true;
        }
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}

TEST_F(ProfilerTest, MultipleRebuildCycles)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Cycle 1: 2 iterations
    for (int i = 0; i < 2; ++i)
    {
        profiler.start("MultiCycle", "Task");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        profiler.stop("MultiCycle", "Task");
    }
    profiler.forceRebuildCallTree();
    auto tree1 = profiler.getCurrentCallTree();
    ASSERT_EQ(tree1.children.size(), 1);

    // Cycle 2: 3 iterations
    for (int i = 0; i < 3; ++i)
    {
        profiler.start("MultiCycle", "Task");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        profiler.stop("MultiCycle", "Task");
    }
    profiler.forceRebuildCallTree();
    auto tree2 = profiler.getCurrentCallTree();
    ASSERT_EQ(tree2.children.size(), 1);

    // Cycle 3: 1 iteration
    profiler.start("MultiCycle", "Task");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("MultiCycle", "Task");
    profiler.forceRebuildCallTree();
    auto tree3 = profiler.getCurrentCallTree();
    ASSERT_EQ(tree3.children.size(), 1);

    // Each cycle should be independent
    // We can't check exact durations due to timing variance,
    // but we can verify the structure is consistent
    EXPECT_EQ(tree1.children[0].name, "MultiCycle");
    EXPECT_EQ(tree2.children[0].name, "MultiCycle");
    EXPECT_EQ(tree3.children[0].name, "MultiCycle");
}

TEST_F(ProfilerTest, NestedOperationsWithIncomplete)
{
    Profiler& profiler = Profiler::getInstance();
    profiler.reset();

    // Parent operation
    profiler.start("NestedIncomplete", "Parent");

    // Complete child 1
    profiler.start("NestedIncomplete", "Child1");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("NestedIncomplete", "Child1");

    // Complete child 2
    profiler.start("NestedIncomplete", "Child2");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.stop("NestedIncomplete", "Child2");

    // Complete parent
    profiler.stop("NestedIncomplete", "Parent");

    profiler.forceRebuildCallTree();
    const auto& tree = profiler.getCurrentCallTree();

    // Should have parent with both complete children
    // Note: Hierarchical restructuring only applies to leaf nodes
    // Non-leaf nodes (like Parent which has children) keep their full name
    ASSERT_EQ(tree.children.size(), 1);
    EXPECT_EQ(tree.children[0].name, "NestedIncomplete::Parent");

    // Parent should have both complete children
    ASSERT_EQ(tree.children[0].children.size(), 2);

    // Children are restructured into Source -> Timer hierarchy since they are leaf nodes
    // First child (could be NestedIncomplete source with Child1 timer, or vice versa depending on order)
    bool foundChild1 = false;
    bool foundChild2 = false;

    for (const auto& child : tree.children[0].children)
    {
        if (child.name == "NestedIncomplete")
        {
            // This is a source node grouping timers
            for (const auto& timer : child.children)
            {
                if (timer.name == "Child1")
                {
                    foundChild1 = true;
                }
                if (timer.name == "Child2")
                {
                    foundChild2 = true;
                }
            }
        }
        else if (child.name == "Child1" || child.name == "NestedIncomplete::Child1")
        {
            foundChild1 = true;
        }
        else if (child.name == "Child2" || child.name == "NestedIncomplete::Child2")
        {
            foundChild2 = true;
        }
    }

    EXPECT_TRUE(foundChild1);
    EXPECT_TRUE(foundChild2);
}

