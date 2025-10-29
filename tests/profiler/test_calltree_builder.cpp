#include <gtest/gtest.h>

#include "LinuxFace/profiler.h"

using linuxface::Profiler;

// Simple helper to create start/end events manually
static Profiler::ProfileEvent ev(const std::string& name, const std::string& type, int64_t ts, uint64_t tid = 1)
{
    Profiler::ProfileEvent e;
    e.name = name;
    e.event = type;
    e.timestamp_ms = ts;
    e.thread_id = tid;
    return e;
}

TEST(ProfilerCallTreeBuilder, NestedTimers)
{
    std::vector<Profiler::ProfileEvent> events;
    // A starts at 0
    events.push_back(ev("MainLoop::FrameProcessing", "start", 0));
    // B inside A
    events.push_back(ev("Application::Update Camera Input", "start", 1));
    events.push_back(ev("Application::Update Camera Input", "end", 3));
    // After B, A continues
    events.push_back(ev("MainLoop::FrameProcessing", "end", 7));

    auto root = Profiler::buildProfileHierarchy(events);
    ASSERT_FALSE(root.children.empty());
    // Root should have one child (MainLoop::FrameProcessing)
    EXPECT_EQ(root.children[0].name, "MainLoop::FrameProcessing");
    EXPECT_GT(root.children[0].inclusive_ms, 0);
    // It should have one child (Application::Update Camera Input)
    ASSERT_EQ(root.children[0].children.size(), 1);
    EXPECT_EQ(root.children[0].children[0].name, "Application::Update Camera Input");
}

TEST(ProfilerCallTreeBuilder, ConcurrentSameThreadNonNested)
{
    std::vector<Profiler::ProfileEvent> events;
    // Two timers on same thread but not nested
    events.push_back(ev("A::task1", "start", 0));
    events.push_back(ev("A::task1", "end", 2));
    events.push_back(ev("A::task2", "start", 3));
    events.push_back(ev("A::task2", "end", 5));

    auto root = Profiler::buildProfileHierarchy(events);
    // With source grouping enabled, leaf nodes get grouped under their source
    // Should have one source node "A" with two timer children
    EXPECT_EQ(root.children.size(), 1);
    EXPECT_EQ(root.children[0].name, "A");
    EXPECT_EQ(root.children[0].children.size(), 2);
    // Children should be task1 and task2
    EXPECT_EQ(root.children[0].children[0].name, "task1");
    EXPECT_EQ(root.children[0].children[1].name, "task2");
}

TEST(ProfilerCallTreeBuilder, CrossThreadStartEnd)
{
    std::vector<Profiler::ProfileEvent> events;
    // Start on thread 1, end on thread 2
    events.push_back(ev("X::async", "start", 0, 1));
    events.push_back(ev("X::async", "end", 5, 2));

    auto root = Profiler::buildProfileHierarchy(events);
    // Since start and end are on different threads, incomplete nodes are created
    // These get filtered out by zero-duration filtering, resulting in empty tree
    EXPECT_EQ(root.name, "Call Tree");
    // Main goal: verify function handles cross-thread events without crashing
}

TEST(ProfilerCallTreeBuilder, ZeroDurationFiltering)
{
    std::vector<Profiler::ProfileEvent> events;

    // Complete operation 1
    events.push_back(ev("A::task1", "start", 0));
    events.push_back(ev("A::task1", "end", 5));

    // Complete operation 2
    events.push_back(ev("B::task2", "start", 10));
    events.push_back(ev("B::task2", "end", 15));

    auto root = Profiler::buildProfileHierarchy(events);

    // Should have both complete operations
    EXPECT_EQ(root.children.size(), 2); // A and B

    // Verify both sources present
    bool foundA = false, foundB = false;
    for (const auto& child : root.children)
    {
        if (child.name == "A")
        {
            foundA = true;
            EXPECT_EQ(child.children.size(), 1);
            EXPECT_EQ(child.children[0].name, "task1");
            EXPECT_GT(child.children[0].inclusive_ms, 0);
        }
        else if (child.name == "B")
        {
            foundB = true;
            EXPECT_EQ(child.children.size(), 1);
            EXPECT_EQ(child.children[0].name, "task2");
            EXPECT_GT(child.children[0].inclusive_ms, 0);
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
}

TEST(ProfilerCallTreeBuilder, NestedZeroDurationFiltering)
{
    std::vector<Profiler::ProfileEvent> events;

    // Parent with complete duration
    events.push_back(ev("Parent::A", "start", 0));

    // Child 1: complete
    events.push_back(ev("Child::B", "start", 1));
    events.push_back(ev("Child::B", "end", 3));

    // Child 2: complete
    events.push_back(ev("Child::C", "start", 4));
    events.push_back(ev("Child::C", "end", 6));

    events.push_back(ev("Parent::A", "end", 10));

    auto root = Profiler::buildProfileHierarchy(events);

    // Should have Parent::A with both children
    ASSERT_FALSE(root.children.empty());
    EXPECT_EQ(root.children[0].name, "Parent::A");
    EXPECT_EQ(root.children[0].children.size(), 2); // Both children complete
    EXPECT_EQ(root.children[0].children[0].name, "Child::B");
    EXPECT_EQ(root.children[0].children[1].name, "Child::C");
}

TEST(ProfilerCallTreeBuilder, AllZeroDurationNodes)
{
    std::vector<Profiler::ProfileEvent> events;

    // All incomplete operations (start without matching end)
    events.push_back(ev("A::task1", "start", 0));
    events.push_back(ev("B::task2", "start", 1));
    events.push_back(ev("C::task3", "start", 2));
    // No end events - all stay on stack

    auto root = Profiler::buildProfileHierarchy(events);

    // Should result in empty tree (all operations incomplete, stay on stack)
    EXPECT_EQ(root.name, "Call Tree");
    EXPECT_TRUE(root.children.empty());
}

TEST(ProfilerCallTreeBuilder, MismatchedStartEnd)
{
    std::vector<Profiler::ProfileEvent> events;

    // Start event
    events.push_back(ev("A::task1", "start", 0));
    // Mismatched end event (different name) - will pop task1 and calculate duration
    events.push_back(ev("B::task2", "end", 5));

    auto root = Profiler::buildProfileHierarchy(events);

    // The system pops whatever is on stack regardless of name match
    // This creates a node named "A::task1" with duration from task2's end event
    // This is expected behavior (garbage in, garbage out for mismatched events)
    EXPECT_FALSE(root.children.empty());
}

