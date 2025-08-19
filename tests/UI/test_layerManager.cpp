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
    // Text layers now use createTextImage to generate their image representation
    textLayer.img = Layer::createTextImage("Hello World");

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

TEST_F(LayerManagerTest, GetLayerByCameraDevicePath)
{
    Layer normalLayer;
    normalLayer.name = "normal";
    normalLayer.cameraDevicePath = "";

    Layer cameraLayer;
    cameraLayer.name = "camera";
    cameraLayer.cameraDevicePath = "/dev/video0";

    Layer anotherCameraLayer;
    anotherCameraLayer.name = "another_camera";
    anotherCameraLayer.cameraDevicePath = "/dev/video1";

    layerManager->addLayer(normalLayer);
    layerManager->addLayer(cameraLayer);
    layerManager->addLayer(anotherCameraLayer);

    Layer* camera = layerManager->getLayerByCameraDevicePath("/dev/video0");
    ASSERT_NE(camera, nullptr);
    EXPECT_EQ(camera->name, "camera");
    EXPECT_EQ(camera->cameraDevicePath, "/dev/video0");
}

TEST_F(LayerManagerTest, GetLayerByCameraDevicePathWhenNoneExists)
{
    Layer layer1;
    layer1.name = "layer1";
    layer1.cameraDevicePath = "/dev/video0";

    Layer layer2;
    layer2.name = "layer2";
    layer2.cameraDevicePath = "/dev/video1";

    layerManager->addLayer(layer1);
    layerManager->addLayer(layer2);

    Layer* camera = layerManager->getLayerByCameraDevicePath("/dev/video2");
    EXPECT_EQ(camera, nullptr);
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
    textLayer.textContent = "Test";
    textLayer.img = Layer::createTextImage("Test");
    
    // Text layer can have texture ID from its generated image
    EXPECT_EQ(textLayer.getTextureId(), 0); // Default before texture is created
    
    // Set texture ID and verify
    textLayer.img->info.textureId = 789;
    EXPECT_EQ(textLayer.getTextureId(), 789);
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
    textLayer.textContent = "Test";
    textLayer.img = Layer::createTextImage("Test");

    // Text layer now has size from its generated image
    EXPECT_GT(textLayer.getSize(), 0);
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
    textLayer.textContent = "Test";
    textLayer.img = Layer::createTextImage("Test");

    // Text layer now has dimensions from its generated image
    EXPECT_GT(textLayer.getWidth(), 0);
    EXPECT_GT(textLayer.getHeight(), 0);
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
    textLayer.textContent = "Test";
    textLayer.img = Layer::createTextImage("Test");

    // Text layer now has RGBA format from its generated image
    EXPECT_EQ(textLayer.getFormat(), ImageFormat::RGBA);
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
    EXPECT_EQ(layerManager->getLayerByCameraDevicePath("/dev/video0"), nullptr);

    // These operations should not crash on empty manager
    layerManager->sortLayers();
    layerManager->markDirty();
    layerManager->invalidateTextures();
    layerManager->setLayerDirty(1, true);
}

// Test text layer creation and functionality
TEST_F(LayerManagerTest, CreateTextImage)
{
    // Test basic text image creation
    auto textImage = Layer::createTextImage("Hello World");
    ASSERT_NE(textImage, nullptr);
    EXPECT_GT(textImage->info.width, 0);
    EXPECT_GT(textImage->info.height, 0);
    EXPECT_EQ(textImage->info.format, ImageFormat::RGBA);
    EXPECT_EQ(textImage->info.filename, "text_layer_Hello World");
}

TEST_F(LayerManagerTest, CreateTextImageWithScaling)
{
    // Test text with different scales
    auto textImage1 = Layer::createTextImage("Test", IM_COL32_WHITE, 1);
    auto textImage2 = Layer::createTextImage("Test", IM_COL32_WHITE, 2);
    auto textImage3 = Layer::createTextImage("Test", IM_COL32_WHITE, 3);
    
    ASSERT_NE(textImage1, nullptr);
    ASSERT_NE(textImage2, nullptr);
    ASSERT_NE(textImage3, nullptr);
    
    // Larger scale should produce larger images
    EXPECT_LT(textImage1->info.width, textImage2->info.width);
    EXPECT_LT(textImage2->info.width, textImage3->info.width);
    EXPECT_LT(textImage1->info.height, textImage2->info.height);
    EXPECT_LT(textImage2->info.height, textImage3->info.height);
}

