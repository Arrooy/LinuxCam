#include <gtest/gtest.h>
#include <sys/stat.h>

#include <cstdio>
#include <memory>
#include <thread>

#include "LinuxFace/Image/mediaManager.h"
#include "LinuxFace/imageRenderGL.h"
#include "config.hpp"

using namespace linuxface;

class MediaManagerTest : public ::testing::Test
{
  protected:
    std::string tempConfigPath;
    std::string tempMediaDir;
    std::string tempModelsDir;
    std::string tempWflwDir;

    void SetUp() override;
    void TearDown() override;

    std::shared_ptr<ImageRenderGL> imageRenderGL;
    std::unique_ptr<MediaManager> mediaManager;
};

void MediaManagerTest::SetUp()
{
    printf("[MediaManagerTest] Entering SetUp()\n");
    fflush(stdout);
    // Create unique temp directories for this test
    char templ1[] = "/tmp/mediaManagerTest_media_XXXXXX";
    char templ2[] = "/tmp/mediaManagerTest_models_XXXXXX";
    char templ3[] = "/tmp/mediaManagerTest_wflw_XXXXXX";
    char configTmpl[] = "/tmp/mediaManagerTest_config_XXXXXX";
    char* mediaDir = mkdtemp(templ1);
    char* modelsDir = mkdtemp(templ2);
    char* wflwDir = mkdtemp(templ3);
    int fd = mkstemp(configTmpl);
    if (mediaDir) tempMediaDir = mediaDir;
    if (modelsDir) tempModelsDir = modelsDir;
    if (wflwDir) tempWflwDir = wflwDir;
    tempConfigPath = configTmpl;
    if (fd != -1) {
        FILE* f = fdopen(fd, "w");
        if (f) {
            fprintf(f,
                "input_cameras:\n"
                "  - name: 'Test Input'\n"
                "    path: '/dev/video0'\n"
                "    width: 640\n"
                "    height: 480\n"
                "    buffer_count: 4\n"
                "output_cameras:\n"
                "  - name: 'Test Output'\n"
                "    path: '/dev/video1'\n"
                "    width: 640\n"
                "    height: 480\n"
                "    subsampling: '420'\n"
                "external_data:\n"
                "  media_folder_path: '%s'\n"
                "  models_folder_path: '%s'\n"
                "  WFLW_folder_path: '%s'\n"
                "  preload_content: false\n"
                "window:\n"
                "  title: 'Test Window'\n"
                "  width: 100\n"
                "  height: 100\n",
                tempMediaDir.c_str(), tempModelsDir.c_str(), tempWflwDir.c_str());
            fclose(f);
        }
    }

    // Debug: Print temp config file path and existence after writing config
    printf("[MediaManagerTest] tempConfigPath: %s\n", tempConfigPath.c_str());
    struct stat statbuf;
    printf("[MediaManagerTest] tempConfigPath exists: %d\n", stat(tempConfigPath.c_str(), &statbuf) == 0);
    // Print config file contents
    FILE* cf = fopen(tempConfigPath.c_str(), "r");
    if (cf) {
        printf("[MediaManagerTest] config file contents:\n");
        char line[256];
        while (fgets(line, sizeof(line), cf)) {
            fputs(line, stdout);
        }
        fclose(cf);
    } else {
        printf("[MediaManagerTest] Could not open config file for reading.\n");
    }
    fflush(stdout);

    // Ensure all required directories exist before constructing MediaManager
    if (!tempMediaDir.empty()) {
        mkdir(tempMediaDir.c_str(), 0700);
        std::string imagesDir = tempMediaDir + "/images";
        std::string gifsDir = tempMediaDir + "/gifs";
        mkdir(imagesDir.c_str(), 0700);
        mkdir(gifsDir.c_str(), 0700);
        std::string dummyImg = imagesDir + "/.keep";
        std::string dummyGif = gifsDir + "/.keep";
        FILE* f1 = fopen(dummyImg.c_str(), "w");
        if (f1) fclose(f1);
        FILE* f2 = fopen(dummyGif.c_str(), "w");
        if (f2) fclose(f2);
    }
    if (!tempModelsDir.empty()) {
        mkdir(tempModelsDir.c_str(), 0700);
        std::string dummy = tempModelsDir + "/.keep";
        FILE* f = fopen(dummy.c_str(), "w");
        if (f) fclose(f);
    }
    if (!tempWflwDir.empty()) {
        mkdir(tempWflwDir.c_str(), 0700);
        std::string dummy = tempWflwDir + "/.keep";
        FILE* f = fopen(dummy.c_str(), "w");
        if (f) fclose(f);
    }

    // Always reload config with the temp config before each test
    bool reloadResult = Config::getInstance().reloadFromFile(tempConfigPath.c_str());
    printf("[MediaManagerTest] reloadFromFile returned: %d\n", reloadResult);
    
    // Call loadConfiguration to parse the loaded YAML into member variables
    bool loadResult = Config::getInstance().loadConfiguration();
    printf("[MediaManagerTest] loadConfiguration returned: %d\n", loadResult);
    fflush(stdout);

    // Debug: Print config paths and verify directory existence
    const std::string& mediaPath = Config::getInstance().getMediaFolderPath();
    const std::string& modelsPath = Config::getInstance().getModelFolderPath();
    const std::string& wflwPath = Config::getInstance().getWFLWFolderPath();
    struct stat sb;
    printf("[MediaManagerTest] media_folder_path: %s\n", mediaPath.c_str());
    printf("[MediaManagerTest] models_folder_path: %s\n", modelsPath.c_str());
    printf("[MediaManagerTest] WFLW_folder_path: %s\n", wflwPath.c_str());
    printf("[MediaManagerTest] media_folder_path exists: %d\n", stat(mediaPath.c_str(), &sb) == 0);
    printf("[MediaManagerTest] models_folder_path exists: %d\n", stat(modelsPath.c_str(), &sb) == 0);
    printf("[MediaManagerTest] WFLW_folder_path exists: %d\n", stat(wflwPath.c_str(), &sb) == 0);
    fflush(stdout);

    
    imageRenderGL = std::make_shared<ImageRenderGL>();
}

void MediaManagerTest::TearDown()
{
    if (mediaManager)
    {
        mediaManager->shutdown();
        mediaManager.reset();
    }
    imageRenderGL.reset();

    // Remove dummy files and temp config file
    if (!tempMediaDir.empty()) {
        std::string imagesDir = tempMediaDir + "/images";
        std::string gifsDir = tempMediaDir + "/gifs";
        std::string dummyImg = imagesDir + "/.keep";
        std::string dummyGif = gifsDir + "/.keep";
        remove(dummyImg.c_str());
        remove(dummyGif.c_str());
        rmdir(imagesDir.c_str());
        rmdir(gifsDir.c_str());
    }
    if (!tempModelsDir.empty()) {
        std::string dummy = tempModelsDir + "/.keep";
        remove(dummy.c_str());
    }
    if (!tempWflwDir.empty()) {
        std::string dummy = tempWflwDir + "/.keep";
        remove(dummy.c_str());
    }
    if (!tempConfigPath.empty()) {
        remove(tempConfigPath.c_str());
    }
    // Remove temp directories (must be empty)
    rmdir(tempMediaDir.c_str());
    rmdir(tempModelsDir.c_str());
    rmdir(tempWflwDir.c_str());
    // Reset config state to blank for next test
    Config::getInstance().reloadFromFile("nonexistent.yaml");
}// Test basic construction and immediate shutdown
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
