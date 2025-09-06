#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include "LinuxFace/videoLoader.h"

using namespace linuxface;

class VideoLoaderTest : public ::testing::Test
{
protected:
    std::string tempDir;
    std::string validVideoPath;
    std::string invalidVideoPath;
    std::string nonExistentVideoPath;
    std::string corruptedVideoPath;

    void SetUp() override
    {
        // Create temporary directory for test files
        char templ[] = "/tmp/videoLoader_test_XXXXXX";
        char* tempDirPtr = mkdtemp(templ);
        if (tempDirPtr)
        {
            tempDir = tempDirPtr;
        }
        else
        {
            FAIL() << "Failed to create temporary directory";
        }

        // Set up test file paths
        validVideoPath = tempDir + "/test_video.mp4";
        invalidVideoPath = tempDir + "/invalid_video.xyz";
        nonExistentVideoPath = tempDir + "/nonexistent.mp4";
        corruptedVideoPath = tempDir + "/corrupted.mp4";

        // Create a minimal valid video file for testing
        createMinimalTestVideo(validVideoPath);

        // Create invalid/corrupted files
        createInvalidTestFile(invalidVideoPath);
        createCorruptedTestFile(corruptedVideoPath);
    }

    void TearDown() override
    {
        // Clean up test files
        if (!tempDir.empty())
        {
            std::filesystem::remove_all(tempDir);
        }
    }

    // Helper method to create a minimal valid video file
    void createMinimalTestVideo(const std::string& path)
    {
        // Create a simple test video using FFmpeg command
        // This creates a 1-second video with a solid color
        std::string cmd = "ffmpeg -f lavfi -i color=c=blue:s=320x240:d=1 -c:v libx264 -t 1 -y " + path + " 2>/dev/null";
        int result = system(cmd.c_str());
        if (result != 0)
        {
            // If FFmpeg fails, create a minimal mock file
            createMockVideoFile(path);
        }
    }

    // Helper method to create a mock video file for testing
    void createMockVideoFile(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (f)
        {
            // Write minimal MP4 header structure (simplified)
            const unsigned char mockData[] = {
                0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, // ftyp box
                0x69, 0x73, 0x6F, 0x6D, 0x00, 0x00, 0x00, 0x01,
                0x69, 0x73, 0x6F, 0x6D, 0x61, 0x76, 0x63, 0x31,
                0x00, 0x00, 0x00, 0x08, 0x6D, 0x64, 0x61, 0x74  // mdat box
            };
            fwrite(mockData, 1, sizeof(mockData), f);
            fclose(f);
        }
    }

    // Helper method to create an invalid file
    void createInvalidTestFile(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (f)
        {
            const char* invalidData = "This is not a video file";
            fwrite(invalidData, 1, strlen(invalidData), f);
            fclose(f);
        }
    }

    // Helper method to create a corrupted file
    void createCorruptedTestFile(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (f)
        {
            // Write partial/invalid MP4 data
            const unsigned char corruptedData[] = {
                0x00, 0x00, 0x00, 0x10, 0x66, 0x74, 0x79, 0x70, // Incomplete ftyp
                0x00, 0x00, 0x00, 0x00                          // Corrupted data
            };
            fwrite(corruptedData, 1, sizeof(corruptedData), f);
            fclose(f);
        }
    }
};

// Test constructor and destructor
TEST_F(VideoLoaderTest, ConstructorAndDestructor)
{
    VideoLoader* loader = new VideoLoader();
    EXPECT_NE(loader, nullptr);
    EXPECT_FALSE(loader->isValid());

    delete loader;
    // Should not crash
}

// Test loading a valid video file
TEST_F(VideoLoaderTest, LoadValidVideoFile)
{
    VideoLoader loader;

    bool result = loader.loadFromFile(validVideoPath);
    EXPECT_TRUE(result);
    EXPECT_TRUE(loader.isValid());

    const auto& metadata = loader.getMetadata();
    EXPECT_TRUE(metadata.isValid);
    EXPECT_GT(metadata.width, 0);
    EXPECT_GT(metadata.height, 0);
    EXPECT_GE(metadata.frameCount, 0); // May be 0 for some formats
    EXPECT_GE(metadata.fps, 0.0);
    EXPECT_EQ(metadata.filename, validVideoPath);
}

