#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>

#include "LinuxFace/common.h"

using namespace linuxface::common;

TEST(CommonTest, FileExists)
{
    // Test with a file that should exist (current directory)
    EXPECT_TRUE(fileExists("."));

    // Test with a file that shouldn't exist
    EXPECT_FALSE(fileExists("/nonexistent/path/file.txt"));
}

TEST(CommonTest, Clamp)
{
    EXPECT_EQ(clamp(5, 0, 10), 5);
    EXPECT_EQ(clamp(-5, 0, 10), 0);
    EXPECT_EQ(clamp(15, 0, 10), 10);

    EXPECT_FLOAT_EQ(clamp(2.5f, 1.0f, 3.0f), 2.5f);
    EXPECT_FLOAT_EQ(clamp(0.5f, 1.0f, 3.0f), 1.0f);
    EXPECT_FLOAT_EQ(clamp(4.0f, 1.0f, 3.0f), 3.0f);
}

TEST(CommonTest, FormatSize)
{
    // Test bytes
    const char* result = formatSize(512);
    EXPECT_TRUE(strstr(result, "bytes") != nullptr);

    // Test KB
    result = formatSize(2048);
    EXPECT_TRUE(strstr(result, "KB") != nullptr);

    // Test MB
    result = formatSize(2 * 1024 * 1024);
    EXPECT_TRUE(strstr(result, "MB") != nullptr);

    // Test GB
    result = formatSize(2UL * 1024 * 1024 * 1024);
    EXPECT_TRUE(strstr(result, "GB") != nullptr);
}

TEST(CommonTest, LongWrite)
{
    // Create a temporary file for testing
    int fd = open("/tmp/test_write.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT_NE(fd, -1);

    const char* test_data = "Hello, World!";
    bool result = longWrite(fd, test_data, strlen(test_data));
    EXPECT_TRUE(result);

    close(fd);
    unlink("/tmp/test_write.txt");
}

TEST(CommonTest, GetKeysFromMap)
{
    std::unordered_map<std::string, std::shared_ptr<int>> test_map;
    test_map["key1"] = std::make_shared<int>(1);
    test_map["key3"] = std::make_shared<int>(3);
    test_map["key2"] = std::make_shared<int>(2);

    auto keys = getKeysFromMap(test_map);
    EXPECT_EQ(keys.size(), 3);

    // Keys should be sorted alphabetically
    EXPECT_EQ(keys[0], "key1");
    EXPECT_EQ(keys[1], "key2");
    EXPECT_EQ(keys[2], "key3");
}

TEST(CommonTest, Lerp)
{
    EXPECT_FLOAT_EQ(lerp(0.0f, 10.0f, 0.5f), 5.0f);
    EXPECT_FLOAT_EQ(lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(lerp(0.0f, 10.0f, 1.0f), 10.0f);
}

// Note: Logger tests are more complex due to static state and file I/O
// These would require more setup/teardown and mocking
TEST(CommonTest, LogLevelStr)
{
    EXPECT_STREQ(logLevelStr(LogLevel::INFO), "INFO");
    EXPECT_STREQ(logLevelStr(LogLevel::WARN), "WARN");
    EXPECT_STREQ(logLevelStr(LogLevel::ERROR), "ERROR");
}

TEST(CommonTest, LogColor)
{
    // Test that color functions return non-null strings
    EXPECT_NE(logColor(LogLevel::INFO), nullptr);
    EXPECT_NE(logColor(LogLevel::WARN), nullptr);
    EXPECT_NE(logColor(LogLevel::ERROR), nullptr);
    EXPECT_NE(logColorReset(), nullptr);
}
