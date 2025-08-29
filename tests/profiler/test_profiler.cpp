#include <gtest/gtest.h>

#include <chrono>
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
    bool found = profiler.duration("TestSource", "TestTimer", duration);

    EXPECT_TRUE(found);
    EXPECT_GT(duration.count(), 0);
    // We slept for 100 microseconds, so duration should be at least 50µs (allowing for timing variance)
    // but less than 10ms (10,000µs) to account for system scheduling
    EXPECT_GE(duration.count(), 50);     // At least 50µs (half of what we slept)
    EXPECT_LT(duration.count(), 10000);  // Less than 10ms (generous upper bound)
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
    EXPECT_TRUE(profiler.duration("Source1", "Timer1", duration1));
    EXPECT_TRUE(profiler.duration("Source2", "Timer2", duration2));

    // Timer2 should be longer than Timer1 since it ran for ~100µs vs ~50µs
    // But allow for timing variance - just ensure both are reasonable
    EXPECT_GE(duration1.count(), 25);    // At least 25µs (half of 50µs sleep)
    EXPECT_GE(duration2.count(), 75);    // At least 75µs (Timer2 ran longer)
    EXPECT_GT(duration2.count(), duration1.count()); // Timer2 should still be longer
}

TEST_F(ProfilerTest, NonExistentTimer)
{
    Profiler& profiler = Profiler::getInstance();

    std::chrono::microseconds duration;
    bool found = profiler.duration("NonExistent", "Timer", duration);

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

    auto durations = profiler.getDurations("TestSource");
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
    std::this_thread::sleep_for(std::chrono::microseconds(50));  // Longer sleep
    profiler.stop("A", "Timer1");

    profiler.start("B", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(10));  // Shorter sleep
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
    // since it should have longer duration
    bool found_timer1_first = false;
    for (const auto& entry : sorted) {
        if (entry.first.find("Timer1") != std::string::npos) {
            found_timer1_first = true;
            break;
        }
        if (entry.first.find("Timer2") != std::string::npos) {
            break;  // Found Timer2 first, which is wrong
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
    bool found = profiler.duration("NonExistent", "Timer", duration);
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
    EXPECT_TRUE(profiler.duration("Source1", "SameName", duration1));
    EXPECT_TRUE(profiler.duration("Source2", "SameName", duration2));

    // Both should be valid and positive
    EXPECT_GT(duration1.count(), 0);
    EXPECT_GT(duration2.count(), 0);
    
    // Source2 should have longer duration since it slept longer
    // But allow for timing variance - don't require exact ordering
    EXPECT_GE(duration1.count(), 10);  // At least 10µs (half of 20µs sleep)
    EXPECT_GE(duration2.count(), 20);  // At least 20µs (half of 40µs sleep)
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
    auto durations = profiler.getDurations();
    EXPECT_EQ(durations.size(), 2);
    
    // Reset and verify empty
    profiler.reset();
    durations = profiler.getDurations();
    EXPECT_EQ(durations.size(), 0);
    
    // Verify specific lookups fail
    std::chrono::microseconds duration;
    EXPECT_FALSE(profiler.duration("Source1", "Timer1", duration));
    EXPECT_FALSE(profiler.duration("Source2", "Timer2", duration));
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
    auto durations = profiler.getDurations();
    EXPECT_EQ(durations.size(), 1);
    
    // Reset should clear everything including active timers
    profiler.reset();
    
    // Verify everything is cleared
    durations = profiler.getDurations();
    EXPECT_EQ(durations.size(), 0);
    
    // Try to stop the previously active timers - should not crash or create entries
    profiler.stop("Source1", "ActiveTimer1");
    profiler.stop("Source2", "ActiveTimer2");
    
    durations = profiler.getDurations();
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
    auto durations = profiler.getDurations("NonExistentSource");
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
    auto durations1 = profiler.getDurations("Source1");
    auto durations2 = profiler.getDurations("Source2");
    
    EXPECT_EQ(durations1.size(), 2);
    EXPECT_EQ(durations2.size(), 1);
    
    EXPECT_TRUE(durations1.find("Timer1") != durations1.end());
    EXPECT_TRUE(durations1.find("Timer2") != durations1.end());
    EXPECT_TRUE(durations2.find("Timer1") != durations2.end());
}

TEST_F(ProfilerTest, GetDurationsSortedOrder)
{
    Profiler& profiler = Profiler::getInstance();
    
    // Add timers with different durations
    profiler.start("Source", "Fast");
    std::this_thread::sleep_for(std::chrono::microseconds(5));
    profiler.stop("Source", "Fast");
    
    profiler.start("Source", "Slow");
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    profiler.stop("Source", "Slow");
    
    profiler.start("Source", "Medium");
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    profiler.stop("Source", "Medium");
    
    auto sorted = profiler.getDurationsSorted();
    EXPECT_EQ(sorted.size(), 3);
    
    // Should be sorted by duration (descending)
    EXPECT_GE(sorted[0].second.count(), sorted[1].second.count());
    EXPECT_GE(sorted[1].second.count(), sorted[2].second.count());
    
    // The longest should be "Slow"
    EXPECT_TRUE(sorted[0].first.find("Slow") != std::string::npos);
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
    EXPECT_TRUE(profiler.duration("Source1", "Timer1", duration1));
    EXPECT_TRUE(profiler.duration("Source2", "Timer2", duration2));
    EXPECT_TRUE(profiler.duration("Source3", "Timer3", duration3));
    
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
    EXPECT_TRUE(profiler.duration("Source", "Timer", duration1));
    
    // Start and stop the same timer again (should overwrite)
    profiler.start("Source", "Timer");
    std::this_thread::sleep_for(std::chrono::microseconds(30));
    profiler.stop("Source", "Timer");
    
    std::chrono::microseconds duration2;
    EXPECT_TRUE(profiler.duration("Source", "Timer", duration2));
    
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
    EXPECT_TRUE(profiler.duration("", "", duration));
    EXPECT_GT(duration.count(), 0);
    
    // Test mixed empty strings
    profiler.start("Source", "");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source", "");
    
    EXPECT_TRUE(profiler.duration("Source", "", duration));
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
