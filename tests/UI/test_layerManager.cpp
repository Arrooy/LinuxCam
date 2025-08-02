#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/UI/layerManager.h"

using namespace linuxface;

// Test fixture for LayerManager tests
class LayerManagerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        layerManager = std::make_unique<LayerManager>();

        // Create test image
        testImageSize = 640 * 480 * 3;
        testImage = std::make_shared<Image>(testImageSize);
        testImage->info.width = 640;
        testImage->info.height = 480;
        testImage->info.pixelSizeBytes = 3;
        testImage->info.format = ImageFormat::RGB;
        testImage->info.filename = "test_image.jpg";
        testImage->info.layer = 1;

        // Fill with test data
        for (size_t i = 0; i < testImageSize; i += 3)
        {
            testImage->data()[i] = static_cast<unsigned char>(i % 256);           // R
            testImage->data()[i + 1] = static_cast<unsigned char>((i + 1) % 256); // G
            testImage->data()[i + 2] = static_cast<unsigned char>((i + 2) % 256); // B
        }

        // Create test gif (note: Gif class is for reading files, not creating)
        // For testing, we'll create a minimal test approach
        testGif = std::make_shared<Gif>("dummy_path.gif"); // This will fail to open, but that's OK for testing
        // We'll test the layer manager functionality without depending on actual gif frames
    }

    void TearDown() override { layerManager.reset(); }

    std::unique_ptr<LayerManager> layerManager;
    std::shared_ptr<Image> testImage;
    std::shared_ptr<Gif> testGif;
    size_t testImageSize;
};

// Test basic layer management
TEST_F(LayerManagerTest, AddLayer)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "test_layer";
    imageLayer.img = testImage;
    imageLayer.x = 10.0f;
    imageLayer.y = 20.0f;

    EXPECT_TRUE(layerManager->getLayers().empty());

    layerManager->addLayer(imageLayer);

    EXPECT_EQ(layerManager->getLayers().size(), 1);
    EXPECT_EQ(layerManager->getLayers()[0].name, "test_layer");
    EXPECT_EQ(layerManager->getLayers()[0].type, LayerType::Image);
    EXPECT_EQ(layerManager->getLayers()[0].x, 10.0f);
    EXPECT_EQ(layerManager->getLayers()[0].y, 20.0f);
}

TEST_F(LayerManagerTest, AddMultipleLayers)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "image_layer";
    imageLayer.img = testImage;

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.name = "gif_layer";
    gifLayer.gif = testGif;

    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.name = "text_layer";
    textLayer.textContent = "Hello World";
    textLayer.fontSize = 24.0f;

    layerManager->addLayer(imageLayer);
    layerManager->addLayer(gifLayer);
    layerManager->addLayer(textLayer);

    EXPECT_EQ(layerManager->getLayers().size(), 3);
    EXPECT_EQ(layerManager->getLayers()[0].type, LayerType::Image);
    EXPECT_EQ(layerManager->getLayers()[1].type, LayerType::Gif);
    EXPECT_EQ(layerManager->getLayers()[2].type, LayerType::Text);
}

TEST_F(LayerManagerTest, RemoveLayer)
{
    Layer layer1;
    layer1.id = 1;
    layer1.name = "layer1";
    layer1.type = LayerType::Image;

    Layer layer2;
    layer2.id = 2;
    layer2.name = "layer2";
    layer2.type = LayerType::Text;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    EXPECT_EQ(layerManager->getLayers().size(), 2);

    layerManager->removeLayer(1);

    EXPECT_EQ(layerManager->getLayers().size(), 1);
    EXPECT_EQ(layerManager->getLayers()[0].id, 2);
    EXPECT_EQ(layerManager->getLayers()[0].name, "layer2");
}

TEST_F(LayerManagerTest, RemoveNonExistentLayer)
{
    Layer layer;
    layer.id = 1;
    layer.name = "test_layer";

    layerManager->addLayer(layer);

    EXPECT_EQ(layerManager->getLayers().size(), 1);

    // Try to remove non-existent layer
    layerManager->removeLayer(999);

    // Should still have the original layer
    EXPECT_EQ(layerManager->getLayers().size(), 1);
    EXPECT_EQ(layerManager->getLayers()[0].id, 1);
}

