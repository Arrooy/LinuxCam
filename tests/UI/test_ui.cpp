#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "LinuxFace/Image/mediaManager.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/cameraManager.h"
#include "LinuxFace/imageRenderGL.h"
#include "LinuxFace/ui.h"

using namespace linuxface;

// Mock GLFW context for UI testing
class MockUIContext
{
  public:
    static bool initialize()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        window = glfwCreateWindow(800, 600, "UI Test", nullptr, nullptr);
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

    static GLFWwindow* getWindow() { return window; }

  private:
    static GLFWwindow* window;
};

GLFWwindow* MockUIContext::window = nullptr;

// Test fixture for UI tests
class UITest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        ASSERT_TRUE(MockUIContext::initialize()) << "Failed to initialize UI context for testing";
    }

    static void TearDownTestSuite() { MockUIContext::cleanup(); }

    void SetUp() override
    {
        layerManager = std::make_shared<LayerManager>();
        imageRender = std::make_shared<ImageRenderGL>();
        mediaManager = std::make_shared<MediaManager>(imageRender);
        cameraManager = std::make_shared<CameraManager>();

        ui = std::make_unique<UI>(layerManager);

        // Connect dependencies
        ui->connect(cameraManager);
        ui->connect(mediaManager);
    }

    void TearDown() override
    {
        ui.reset();
        layerManager.reset();
        mediaManager.reset();
        cameraManager.reset();
        imageRender.reset();
    }

    std::unique_ptr<UI> ui;
    std::shared_ptr<LayerManager> layerManager;
    std::shared_ptr<MediaManager> mediaManager;
    std::shared_ptr<CameraManager> cameraManager;
    std::shared_ptr<ImageRenderGL> imageRender;
};

// Test basic UI initialization
TEST_F(UITest, Initialization)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_NE(window, nullptr);

    EXPECT_TRUE(ui->initialize(window, "#version 130"));
}

TEST_F(UITest, InitializationWithNullWindow)
{
    // Should handle null window gracefully
    EXPECT_FALSE(ui->initialize(nullptr, "#version 130"));
}

TEST_F(UITest, Shutdown)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Shutdown should not crash
    EXPECT_NO_THROW(ui->shutdown());

    // Multiple shutdowns should not crash
    EXPECT_NO_THROW(ui->shutdown());
}

// Test UI frame cycle
TEST_F(UITest, FrameCycle)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Test frame cycle operations
    EXPECT_NO_THROW(ui->newFrame());
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
}

TEST_F(UITest, MultipleFrameCycles)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Test multiple frame cycles
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NO_THROW(ui->newFrame()) << "Failed on frame " << i;
        EXPECT_NO_THROW(ui->paint()) << "Failed on frame " << i;
        EXPECT_NO_THROW(ui->render()) << "Failed on frame " << i;
    }
}

// Test loading screen
TEST_F(UITest, LoadingScreen)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Loading screen should not crash
    EXPECT_NO_THROW(ui->loadingScreen());
}

// Test keyboard handling
TEST_F(UITest, KeyboardHandling)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Keyboard handling should not crash
    EXPECT_NO_THROW(ui->handleKeyboard());
}

// Test layer dragging
TEST_F(UITest, LayerDragging)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Add a test layer to the layer manager
    Layer testLayer;
    testLayer.type = LayerType::Image;
    testLayer.name = "test_layer";
    testLayer.x = 100.0f;
    testLayer.y = 100.0f;
    testLayer.id = 1;

    layerManager->addLayer(testLayer);

    // Layer dragging should not crash
    EXPECT_NO_THROW(ui->handleLayerDragging());
}

// Test dependency connections
TEST_F(UITest, CameraManagerConnection)
{
    auto newCameraManager = std::make_shared<CameraManager>();

    // Should be able to connect camera manager
    EXPECT_NO_THROW(ui->connect(newCameraManager));
}

TEST_F(UITest, MediaManagerConnection)
{
    auto newMediaManager = std::make_shared<MediaManager>(imageRender);

    // Should be able to connect media manager
    EXPECT_NO_THROW(ui->connect(newMediaManager));
}

TEST_F(UITest, NullDependencyConnections)
{
    // Should handle null dependencies gracefully
    EXPECT_NO_THROW(ui->connect(std::shared_ptr<CameraManager>(nullptr)));
    EXPECT_NO_THROW(ui->connect(std::shared_ptr<MediaManager>(nullptr)));
}

// Test UI state management
TEST_F(UITest, InitialState)
{
    // UI should start in a valid initial state
    EXPECT_NE(ui.get(), nullptr);
}

TEST_F(UITest, StateAfterInitialization)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // After initialization, UI should be ready for operation
    EXPECT_NO_THROW(ui->newFrame());
}

// Test error handling
TEST_F(UITest, OperationsWithoutInitialization)
{
    // Operations without initialization should not crash
    EXPECT_NO_THROW(ui->newFrame());
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
    EXPECT_NO_THROW(ui->handleKeyboard());
    EXPECT_NO_THROW(ui->handleLayerDragging());
    EXPECT_NO_THROW(ui->loadingScreen());
}

TEST_F(UITest, ReinitializationAfterShutdown)
{
    GLFWwindow* window = MockUIContext::getWindow();

    // Initialize
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Shutdown
    ui->shutdown();

    // Re-initialize
    EXPECT_TRUE(ui->initialize(window, "#version 130"));
}