TEST_F(LayerManagerTest, CreateTextImageWithBackground)
{
    // Test text with background
    auto textWithBg = Layer::createTextImage("Test", IM_COL32_WHITE, 1, true, IM_COL32_BLACK);
    auto textWithoutBg = Layer::createTextImage("Test", IM_COL32_WHITE, 1, false);
    
    ASSERT_NE(textWithBg, nullptr);
    ASSERT_NE(textWithoutBg, nullptr);
    
    // Background version should be larger due to padding
    EXPECT_GE(textWithBg->info.width, textWithoutBg->info.width);
    EXPECT_GE(textWithBg->info.height, textWithoutBg->info.height);
}

TEST_F(LayerManagerTest, CreateTextImageWithAlignment)
{
    // Test text with bounding rectangle and alignment
    int boundingWidth = 200;
    int boundingHeight = 100;
    
    auto centeredText = Layer::createTextImage("Test", IM_COL32_WHITE, 1, false, IM_COL32_BLACK, 
                                              false, false, 1, 1, 2, boundingWidth, boundingHeight);
    
    ASSERT_NE(centeredText, nullptr);
    EXPECT_EQ(centeredText->info.width, boundingWidth);
    EXPECT_EQ(centeredText->info.height, boundingHeight);
}

TEST_F(LayerManagerTest, CreateTextImageMultiline)
{
    // Test multiline text
    auto multilineText = Layer::createTextImage("Line1\nLine2\nLine3", IM_COL32_WHITE, 1, 
                                               false, IM_COL32_BLACK, false, true);
    auto singleLineText = Layer::createTextImage("Line1Line2Line3", IM_COL32_WHITE, 1);
    
    ASSERT_NE(multilineText, nullptr);
    ASSERT_NE(singleLineText, nullptr);
    
    // Multiline should be taller
    EXPECT_GT(multilineText->info.height, singleLineText->info.height);
}

TEST_F(LayerManagerTest, CreateTextImageEmptyString)
{
    // Test empty string handling
    auto emptyText = Layer::createTextImage("");
    EXPECT_EQ(emptyText, nullptr);
}

TEST_F(LayerManagerTest, TextLayerLifecycle)
{
    // Test complete text layer lifecycle
    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.name = "test_text";
    textLayer.textContent = "Hello LinuxCam";
    textLayer.x = 50.0f;
    textLayer.y = 100.0f;
    
    // Create the text image
    textLayer.img = Layer::createTextImage(textLayer.textContent, IM_COL32(255, 255, 255, 255), 2);
    
    ASSERT_NE(textLayer.img, nullptr);
    
    // Add to layer manager
    layerManager->addLayer(textLayer);
    
    EXPECT_EQ(layerManager->getLayers().size(), 1);
    EXPECT_EQ(layerManager->getLayers()[0].type, LayerType::Text);
    EXPECT_EQ(layerManager->getLayers()[0].textContent, "Hello LinuxCam");
    EXPECT_EQ(layerManager->getLayers()[0].x, 50.0f);
    EXPECT_EQ(layerManager->getLayers()[0].y, 100.0f);
    
    // Test layer dimensions from text image
    EXPECT_GT(layerManager->getLayers()[0].getWidth(), 0);
    EXPECT_GT(layerManager->getLayers()[0].getHeight(), 0);
    EXPECT_EQ(layerManager->getLayers()[0].getFormat(), ImageFormat::RGBA);
}

TEST_F(LayerManagerTest, TextLayerGetters)
{
    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.textContent = "Test Text";
    textLayer.img = Layer::createTextImage("Test Text", IM_COL32_WHITE, 1);
    
    // Test width and height getters work with text layers
    EXPECT_GT(textLayer.getWidth(), 0);
    EXPECT_GT(textLayer.getHeight(), 0);
    EXPECT_EQ(textLayer.getFormat(), ImageFormat::RGBA);
    EXPECT_EQ(textLayer.getLayerName(), "Test Text");
    EXPECT_GT(textLayer.getSize(), 0);
}

