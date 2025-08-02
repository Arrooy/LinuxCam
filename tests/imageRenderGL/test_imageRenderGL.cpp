#include <gtest/gtest.h>

#include <memory>
#include <vector>


#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/imageRenderGL.h"

using namespace linuxface;

class MockGLContext
{
  public:
    static bool initialize()
    {
        // Check for headless environment (CI/CD compatibility)
        if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr)
        {
            // Try to initialize anyway for environments that support headless OpenGL
            // but mark as potentially unavailable
        }

        if (glfwInit() != GLFW_TRUE)
        {
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hidden window for testing

        window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
        if (!window)
        {
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window);

        if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress))
        {
            glfwDestroyWindow(window);
            glfwTerminate();
            return false;
        }

        return true;
    }

    static void cleanup()
    {
        if (window)
        {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
    }

  private:
    static GLFWwindow* window;
};

GLFWwindow* MockGLContext::window = nullptr;

class ImageRenderGLTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        // Check for headless environment and skip if no graphics available
        if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr)
        {
            // In CI/CD environments, this will be caught by individual test checks
            // Some CI environments provide Xvfb, so we still try initialization
        }
        
        // Initialize OpenGL context once for all tests
        glContextAvailable = MockGLContext::initialize();
        if (!glContextAvailable)
        {
            // Will be handled by individual tests with GTEST_SKIP
        }
    }

    static void TearDownTestSuite() 
    { 
        if (glContextAvailable)
        {
            MockGLContext::cleanup(); 
        }
    }

    void SetUp() override
    {
        // Skip test if OpenGL context is not available
        if (!glContextAvailable)
        {
            GTEST_SKIP() << "OpenGL context not available - likely headless environment or missing graphics drivers";
        }
        
        renderer = std::make_unique<ImageRenderGL>();

        // Create test image
        testImageSize = 100 * 100 * 3; // Small image for testing
        testImage = std::make_shared<Image>(testImageSize);
        testImage->info.width = 100;
        testImage->info.height = 100;
        testImage->info.pixelSizeBytes = 3;
        testImage->info.format = ImageFormat::RGB;
        testImage->info.filename = "test_image.jpg";
        testImage->info.layer = 1;
        testImage->info.textureId = 0;

        // Fill with test pattern
        for (size_t i = 0; i < testImageSize; i += 3)
        {
            testImage->data()[i] = static_cast<unsigned char>((i / 3) % 256);             // R
            testImage->data()[i + 1] = static_cast<unsigned char>(((i / 3) + 85) % 256);  // G
            testImage->data()[i + 2] = static_cast<unsigned char>(((i / 3) + 170) % 256); // B
        }

        // Create test gif (note: Gif class is for reading files, not creating)
        // For testing, we'll create a minimal test approach
        testGif = std::make_shared<Gif>("dummy_path.gif"); // This will fail to open, but that's OK for testing

        // Create layer manager
        layerManager = std::make_shared<LayerManager>();
    }

    void TearDown() override
    {
        renderer.reset();
        layerManager.reset();
        testImage.reset();
        testGif.reset();
    }

    std::unique_ptr<ImageRenderGL> renderer;
    std::shared_ptr<LayerManager> layerManager;
    std::shared_ptr<Image> testImage;
    std::shared_ptr<Gif> testGif;
    size_t testImageSize;
    
    static bool glContextAvailable;
};

// Static variable definition  
bool ImageRenderGLTest::glContextAvailable = false;

// Test basic initialization
TEST_F(ImageRenderGLTest, Initialization)
{
    EXPECT_TRUE(renderer->initialize());
}

TEST_F(ImageRenderGLTest, InitializationFailure)
{
    // Create renderer without proper GL context
    // Note: This test might be tricky since we already have a context
    // We can test shutdown and re-initialization
    renderer->shutdown();

    // Re-initialization should still work with existing context
    EXPECT_TRUE(renderer->initialize());
}

