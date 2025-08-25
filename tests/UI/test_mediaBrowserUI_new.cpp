#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/imageRenderGL.h"

using namespace linuxface;

// Simplified LayerManager tests to avoid filesystem issues with MediaManager/MediaBrowserUI
// These tests focus on the Layer functionality that MediaBrowserUI depends on

// Test fixture for Layer integration tests
class LayerIntegrationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create dependencies without MediaManager (to avoid filesystem access)
        layerManager = std::make_shared<LayerManager>();

        // Create test image
        testImageSize = 100 * 100 * 3;
        testImage = std::make_shared<Image>(testImageSize);
        testImage->info.width = 100;
        testImage->info.height = 100;
        testImage->info.pixelSizeBytes = 3;
        testImage->info.format = ImageFormat::RGB;
        testImage->info.filename = "test_image.jpg";
        testImage->info.layer = 1;

        // Fill with test pattern
        for (size_t i = 0; i < testImageSize; i += 3)
        {
            testImage->data()[i] = static_cast<unsigned char>(i % 256);
            testImage->data()[i + 1] = static_cast<unsigned char>((i + 1) % 256);
            testImage->data()[i + 2] = static_cast<unsigned char>((i + 2) % 256);
        }

        // Create test gif (note: Gif class is for reading files, not creating)
        testGif = std::make_shared<Gif>("dummy_path.gif"); // This will fail to open, but that's OK for testing
    }

    void TearDown() override
    {
        layerManager.reset();
        testImage.reset();
        testGif.reset();
    }

    std::shared_ptr<LayerManager> layerManager;
    std::shared_ptr<Image> testImage;
    std::shared_ptr<Gif> testGif;
    size_t testImageSize;
};

// Test Layer functionality that MediaBrowserUI depends on
TEST_F(LayerIntegrationTest, LayerBasicFunctionality)
{
    // Test adding a layer
    Layer layer;
    layer.type = LayerType::IMAGE;
    layer.name = "test_layer";
    layer.img = testImage;
    layer.selected = true;
    layer.dirty = false;
    layer.id = 1;

    layerManager->addLayer(layer);

    // Verify layer was added
    EXPECT_EQ(layerManager->getLayers().size(), 1);

    // Verify layer properties
    const auto& layers = layerManager->getLayers();
    EXPECT_EQ(layers[0].name, "test_layer");
    EXPECT_TRUE(layers[0].selected);
    EXPECT_EQ(layers[0].type, LayerType::IMAGE);
}

TEST_F(LayerIntegrationTest, LayerSelection)
{
    // Add multiple layers
    Layer layer1;
    layer1.type = LayerType::IMAGE;
    layer1.name = "layer1";
    layer1.img = testImage;
    layer1.selected = false;
    layer1.id = 1;

    Layer layer2;
    layer2.type = LayerType::TEXT;
    layer2.name = "layer2";
    layer2.textContent = "Text";
    layer2.selected = true; // This one is selected
    layer2.id = 2;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    // Find selected layer (simulating MediaBrowserUI::getSelectedLayer)
    const Layer* selectedLayer = nullptr;
    auto& layers = layerManager->getLayers();
    for (auto& layer : layers)
    {
        if (layer.selected)
        {
            selectedLayer = &layer;
            break;
        }
    }

    ASSERT_NE(selectedLayer, nullptr);
    EXPECT_EQ(selectedLayer->id, 2);
    EXPECT_EQ(selectedLayer->name, "layer2");
    EXPECT_TRUE(selectedLayer->selected);
}

TEST_F(LayerIntegrationTest, LayerManipulation)
{
    // Add a layer
    Layer layer;
    layer.type = LayerType::IMAGE;
    layer.name = "test_layer";
    layer.img = testImage;
    layer.selected = true;
    layer.dirty = false;
    layer.id = 1;

    layerManager->addLayer(layer);

    // Verify initial state
    auto& layers = layerManager->getLayers();
    EXPECT_FALSE(layers[0].dirty);

    // Modify layer properties (simulating UI operations)
    layers[0].dirty = true;
    layers[0].x = 100.0f;
    layers[0].y = 200.0f;

    // Verify changes
    EXPECT_TRUE(layers[0].dirty);
    EXPECT_EQ(layers[0].x, 100.0f);
    EXPECT_EQ(layers[0].y, 200.0f);
}