// Test memory management
TEST_F(UITest, MemoryManagement)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Add multiple layers to test memory usage
    for (int i = 0; i < 10; ++i)
    {
        Layer layer;
        layer.type = LayerType::Text;
        layer.name = "layer_" + std::to_string(i);
        layer.textContent = "Test layer " + std::to_string(i);
        layer.fontSize = 16.0f;
        layer.x = static_cast<float>(i * 20);
        layer.y = static_cast<float>(i * 20);
        layer.id = i + 1;

        layerManager->addLayer(layer);
    }

    // Multiple frame cycles should not cause memory issues
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NO_THROW(ui->newFrame());
        EXPECT_NO_THROW(ui->paint());
        EXPECT_NO_THROW(ui->render());
    }

    // Cleanup should be automatic
    layerManager->clearLayers();
}

// Test UI with different layer types
TEST_F(UITest, UIWithImageLayers)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Create a test image
    auto testImage = std::make_shared<Image>(100 * 100 * 3);
    testImage->info.width = 100;
    testImage->info.height = 100;
    testImage->info.pixelSizeBytes = 3;
    testImage->info.format = ImageFormat::RGB;
    testImage->info.filename = "test.jpg";

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "image_layer";
    imageLayer.img = testImage;
    imageLayer.x = 50.0f;
    imageLayer.y = 50.0f;
    imageLayer.id = 1;

    layerManager->addLayer(imageLayer);

    // UI operations should work with image layers
    EXPECT_NO_THROW(ui->newFrame());
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
}

TEST_F(UITest, UIWithTextLayers)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.name = "text_layer";
    textLayer.textContent = "Hello, World!";
    textLayer.fontSize = 24.0f;
    textLayer.textColor = IM_COL32(255, 255, 255, 255);
    textLayer.x = 100.0f;
    textLayer.y = 100.0f;
    textLayer.id = 1;

    layerManager->addLayer(textLayer);

    // UI operations should work with text layers
    EXPECT_NO_THROW(ui->newFrame());
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
}

TEST_F(UITest, UIWithMixedLayers)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Create mixed layer types
    auto testImage = std::make_shared<Image>(50 * 50 * 3);
    testImage->info.width = 50;
    testImage->info.height = 50;
    testImage->info.pixelSizeBytes = 3;
    testImage->info.format = ImageFormat::RGB;

    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "image";
    imageLayer.img = testImage;
    imageLayer.x = 0.0f;
    imageLayer.y = 0.0f;
    imageLayer.id = 1;

    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.name = "text";
    textLayer.textContent = "Overlay";
    textLayer.fontSize = 16.0f;
    textLayer.x = 60.0f;
    textLayer.y = 60.0f;
    textLayer.id = 2;

    layerManager->addLayer(imageLayer);
    layerManager->addLayer(textLayer);

    // UI should handle mixed layer types
    EXPECT_NO_THROW(ui->newFrame());
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
}

// Test UI stability
TEST_F(UITest, StabilityTest)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Create a complex scene
    for (int i = 0; i < 5; ++i)
    {
        Layer textLayer;
        textLayer.type = LayerType::Text;
        textLayer.name = "text_" + std::to_string(i);
        textLayer.textContent = "Layer " + std::to_string(i);
        textLayer.fontSize = 12.0f + i * 2;
        textLayer.x = static_cast<float>(i * 30);
        textLayer.y = static_cast<float>(i * 30);
        textLayer.id = i + 1;

        layerManager->addLayer(textLayer);
    }

    // Run many frame cycles to test stability
    for (int frame = 0; frame < 20; ++frame)
    {
        EXPECT_NO_THROW(ui->newFrame()) << "Frame " << frame;
        EXPECT_NO_THROW(ui->paint()) << "Frame " << frame;
        EXPECT_NO_THROW(ui->render()) << "Frame " << frame;
        EXPECT_NO_THROW(ui->handleKeyboard()) << "Frame " << frame;
        EXPECT_NO_THROW(ui->handleLayerDragging()) << "Frame " << frame;
    }
}

// Test UI construction and destruction
TEST_F(UITest, ConstructionDestruction)
{
    // Test creating and destroying UI multiple times
    for (int i = 0; i < 3; ++i)
    {
        auto testLayerManager = std::make_shared<LayerManager>();
        auto testUI = std::make_unique<UI>(testLayerManager);

        EXPECT_NE(testUI.get(), nullptr) << "Iteration " << i;

        GLFWwindow* window = MockUIContext::getWindow();
        EXPECT_TRUE(testUI->initialize(window, "#version 130")) << "Iteration " << i;

        testUI->shutdown();
        testUI.reset();
        testLayerManager.reset();
    }
}

// Test edge cases
TEST_F(UITest, EmptyLayerManager)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    // Ensure layer manager is empty
    layerManager->clearLayers();
    EXPECT_TRUE(layerManager->getLayers().empty());

    // UI should work with empty layer manager
    EXPECT_NO_THROW(ui->newFrame());
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
    EXPECT_NO_THROW(ui->handleLayerDragging());
}

TEST_F(UITest, LayerModificationDuringRender)
{
    GLFWwindow* window = MockUIContext::getWindow();
    ASSERT_TRUE(ui->initialize(window, "#version 130"));

    Layer testLayer;
    testLayer.type = LayerType::Text;
    testLayer.name = "test";
    testLayer.textContent = "Test";
    testLayer.fontSize = 16.0f;
    testLayer.id = 1;

    layerManager->addLayer(testLayer);

    // Start frame
    ui->newFrame();

    // Modify layer during frame
    auto& layers = layerManager->getLayers();
    if (!layers.empty())
    {
        layers[0].textContent = "Modified";
        layers[0].x = 200.0f;
        layers[0].y = 200.0f;
    }

    // Should handle modifications gracefully
    EXPECT_NO_THROW(ui->paint());
    EXPECT_NO_THROW(ui->render());
}