TEST_F(LayerManagerTest, ClearLayers)
{
    Layer layer1;
    layer1.name = "layer1";
    Layer layer2;
    layer2.name = "layer2";
    Layer layer3;
    layer3.name = "layer3";

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);
    layerManager->addLayer(layer3);

    EXPECT_EQ(layerManager->getLayers().size(), 3);

    layerManager->clearLayers();

    EXPECT_TRUE(layerManager->getLayers().empty());
}

TEST_F(LayerManagerTest, GetLayerByNumber)
{
    Layer layer1;
    layer1.type = LayerType::Image;
    layer1.img = testImage;
    layer1.img->info.layer = 5;

    Layer layer2;
    layer2.type = LayerType::Text;
    layer2.textContent = "test";

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    Layer* found = layerManager->getLayerByNumber(5);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, LayerType::Image);

    Layer* notFound = layerManager->getLayerByNumber(999);
    EXPECT_EQ(notFound, nullptr);
}

TEST_F(LayerManagerTest, GetLayerByName)
{
    Layer layer1;
    layer1.type = LayerType::Image;
    layer1.img = testImage;

    Layer layer2;
    layer2.type = LayerType::Text;
    layer2.textContent = "Hello World";

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    Layer* found = layerManager->getLayerByName("test_image.jpg");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, LayerType::Image);

    Layer* found2 = layerManager->getLayerByName("Hello World");
    ASSERT_NE(found2, nullptr);
    EXPECT_EQ(found2->type, LayerType::Text);

    Layer* notFound = layerManager->getLayerByName("nonexistent");
    EXPECT_EQ(notFound, nullptr);
}

TEST_F(LayerManagerTest, GetBaseLayer)
{
    Layer normalLayer;
    normalLayer.name = "normal";
    normalLayer.isBaseLayer = false;

    Layer baseLayer;
    baseLayer.name = "base";
    baseLayer.isBaseLayer = true;

    Layer anotherLayer;
    anotherLayer.name = "another";
    anotherLayer.isBaseLayer = false;

    layerManager->addLayer(normalLayer);
    layerManager->addLayer(baseLayer);
    layerManager->addLayer(anotherLayer);

    Layer* base = layerManager->getBaseLayer();
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->name, "base");
    EXPECT_TRUE(base->isBaseLayer);
}

TEST_F(LayerManagerTest, GetBaseLayerWhenNoneExists)
{
    Layer layer1;
    layer1.name = "layer1";
    layer1.isBaseLayer = false;

    Layer layer2;
    layer2.name = "layer2";
    layer2.isBaseLayer = false;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    Layer* base = layerManager->getBaseLayer();
    EXPECT_EQ(base, nullptr);
}

TEST_F(LayerManagerTest, SortLayers)
{
    Layer layer1;
    layer1.type = LayerType::Image;
    layer1.img = std::make_shared<Image>(100);
    layer1.img->info.layer = 3;

    Layer layer2;
    layer2.type = LayerType::Image;
    layer2.img = std::make_shared<Image>(100);
    layer2.img->info.layer = 1;

    Layer layer3;
    layer3.type = LayerType::Image;
    layer3.img = std::make_shared<Image>(100);
    layer3.img->info.layer = 2;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);
    layerManager->addLayer(layer3);

    // Before sorting: 3, 1, 2
    EXPECT_EQ(layerManager->getLayers()[0].getLayerNumber(), 3);
    EXPECT_EQ(layerManager->getLayers()[1].getLayerNumber(), 1);
    EXPECT_EQ(layerManager->getLayers()[2].getLayerNumber(), 2);

    layerManager->sortLayers();

    // After sorting: 1, 2, 3
    EXPECT_EQ(layerManager->getLayers()[0].getLayerNumber(), 1);
    EXPECT_EQ(layerManager->getLayers()[1].getLayerNumber(), 2);
    EXPECT_EQ(layerManager->getLayers()[2].getLayerNumber(), 3);
}