TEST_F(LayerIntegrationTest, LayerProperties)
{
    Layer imageLayer;
    imageLayer.type = LayerType::IMAGE;
    imageLayer.name = "test_image";
    imageLayer.img = testImage;
    imageLayer.selected = true;
    imageLayer.x = 50.0f;
    imageLayer.y = 75.0f;
    imageLayer.id = 1;
    imageLayer.img->info.layer = 0; // Explicitly set layer number for test

    layerManager->addLayer(imageLayer);

    auto& layers = layerManager->getLayers();
    const Layer& layer = layers[0];

    // Test that we can access layer properties
    EXPECT_EQ(layer.getWidth(), 100);
    EXPECT_EQ(layer.getHeight(), 100);
    EXPECT_EQ(layer.getFormat(), ImageFormat::RGB);
    EXPECT_GT(layer.getSize(), 0);
    EXPECT_EQ(layer.getLayerName(), "test_image.jpg");
    EXPECT_EQ(layer.getLayerNumber(), 0); // Should be 0-based in layer manager
    EXPECT_EQ(layer.x, 50.0f);
    EXPECT_EQ(layer.y, 75.0f);
}

TEST_F(LayerIntegrationTest, TextLayerHandling)
{
    Layer textLayer;
    textLayer.type = LayerType::TEXT;
    textLayer.name = "text_test";
    textLayer.textContent = "Hello, World!";
    // Create text image using the new implementation
    textLayer.img = Layer::createTextImage("Hello, World!", IM_COL32(255, 0, 0, 255), 2);
    textLayer.x = 10.0f;
    textLayer.y = 20.0f;
    textLayer.selected = true;
    textLayer.id = 1;

    layerManager->addLayer(textLayer);

    auto& layers = layerManager->getLayers();
    const Layer& layer = layers[0];

    EXPECT_EQ(layer.type, LayerType::TEXT);
    EXPECT_EQ(layer.textContent, "Hello, World!");
    EXPECT_NE(layer.img, nullptr); // Text image should be created
    EXPECT_GT(layer.getWidth(), 0); // Should have dimensions
    EXPECT_GT(layer.getHeight(), 0);
    EXPECT_EQ(layer.x, 10.0f);
    EXPECT_EQ(layer.y, 20.0f);
}

TEST_F(LayerIntegrationTest, GifLayerHandling)
{
    Layer gifLayer;
    gifLayer.type = LayerType::GIF;
    gifLayer.name = "test_gif";
    gifLayer.gif = testGif;
    gifLayer.selected = true;
    gifLayer.id = 1;

    layerManager->addLayer(gifLayer);

    auto& layers = layerManager->getLayers();
    const Layer& layer = layers[0];

    EXPECT_EQ(layer.type, LayerType::GIF);
    EXPECT_EQ(layer.gif, testGif);
    // Since testGif failed to open dummy file, dimensions should be 0
    EXPECT_EQ(layer.getWidth(), 0);
    EXPECT_EQ(layer.getHeight(), 0);
}

