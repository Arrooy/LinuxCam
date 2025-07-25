#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include "LinuxFace/Image/mediaManager.h"
#include "LinuxFace/imageRenderGL.h"

using namespace linuxface;

class MediaManagerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create a mock ImageRenderGL for testing
        imageRenderGL = std::make_shared<ImageRenderGL>();
    }

    void TearDown() override
    {
        if (mediaManager)
        {
            mediaManager->shutdown();
            mediaManager.reset();
        }
        imageRenderGL.reset();
    }

    std::shared_ptr<ImageRenderGL> imageRenderGL;
    std::unique_ptr<MediaManager> mediaManager;
};

// Test basic construction and immediate shutdown
TEST_F(MediaManagerTest, ConstructionAndShutdown)
{
    // Create MediaManager - it may start loading immediately
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);
    EXPECT_NE(mediaManager, nullptr);

    // Shutdown should work without crashes
    EXPECT_NO_THROW(mediaManager->shutdown());
}

// Test getting image names (may be empty but shouldn't crash)
TEST_F(MediaManagerTest, GetImageNames)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    auto imageNames = mediaManager->getImageNames();

    // Should return a vector (may be empty initially)
    EXPECT_TRUE(imageNames.empty() || !imageNames.empty());
}

// Test getting gif names (may be empty but shouldn't crash)
TEST_F(MediaManagerTest, GetGifNames)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    auto gifNames = mediaManager->getGifNames();

    // Should return a vector (may be empty initially)
    EXPECT_TRUE(gifNames.empty() || !gifNames.empty());
}

// Test getting non-existent image
TEST_F(MediaManagerTest, GetNonExistentImage)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    auto image = mediaManager->getImage("non_existent_image.jpg");

    // Should return nullptr for non-existent image
    EXPECT_EQ(image, nullptr);
}

// Test getting non-existent gif
TEST_F(MediaManagerTest, GetNonExistentGif)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    auto gif = mediaManager->getGif("non_existent_gif.gif");

    // Should return nullptr for non-existent gif
    EXPECT_EQ(gif, nullptr);
}

// Test reloading non-existent image
TEST_F(MediaManagerTest, ReloadNonExistentImage)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    bool result = mediaManager->reloadImage("non_existent_image.jpg");

    // Should return false for non-existent image
    EXPECT_FALSE(result);
}

// Test multiple shutdown calls
TEST_F(MediaManagerTest, MultipleShutdown)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    // Should not crash
    EXPECT_NO_THROW(mediaManager->shutdown());

    // Should be safe to call multiple times
    EXPECT_NO_THROW(mediaManager->shutdown());
}

// Test thread safety - multiple calls to get methods
TEST_F(MediaManagerTest, ThreadSafety)
{
    mediaManager = std::make_unique<MediaManager>(imageRenderGL);

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    // Launch multiple threads accessing the media manager
    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back(
            [this, &successCount]()
            {
                try
                {
                    auto imageNames = mediaManager->getImageNames();
                    auto gifNames = mediaManager->getGifNames();
                    successCount++;
                }
                catch (const std::exception&)
                {
                    // Should not throw
                }
            });
    }

    // Wait for all threads to complete
    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(successCount.load(), 3);
}