TEST_F(LayerManagerTest, MarkDirty)
{
    Layer layer1;
    layer1.dirty = false;
    Layer layer2;
    layer2.dirty = false;
    Layer layer3;
    layer3.dirty = false;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);
    layerManager->addLayer(layer3);

    // Manually set all layers to clean
    for (auto& layer : layerManager->getLayers())
    {
        layer.dirty = false;
    }

    EXPECT_FALSE(layerManager->isDirty());

    layerManager->markDirty();

    EXPECT_TRUE(layerManager->isDirty());

    // All layers should be dirty
    for (const auto& layer : layerManager->getLayers())
    {
        EXPECT_TRUE(layer.dirty);
    }
}

TEST_F(LayerManagerTest, IsDirty)
{
    Layer cleanLayer;
    cleanLayer.dirty = false;

    Layer dirtyLayer;
    dirtyLayer.dirty = true;

    layerManager->addLayer(cleanLayer);
    EXPECT_FALSE(layerManager->isDirty());

    layerManager->addLayer(dirtyLayer);
    EXPECT_TRUE(layerManager->isDirty());
}

TEST_F(LayerManagerTest, SetLayerDirty)
{
    Layer layer1;
    layer1.type = LayerType::Image;
    layer1.img = std::make_shared<Image>(100);
    layer1.img->info.layer = 1;
    layer1.dirty = false;

    Layer layer2;
    layer2.type = LayerType::Image;
    layer2.img = std::make_shared<Image>(100);
    layer2.img->info.layer = 2;
    layer2.dirty = false;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    EXPECT_FALSE(layerManager->isDirty());

    layerManager->setLayerDirty(1, true);

    EXPECT_TRUE(layerManager->isDirty());
    EXPECT_TRUE(layerManager->getLayers()[0].dirty);
    EXPECT_FALSE(layerManager->getLayers()[1].dirty);
}

TEST_F(LayerManagerTest, InvalidateTextures)
{
    Layer layer1;
    layer1.dirty = false;
    Layer layer2;
    layer2.dirty = false;

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    // Set all layers to clean
    for (auto& layer : layerManager->getLayers())
    {
        layer.dirty = false;
    }

    EXPECT_FALSE(layerManager->isDirty());

    layerManager->invalidateTextures();

    EXPECT_TRUE(layerManager->isDirty());
    for (const auto& layer : layerManager->getLayers())
    {
        EXPECT_TRUE(layer.dirty);
    }
}

// Test Layer struct functionality
TEST_F(LayerManagerTest, LayerGetLayerNumber)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;

    EXPECT_EQ(imageLayer.getLayerNumber(), 1); // testImage layer is set to 1

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.gif = testGif;

    // Since testGif failed to open (dummy path), it has no frames, so returns default 0
    EXPECT_EQ(gifLayer.getLayerNumber(), 0); // Default when no frames

    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.textContent = "test";

    EXPECT_EQ(textLayer.getLayerNumber(), 0); // Default for text
}

TEST_F(LayerManagerTest, LayerSetLayerNumber)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;

    imageLayer.setLayerNumber(5);
    EXPECT_EQ(imageLayer.getLayerNumber(), 5);
    EXPECT_EQ(imageLayer.img->info.layer, 5);
}

TEST_F(LayerManagerTest, LayerGetLayerName)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;

    EXPECT_EQ(imageLayer.getLayerName(), "test_image.jpg");

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.gif = testGif;

    // Since testGif failed to open (dummy path), getFilename() returns the original path
    EXPECT_EQ(gifLayer.getLayerName(), "dummy_path.gif");

    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.textContent = "Hello World";

    EXPECT_EQ(textLayer.getLayerName(), "Hello World");

    Layer emptyTextLayer;
    emptyTextLayer.type = LayerType::Text;
    emptyTextLayer.textContent = "";

    EXPECT_EQ(emptyTextLayer.getLayerName(), "Text Layer");
}