TEST_F(ImageRenderGLTest, Shutdown)
{
    ASSERT_TRUE(renderer->initialize());

    // Shutdown should not crash
    renderer->shutdown();

    // Multiple shutdowns should not crash
    renderer->shutdown();
}

// Test layer rendering
TEST_F(ImageRenderGLTest, RenderEmptyLayers)
{
    ASSERT_TRUE(renderer->initialize());

    std::vector<Layer> emptyLayers;

    // Rendering empty layers should not crash
    EXPECT_NO_THROW(renderer->renderLayers(emptyLayers, 800, 600));
}

TEST_F(ImageRenderGLTest, RenderSingleImageLayer)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "test_image";
    imageLayer.img = testImage;
    imageLayer.x = 50.0f;
    imageLayer.y = 50.0f;
    imageLayer.selected = false;
    imageLayer.dirty = true;
    imageLayer.id = 1;

    std::vector<Layer> layers = {imageLayer};

    // Rendering should not crash
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}

TEST_F(ImageRenderGLTest, RenderSingleGifLayer)
{
    ASSERT_TRUE(renderer->initialize());

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.name = "test_gif";
    gifLayer.gif = testGif;
    gifLayer.gifFrameIndex = 0;
    gifLayer.x = 100.0f;
    gifLayer.y = 100.0f;
    gifLayer.selected = false;
    gifLayer.dirty = true;
    gifLayer.id = 2;

    std::vector<Layer> layers = {gifLayer};

    // Rendering should not crash
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}


TEST_F(ImageRenderGLTest, RenderMultipleLayers)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "image";
    imageLayer.img = testImage;
    imageLayer.x = 0.0f;
    imageLayer.y = 0.0f;
    imageLayer.id = 1;
    imageLayer.dirty = true;

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.name = "gif";
    gifLayer.gif = testGif;
    gifLayer.x = 200.0f;
    gifLayer.y = 0.0f;
    gifLayer.id = 2;
    gifLayer.dirty = true;

    std::vector<Layer> layers = {imageLayer, gifLayer};

    // Rendering multiple layers should not crash
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}


// Test layer caching behavior
TEST_F(ImageRenderGLTest, LayerCaching)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "cached_image";
    imageLayer.img = testImage;
    imageLayer.x = 0.0f;
    imageLayer.y = 0.0f;
    imageLayer.id = 1;
    imageLayer.dirty = true; // First render should create cache

    std::vector<Layer> layers = {imageLayer};

    // First render - should create texture cache
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));

    // Mark as clean for second render
    layers[0].dirty = false;

    // Second render - should use cached texture
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}

TEST_F(ImageRenderGLTest, LayerCacheInvalidation)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "invalidated_image";
    imageLayer.img = testImage;
    imageLayer.x = 0.0f;
    imageLayer.y = 0.0f;
    imageLayer.id = 1;
    imageLayer.dirty = true;

    std::vector<Layer> layers = {imageLayer};

    // First render
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));

    // Modify image data and mark dirty
    testImage->data()[0] = 255; // Change first pixel
    layers[0].dirty = true;

    // Second render should recreate texture
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}

// Test different window sizes
TEST_F(ImageRenderGLTest, DifferentWindowSizes)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "test_image";
    imageLayer.img = testImage;
    imageLayer.x = 10.0f;
    imageLayer.y = 10.0f;
    imageLayer.id = 1;
    imageLayer.dirty = true;

    std::vector<Layer> layers = {imageLayer};

    // Test various window sizes
    std::vector<std::pair<int, int>> windowSizes = {
        {640,  480 },
        {800,  600 },
        {1024, 768 },
        {1920, 1080},
        {100,  100 }, // Very small
        {4000, 2000}  // Very large
    };

    for (const auto& size : windowSizes)
    {
        EXPECT_NO_THROW(renderer->renderLayers(layers, size.first, size.second))
            << "Failed to render at size " << size.first << "x" << size.second;
    }
}