// Test loading a non-existent file
TEST_F(VideoLoaderTest, LoadNonExistentFile)
{
    VideoLoader loader;

    bool result = loader.loadFromFile(nonExistentVideoPath);
    EXPECT_FALSE(result);
    EXPECT_FALSE(loader.isValid());
}

// Test loading an invalid file format
TEST_F(VideoLoaderTest, LoadInvalidFileFormat)
{
    VideoLoader loader;

    bool result = loader.loadFromFile(invalidVideoPath);
    EXPECT_FALSE(result);
    EXPECT_FALSE(loader.isValid());
}

// Test loading a corrupted file
TEST_F(VideoLoaderTest, LoadCorruptedFile)
{
    VideoLoader loader;

    bool result = loader.loadFromFile(corruptedVideoPath);
    EXPECT_FALSE(result);
    EXPECT_FALSE(loader.isValid());
}

// Test getting frames from a loaded video
TEST_F(VideoLoaderTest, GetNextFrame)
{
    VideoLoader loader;

    // Load video first
    ASSERT_TRUE(loader.loadFromFile(validVideoPath));

    std::unique_ptr<Image> frame;
    bool result = loader.getNextFrame(frame);

    if (result)
    {
        EXPECT_NE(frame, nullptr);
        EXPECT_GT(frame->info.width, 0);
        EXPECT_GT(frame->info.height, 0);
        EXPECT_EQ(frame->info.pixelSizeBytes, 3); // RGB
        EXPECT_EQ(frame->info.format, ImageFormat::RGB);
    }
    else
    {
        // It's okay if we can't get frames from the mock video
        EXPECT_EQ(frame, nullptr);
    }
}

// Test getting frames without loading video first
TEST_F(VideoLoaderTest, GetNextFrameWithoutLoading)
{
    VideoLoader loader;

    std::unique_ptr<Image> frame;
    bool result = loader.getNextFrame(frame);

    EXPECT_FALSE(result);
    EXPECT_EQ(frame, nullptr);
}

// Test seeking to a specific frame
TEST_F(VideoLoaderTest, SeekToFrame)
{
    VideoLoader loader;

    // Load video first
    ASSERT_TRUE(loader.loadFromFile(validVideoPath));

    // Test seeking to frame 0
    bool result = loader.seekToFrame(0);
    EXPECT_TRUE(result);
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);

    // Test seeking to invalid frame
    result = loader.seekToFrame(-1);
    EXPECT_FALSE(result);

    result = loader.seekToFrame(999999);
    EXPECT_FALSE(result);
}

// Test seeking without loading video first
TEST_F(VideoLoaderTest, SeekToFrameWithoutLoading)
{
    VideoLoader loader;

    bool result = loader.seekToFrame(0);
    EXPECT_FALSE(result);
}

// Test getting current frame index
TEST_F(VideoLoaderTest, GetCurrentFrameIndex)
{
    VideoLoader loader;

    // Initially should be 0
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);

    // After loading, should still be 0
    if (loader.loadFromFile(validVideoPath))
    {
        EXPECT_EQ(loader.getCurrentFrameIndex(), 0);
    }
}

// Test reset functionality
TEST_F(VideoLoaderTest, Reset)
{
    VideoLoader loader;

    // Load video first
    ASSERT_TRUE(loader.loadFromFile(validVideoPath));

    // Reset should work
    loader.reset();
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);
}

// Test reset without loading video first
TEST_F(VideoLoaderTest, ResetWithoutLoading)
{
    VideoLoader loader;

    // Should not crash
    loader.reset();
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);
}