TEST_F(LayerIntegrationTest, MultipleLayerTypes)
{
    // Add different layer types
    Layer imageLayer;
    imageLayer.type = LayerType::IMAGE;
    imageLayer.name = "image_layer";
    imageLayer.img = testImage;
    imageLayer.selected = false;
    imageLayer.id = 1;

    Layer textLayer;
    textLayer.type = LayerType::TEXT;
    textLayer.name = "text_layer";
    textLayer.textContent = "Test Text";
    // Create text image using the new implementation
    textLayer.img = Layer::createTextImage("Test Text");
    textLayer.selected = false;
    textLayer.id = 2;

    Layer gifLayer;
    gifLayer.type = LayerType::GIF;
    gifLayer.name = "gif_layer";
    gifLayer.gif = testGif;
    gifLayer.selected = true; // This one is selected
    gifLayer.id = 3;

    layerManager->addLayer(imageLayer);
    layerManager->addLayer(textLayer);
    layerManager->addLayer(gifLayer);

    EXPECT_EQ(layerManager->getLayers().size(), 3);

    // Find selected layer
    const Layer* selectedLayer = nullptr;
    auto& layers = layerManager->getLayers();
    for (auto& layer : layers)
    {
        if (layer.selected)
        {
            selectedLayer = &layer;
            break;
        }
    }

    ASSERT_NE(selectedLayer, nullptr);
    EXPECT_EQ(selectedLayer->type, LayerType::GIF);
    EXPECT_EQ(selectedLayer->name, "gif_layer");
}

TEST_F(LayerIntegrationTest, LayerOrdering)
{
    // Add layers and test ordering
    for (int i = 0; i < 5; ++i)
    {
        Layer layer;
    layer.type = LayerType::TEXT;
        layer.name = "layer_" + std::to_string(i);
        layer.textContent = "Content " + std::to_string(i);
        layer.selected = (i == 2); // Select middle layer
        layer.id = i + 1;
        layer.layerNumber = i; // Explicitly set layer number for test

        layerManager->addLayer(layer);
    }

    EXPECT_EQ(layerManager->getLayers().size(), 5);

    // Verify ordering
    const auto& layers = layerManager->getLayers();
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(layers[i].name, "layer_" + std::to_string(i));
        EXPECT_EQ(layers[i].getLayerNumber(), i);
    }

    // Find selected layer
    const Layer* selectedLayer = nullptr;
    for (auto& layer : layers)
    {
        if (layer.selected)
        {
            selectedLayer = &layer;
            break;
        }
    }

    ASSERT_NE(selectedLayer, nullptr);
    EXPECT_EQ(selectedLayer->name, "layer_2");
}

TEST_F(LayerIntegrationTest, EmptyTextLayer)
{
    Layer textLayer;
    textLayer.type = LayerType::TEXT;
    textLayer.name = "empty_text";
    textLayer.textContent = ""; // Empty text
    // Empty text should result in null image
    textLayer.img = Layer::createTextImage("");
    textLayer.selected = true;
    textLayer.id = 1;

    layerManager->addLayer(textLayer);

    auto& layers = layerManager->getLayers();
    const Layer& layer = layers[0];

    EXPECT_EQ(layer.textContent, "");
    EXPECT_EQ(layer.getLayerName(), "Text Layer"); // Should default to "Text Layer"
}

TEST_F(LayerIntegrationTest, NullDataHandling)
{
    // Test with null image
    Layer imageLayer;
    imageLayer.type = LayerType::IMAGE;
    imageLayer.name = "null_image";
    imageLayer.img = nullptr;
    imageLayer.selected = true;
    imageLayer.id = 1;

    layerManager->addLayer(imageLayer);

    auto& layers = layerManager->getLayers();
    const Layer& layer = layers[0];

    EXPECT_EQ(layer.img, nullptr);
    EXPECT_EQ(layer.getWidth(), 0);
    EXPECT_EQ(layer.getHeight(), 0);
    EXPECT_EQ(layer.getSize(), 0);
}

TEST_F(LayerIntegrationTest, LayerClearing)
{
    // Add multiple layers
    for (int i = 0; i < 3; ++i)
    {
        Layer layer;
    layer.type = LayerType::TEXT;
        layer.name = "layer_" + std::to_string(i);
        layer.textContent = "Content " + std::to_string(i);
        layer.id = i + 1;

        layerManager->addLayer(layer);
    }

    EXPECT_EQ(layerManager->getLayers().size(), 3);

    // Clear all layers
    layerManager->clearLayers();

    EXPECT_EQ(layerManager->getLayers().size(), 0);
}