// Test edge cases
TEST_F(ImageRenderGLTest, NullImageLayer)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "null_image";
    imageLayer.img = nullptr; // Null image
    imageLayer.id = 1;

    std::vector<Layer> layers = {imageLayer};

    // Should not crash with null image
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}

TEST_F(ImageRenderGLTest, NullGifLayer)
{
    ASSERT_TRUE(renderer->initialize());

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.name = "null_gif";
    gifLayer.gif = nullptr; // Null gif
    gifLayer.id = 1;

    std::vector<Layer> layers = {gifLayer};

    // Should not crash with null gif
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}

TEST_F(ImageRenderGLTest, EmptyGifLayer)
{
    ASSERT_TRUE(renderer->initialize());

    auto emptyGif = std::make_shared<Gif>("empty.gif");
    // Don't add any frames

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.name = "empty_gif";
    gifLayer.gif = emptyGif;
    gifLayer.id = 1;

    std::vector<Layer> layers = {gifLayer};

    // Should not crash with empty gif
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));
}



// Test texture management
TEST_F(ImageRenderGLTest, TextureCleanup)
{
    ASSERT_TRUE(renderer->initialize());

    // Create multiple layers to test texture management
    std::vector<Layer> layers;
    for (int i = 0; i < 5; ++i)
    {
        Layer layer;
        layer.type = LayerType::Image;
        layer.name = "image_" + std::to_string(i);
        layer.img = testImage; // Share the same test image
        layer.x = static_cast<float>(i * 50);
        layer.y = static_cast<float>(i * 50);
        layer.id = i + 1;
        layer.dirty = true;
        layers.push_back(layer);
    }

    // Render all layers
    EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600));

    // Shutdown should clean up all textures
    EXPECT_NO_THROW(renderer->shutdown());
}

// Test error conditions
TEST_F(ImageRenderGLTest, RenderWithoutInitialization)
{
    // Create new renderer without initialization
    auto uninitializedRenderer = std::make_unique<ImageRenderGL>();

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "test";
    imageLayer.img = testImage;
    imageLayer.id = 1;

    std::vector<Layer> layers = {imageLayer};

    // Should handle rendering without proper initialization gracefully
    EXPECT_NO_THROW(uninitializedRenderer->renderLayers(layers, 800, 600));
}

TEST_F(ImageRenderGLTest, ZeroSizeWindow)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "test";
    imageLayer.img = testImage;
    imageLayer.id = 1;

    std::vector<Layer> layers = {imageLayer};

    // Should handle zero-size windows gracefully
    EXPECT_NO_THROW(renderer->renderLayers(layers, 0, 0));
    EXPECT_NO_THROW(renderer->renderLayers(layers, 100, 0));
    EXPECT_NO_THROW(renderer->renderLayers(layers, 0, 100));
}

// Test layer positioning
TEST_F(ImageRenderGLTest, LayerPositioning)
{
    ASSERT_TRUE(renderer->initialize());

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "positioned_image";
    imageLayer.img = testImage;
    imageLayer.id = 1;
    imageLayer.dirty = true;

    std::vector<Layer> layers = {imageLayer};

    // Test various positions
    std::vector<std::pair<float, float>> positions = {
        {0.0f,    0.0f   }, // Top-left
        {400.0f,  300.0f }, // Center
        {-50.0f,  -50.0f }, // Negative (off-screen)
        {1000.0f, 1000.0f}, // Large positive (off-screen)
        {0.5f,    0.5f   }  // Fractional
    };

    for (const auto& pos : positions)
    {
        layers[0].x = pos.first;
        layers[0].y = pos.second;

        EXPECT_NO_THROW(renderer->renderLayers(layers, 800, 600))
            << "Failed to render at position (" << pos.first << ", " << pos.second << ")";
    }
}