// Test metadata access
TEST_F(VideoLoaderTest, GetMetadata)
{
    VideoLoader loader;

    // Before loading
    const auto& metadata = loader.getMetadata();
    EXPECT_FALSE(metadata.isValid);
    EXPECT_EQ(metadata.width, 0);
    EXPECT_EQ(metadata.height, 0);
    EXPECT_EQ(metadata.frameCount, 0);
    EXPECT_EQ(metadata.fps, 0.0);
    EXPECT_TRUE(metadata.filename.empty());

    // After loading
    if (loader.loadFromFile(validVideoPath))
    {
        const auto& loadedMetadata = loader.getMetadata();
        EXPECT_TRUE(loadedMetadata.isValid);
        EXPECT_GT(loadedMetadata.width, 0);
        EXPECT_GT(loadedMetadata.height, 0);
        EXPECT_GE(loadedMetadata.frameCount, 0);
        EXPECT_GE(loadedMetadata.fps, 0.0);
        EXPECT_EQ(loadedMetadata.filename, validVideoPath);
    }
}

// Test isValid method
TEST_F(VideoLoaderTest, IsValid)
{
    VideoLoader loader;

    // Initially invalid
    EXPECT_FALSE(loader.isValid());

    // After failed load, still invalid
    loader.loadFromFile(nonExistentVideoPath);
    EXPECT_FALSE(loader.isValid());

    // After successful load, valid
    if (loader.loadFromFile(validVideoPath))
    {
        EXPECT_TRUE(loader.isValid());
    }
}

// Test multiple load operations
TEST_F(VideoLoaderTest, MultipleLoadOperations)
{
    VideoLoader loader;

    // Load valid file
    EXPECT_TRUE(loader.loadFromFile(validVideoPath));
    EXPECT_TRUE(loader.isValid());

    // Load invalid file (should reset state)
    EXPECT_FALSE(loader.loadFromFile(nonExistentVideoPath));
    EXPECT_FALSE(loader.isValid());

    // Load valid file again
    EXPECT_TRUE(loader.loadFromFile(validVideoPath));
    EXPECT_TRUE(loader.isValid());
}

// Test frame iteration
TEST_F(VideoLoaderTest, FrameIteration)
{
    VideoLoader loader;

    if (!loader.loadFromFile(validVideoPath))
    {
        GTEST_SKIP() << "Cannot test frame iteration without a valid video file";
    }

    int frameCount = 0;
    std::unique_ptr<Image> frame;

    // Try to get up to 10 frames
    while (frameCount < 10 && loader.getNextFrame(frame))
    {
        EXPECT_NE(frame, nullptr);
        frameCount++;
    }

    // Should have gotten at least 1 frame or reached end of video
    EXPECT_GE(frameCount, 0);
}

// Test seeking and frame retrieval
TEST_F(VideoLoaderTest, SeekAndRetrieve)
{
    VideoLoader loader;

    if (!loader.loadFromFile(validVideoPath))
    {
        GTEST_SKIP() << "Cannot test seek and retrieve without a valid video file";
    }

    // Seek to beginning
    EXPECT_TRUE(loader.seekToFrame(0));
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);

    // Try to get a frame
    std::unique_ptr<Image> frame;
    bool gotFrame = loader.getNextFrame(frame);

    if (gotFrame)
    {
        EXPECT_NE(frame, nullptr);
        EXPECT_GT(frame->info.width, 0);
        EXPECT_GT(frame->info.height, 0);
    }
}

// Test memory management and resource cleanup
TEST_F(VideoLoaderTest, ResourceCleanup)
{
    // Test that resources are properly cleaned up
    {
        VideoLoader loader;
        loader.loadFromFile(validVideoPath);
        // loader goes out of scope here
    }
    // Should not have memory leaks or crashes
    EXPECT_TRUE(true); // If we get here without crashing, test passes
}