TEST_F(LayerManagerTest, TextLayerWithCustomColors)
{
    // Test text with custom colors
    ImU32 redColor = IM_COL32(255, 0, 0, 255);
    ImU32 blueBackground = IM_COL32(0, 0, 255, 128);
    
    auto coloredText = Layer::createTextImage("Colored Text", redColor, 1, true, blueBackground);
    
    ASSERT_NE(coloredText, nullptr);
    EXPECT_GT(coloredText->info.width, 0);
    EXPECT_GT(coloredText->info.height, 0);
    EXPECT_EQ(coloredText->info.format, ImageFormat::RGBA);
}

TEST_F(LayerManagerTest, TextLayerSorting)
{
    // Test text layers in sorting
    Layer textLayer1;
    textLayer1.type = LayerType::Text;
    textLayer1.textContent = "Layer 1";
    textLayer1.layerNumber = 3;
    textLayer1.img = Layer::createTextImage("Layer 1");
    
    Layer textLayer2;
    textLayer2.type = LayerType::Text;
    textLayer2.textContent = "Layer 2";
    textLayer2.layerNumber = 1;
    textLayer2.img = Layer::createTextImage("Layer 2");
    
    Layer textLayer3;
    textLayer3.type = LayerType::Text;
    textLayer3.textContent = "Layer 3";
    textLayer3.layerNumber = 2;
    textLayer3.img = Layer::createTextImage("Layer 3");
    
    layerManager->addLayer(textLayer1);
    layerManager->addLayer(textLayer2);
    layerManager->addLayer(textLayer3);
    
    // Before sorting: 3, 1, 2
    EXPECT_EQ(layerManager->getLayers()[0].getLayerNumber(), 3);
    EXPECT_EQ(layerManager->getLayers()[1].getLayerNumber(), 1);
    EXPECT_EQ(layerManager->getLayers()[2].getLayerNumber(), 2);
    
    layerManager->sortLayers();
    
    // After sorting: 1, 2, 3
    EXPECT_EQ(layerManager->getLayers()[0].getLayerNumber(), 1);
    EXPECT_EQ(layerManager->getLayers()[1].getLayerNumber(), 2);
    EXPECT_EQ(layerManager->getLayers()[2].getLayerNumber(), 3);
    
    EXPECT_EQ(layerManager->getLayers()[0].textContent, "Layer 2");
    EXPECT_EQ(layerManager->getLayers()[1].textContent, "Layer 3");
    EXPECT_EQ(layerManager->getLayers()[2].textContent, "Layer 1");
}

TEST_F(LayerManagerTest, MixedLayerTypes)
{
    // Test managing mixed layer types (Image, Gif, Text)
    Layer imageLayer;
    imageLayer.type = LayerType::Image;
    imageLayer.name = "image";
    imageLayer.img = testImage;
    
    Layer gifLayer;
    gifLayer.type = LayerType::Gif;
    gifLayer.name = "gif";
    gifLayer.gif = testGif;
    
    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.name = "text";
    textLayer.textContent = "Mixed Layer Test";
    textLayer.img = Layer::createTextImage("Mixed Layer Test");
    
    layerManager->addLayer(imageLayer);
    layerManager->addLayer(gifLayer);
    layerManager->addLayer(textLayer);
    
    EXPECT_EQ(layerManager->getLayers().size(), 3);
    
    // Find layers by name
    Layer* foundImage = layerManager->getLayerByName("test_image.jpg");
    Layer* foundText = layerManager->getLayerByName("Mixed Layer Test");
    
    ASSERT_NE(foundImage, nullptr);
    ASSERT_NE(foundText, nullptr);
    
    EXPECT_EQ(foundImage->type, LayerType::Image);
    EXPECT_EQ(foundText->type, LayerType::Text);
}

TEST_F(LayerManagerTest, TextLayerDirtyState)
{
    Layer textLayer;
    textLayer.type = LayerType::Text;
    textLayer.textContent = "Dirty Test";
    textLayer.img = Layer::createTextImage("Dirty Test");
    textLayer.dirty = false;
    
    layerManager->addLayer(textLayer);
    
    EXPECT_FALSE(layerManager->isDirty());
    
    // Mark text layer as dirty
    layerManager->getLayers()[0].dirty = true;
    
    EXPECT_TRUE(layerManager->isDirty());
    
    // Test invalidateTextures with text layers
    layerManager->invalidateTextures();
    EXPECT_TRUE(layerManager->getLayers()[0].dirty);
}