TEST_F(LayerManagerTest, LayerGetTextureId)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;
    imageLayer.img->info.textureId = 123;

    EXPECT_EQ(imageLayer.getTextureId(), 123);

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.gif = testGif;
    if (!gifLayer.gif->frames().empty())
    {
        gifLayer.gif->frames()[0]->info.textureId = 456;
        EXPECT_EQ(gifLayer.getTextureId(), 456);
    }

    Layer textLayer;
    textLayer.type = LayerType::Text;

    EXPECT_EQ(textLayer.getTextureId(), 0);
}

TEST_F(LayerManagerTest, LayerGetSize)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;

    EXPECT_EQ(imageLayer.getSize(), testImageSize);

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.gif = testGif;

    // Since testGif failed to open (dummy path), it has no size
    EXPECT_EQ(gifLayer.getSize(), 0);

    Layer textLayer;
    textLayer.type = LayerType::Text;

    EXPECT_EQ(textLayer.getSize(), 0);
}

TEST_F(LayerManagerTest, LayerGetDimensions)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;

    EXPECT_EQ(imageLayer.getWidth(), 640);
    EXPECT_EQ(imageLayer.getHeight(), 480);

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.gif = testGif;

    // Since testGif failed to open (dummy path), it has no frames, so dimensions are 0
    EXPECT_EQ(gifLayer.getWidth(), 0);
    EXPECT_EQ(gifLayer.getHeight(), 0);

    Layer textLayer;
    textLayer.type = LayerType::Text;

    EXPECT_EQ(textLayer.getWidth(), 0);
    EXPECT_EQ(textLayer.getHeight(), 0);
}

TEST_F(LayerManagerTest, LayerGetFormat)
{
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.img = testImage;

    EXPECT_EQ(imageLayer.getFormat(), ImageFormat::RGB);

    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.gif = testGif;

    // Since testGif failed to open (dummy path), it has no frames, so format is UNKNOWN
    EXPECT_EQ(gifLayer.getFormat(), ImageFormat::UNKNOWN);

    Layer textLayer;
    textLayer.type = LayerType::Text;

    EXPECT_EQ(textLayer.getFormat(), ImageFormat::UNKNOWN);
}

// Test layer unique ID assignment
TEST_F(LayerManagerTest, LayerUniqueIds)
{
    Layer layer1;
    layer1.id = Layer::next_id++;

    Layer layer2;
    layer2.id = Layer::next_id++;

    Layer layer3;
    layer3.id = Layer::next_id++;

    EXPECT_NE(layer1.id, layer2.id);
    EXPECT_NE(layer2.id, layer3.id);
    EXPECT_NE(layer1.id, layer3.id);

    EXPECT_LT(layer1.id, layer2.id);
    EXPECT_LT(layer2.id, layer3.id);
}

// Test const correctness
TEST_F(LayerManagerTest, ConstCorrectness)
{
    Layer layer;
    layer.name = "test";
    layerManager->addLayer(layer);

    const LayerManager& constRef = *layerManager;
    const auto& constLayers = constRef.getLayers();

    EXPECT_EQ(constLayers.size(), 1);
    EXPECT_EQ(constLayers[0].name, "test");
}

// Test empty layer manager behavior
TEST_F(LayerManagerTest, EmptyLayerManager)
{
    EXPECT_TRUE(layerManager->getLayers().empty());
    EXPECT_FALSE(layerManager->isDirty());
    EXPECT_EQ(layerManager->getLayerByNumber(1), nullptr);
    EXPECT_EQ(layerManager->getLayerByName("test"), nullptr);
    EXPECT_EQ(layerManager->getBaseLayer(), nullptr);

    // These operations should not crash on empty manager
    layerManager->sortLayers();
    layerManager->markDirty();
    layerManager->invalidateTextures();
    layerManager->setLayerDirty(1, true);
}