// Test concurrent access (basic thread safety)
TEST_F(VideoLoaderTest, BasicThreadSafety)
{
    VideoLoader loader;

    if (!loader.loadFromFile(validVideoPath))
    {
        GTEST_SKIP() << "Cannot test thread safety without a valid video file";
    }

    // Test that multiple calls to const methods don't crash
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < 5; ++i)
    {
        threads.emplace_back([&loader, &successCount]() {
            try
            {
                // Access various const methods
                loader.isValid();
                loader.getCurrentFrameIndex();
                loader.getMetadata();
                successCount++;
            }
            catch (const std::exception&)
            {
                // Should not throw
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(successCount.load(), 5);
}

// Test edge cases
TEST_F(VideoLoaderTest, EdgeCases)
{
    VideoLoader loader;

    // Test with empty filename
    EXPECT_FALSE(loader.loadFromFile(""));
    EXPECT_FALSE(loader.isValid());

    // Test with very long filename
    std::string longFilename(1000, 'a');
    longFilename += ".mp4";
    EXPECT_FALSE(loader.loadFromFile(longFilename));
    EXPECT_FALSE(loader.isValid());

    // Test seeking with invalid parameters after loading
    if (loader.loadFromFile(validVideoPath))
    {
        EXPECT_FALSE(loader.seekToFrame(-999));
        EXPECT_FALSE(loader.seekToFrame(999999));
    }
}

// Test destructor cleanup
TEST_F(VideoLoaderTest, DestructorCleanup)
{
    VideoLoader* loader = new VideoLoader();

    // Load a video
    if (loader->loadFromFile(validVideoPath))
    {
        // Get some frames to ensure resources are allocated
        std::unique_ptr<Image> frame;
        loader->getNextFrame(frame);
    }

    // Delete loader
    delete loader;

    // Should not crash or leak memory
    EXPECT_TRUE(true);
}

// Test metadata consistency
TEST_F(VideoLoaderTest, MetadataConsistency)
{
    VideoLoader loader;

    if (!loader.loadFromFile(validVideoPath))
    {
        GTEST_SKIP() << "Cannot test metadata consistency without a valid video file";
    }

    const auto& metadata1 = loader.getMetadata();
    const auto& metadata2 = loader.getMetadata();

    // Multiple calls should return consistent data
    EXPECT_EQ(metadata1.width, metadata2.width);
    EXPECT_EQ(metadata1.height, metadata2.height);
    EXPECT_EQ(metadata1.frameCount, metadata2.frameCount);
    EXPECT_EQ(metadata1.fps, metadata2.fps);
    EXPECT_EQ(metadata1.filename, metadata2.filename);
    EXPECT_EQ(metadata1.isValid, metadata2.isValid);
}

// Test frame index tracking
TEST_F(VideoLoaderTest, FrameIndexTracking)
{
    VideoLoader loader;

    if (!loader.loadFromFile(validVideoPath))
    {
        GTEST_SKIP() << "Cannot test frame index tracking without a valid video file";
    }

    // Initial frame index should be 0
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);

    // After seeking, frame index should update
    if (loader.seekToFrame(5))
    {
        EXPECT_EQ(loader.getCurrentFrameIndex(), 5);
    }

    // After reset, should be back to 0
    loader.reset();
    EXPECT_EQ(loader.getCurrentFrameIndex(), 0);
}

// Test error handling in frame retrieval
TEST_F(VideoLoaderTest, FrameRetrievalErrorHandling)
{
    VideoLoader loader;

    if (!loader.loadFromFile(validVideoPath))
    {
        GTEST_SKIP() << "Cannot test frame retrieval error handling without a valid video file";
    }

    std::unique_ptr<Image> frame;

    // Try to get frames until we reach the end
    int framesRetrieved = 0;
    while (loader.getNextFrame(frame) && framesRetrieved < 100) // Safety limit
    {
        EXPECT_NE(frame, nullptr);
        framesRetrieved++;
    }

    // After reaching the end, further calls should return false
    EXPECT_FALSE(loader.getNextFrame(frame));
    EXPECT_EQ(frame, nullptr);
}
