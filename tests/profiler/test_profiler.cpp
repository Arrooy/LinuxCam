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
        // Get fresh instance for each test
    }

    void TearDown() override
    {
        // Clean up profiler state if needed
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
    EXPECT_GE(duration.count(), 100); // Should be at least 100 microseconds
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

    // Timer2 should be longer than Timer1
    EXPECT_GT(duration2.count(), duration1.count());
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
    EXPECT_GT(durations["Timer1"].count(), 0);
    EXPECT_GT(durations["Timer2"].count(), 0);
}

TEST_F(ProfilerTest, GetDurationsSorted)
{
    Profiler& profiler = Profiler::getInstance();

    profiler.start("A", "Timer1");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("A", "Timer1");

    profiler.start("B", "Timer2");
    std::this_thread::sleep_for(std::chrono::microseconds(5));
    profiler.stop("B", "Timer2");

    auto sorted = profiler.getDurationsSorted();
    EXPECT_GE(sorted.size(), 2);

    // Should be sorted alphabetically by key
    bool is_sorted = true;
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        if (sorted[i - 1].first > sorted[i].first)
        {
            is_sorted = false;
            break;
        }
    }
    EXPECT_TRUE(is_sorted);
}

TEST_F(ProfilerTest, FormatDurationMicroseconds)
{
    std::string result = Profiler::format_duration(static_cast<int64_t>(500));
    EXPECT_NE(result.find("µs"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationMilliseconds)
{
    std::string result = Profiler::format_duration(static_cast<int64_t>(5000)); // 5ms
    EXPECT_NE(result.find("ms"), std::string::npos);
    EXPECT_NE(result.find("Hz"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationSeconds)
{
    std::string result = Profiler::format_duration(static_cast<int64_t>(2000000)); // 2s
    EXPECT_NE(result.find("s"), std::string::npos);
    EXPECT_NE(result.find("Hz"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationMinutes)
{
    std::string result = Profiler::format_duration(static_cast<int64_t>(120000000)); // 2 minutes
    EXPECT_NE(result.find("min"), std::string::npos);
    EXPECT_NE(result.find("Hz"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationInvalid)
{
    std::string result = Profiler::format_duration(static_cast<int64_t>(-1));
    EXPECT_NE(result.find("Invalid"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationChrono)
{
    auto duration = std::chrono::microseconds(1500);
    std::string result = Profiler::format_duration(duration);
    EXPECT_NE(result.find("ms"), std::string::npos);
}

TEST_F(ProfilerTest, FormatDurationTimePoints)
{
    auto start = std::chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    auto end = std::chrono::high_resolution_clock::now();

    std::string result = Profiler::format_duration(start, end);
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
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    profiler.stop("Source1", "SameName");

    profiler.start("Source2", "SameName");
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    profiler.stop("Source2", "SameName");

    std::chrono::microseconds duration1, duration2;
    EXPECT_TRUE(profiler.duration("Source1", "SameName", duration1));
    EXPECT_TRUE(profiler.duration("Source2", "SameName", duration2));

    // Both should be valid but different
    EXPECT_GT(duration1.count(), 0);
    EXPECT_GT(duration2.count(), 0);
    EXPECT_NE(duration1.count(), duration2.count());
}
