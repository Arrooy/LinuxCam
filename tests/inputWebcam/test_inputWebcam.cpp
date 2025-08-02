#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <cstring>

#include "LinuxFace/inputWebcam.h"

using namespace linuxface;

// Mock InputWebcam implementation for testing
class MockInputWebcam : public InputWebcam
{
  public:
    MockInputWebcam(const std::string& name, const std::string& devicePath, unsigned int width, unsigned int height, unsigned int bufferCount = 4)
        : InputWebcam(name, devicePath, width, height, bufferCount), setupCalled_(false), startCalled_(false), stopCalled_(false),
          imageRetrieved_(false), isConnected_(true)
    {
        // Create mock image with calculated size for RGB format (3 bytes per pixel)
        size_t imageSize = width * height * 3;  // RGB = 3 bytes per pixel
        mockImage_ = std::make_unique<Image>(imageSize);
        
        // Set image metadata
        mockImage_->info.width = width;
        mockImage_->info.height = height;
        mockImage_->info.format = ImageFormat::RGB;
        mockImage_->info.pixelSizeBytes = 3;
        mockImage_->info.is_valid = true;
        
        // Fill with gray data
        std::fill(mockImage_->data(), mockImage_->data() + mockImage_->size(), 128);
    }

    bool setupDevice() override
    {
        setupCalled_ = true;
        return isConnected_;
    }

    bool start() override
    {
        startCalled_ = true;
        return isConnected_;
    }

    bool stop() override
    {
        stopCalled_ = true;
        return true;
    }

    bool getImage(std::unique_ptr<Image>& outImage)
    {
        if (!isConnected_) return false;
        imageRetrieved_ = true;
        
        // Create a new image and copy the mock image data
        outImage = std::make_unique<Image>(mockImage_->size());
        outImage->info = mockImage_->info; // Copy metadata
        std::memcpy(outImage->data(), mockImage_->data(), mockImage_->size());
        return true;
    }

    // Test helpers
    bool wasSetupCalled() const { return setupCalled_; }
    bool wasStartCalled() const { return startCalled_; }
    bool wasStopCalled() const { return stopCalled_; }
    bool wasImageRetrieved() const { return imageRetrieved_; }
    void setConnected(bool connected) { isConnected_ = connected; }
    void simulateImageCapture() { imageRetrieved_ = true; }
    void simulateImageLoss() { imageRetrieved_ = false; }

  private:
    bool setupCalled_;
    bool startCalled_;
    bool stopCalled_;
    bool imageRetrieved_;
    bool isConnected_;
    std::unique_ptr<Image> mockImage_;
};

class InputWebcamTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        inputWebcam_ = std::make_unique<MockInputWebcam>("Test Input", "/dev/video0", 640, 480);
        hdInputWebcam_ = std::make_unique<MockInputWebcam>("HD Input", "/dev/video1", 1920, 1080);
    }

    void TearDown() override {}

    std::unique_ptr<MockInputWebcam> inputWebcam_;
    std::unique_ptr<MockInputWebcam> hdInputWebcam_;
};

TEST_F(InputWebcamTest, InputWebcamConstruction)
{
    EXPECT_EQ(inputWebcam_->getDevicePath(), "/dev/video0");
    EXPECT_EQ(inputWebcam_->getName(), "Test Input");
    
    EXPECT_EQ(hdInputWebcam_->getDevicePath(), "/dev/video1");
    EXPECT_EQ(hdInputWebcam_->getName(), "HD Input");
}

TEST_F(InputWebcamTest, InputWebcamLifecycle)
{
    // Initial state
    EXPECT_FALSE(inputWebcam_->wasSetupCalled());
    EXPECT_FALSE(inputWebcam_->wasStartCalled());
    EXPECT_FALSE(inputWebcam_->wasStopCalled());
    EXPECT_FALSE(inputWebcam_->wasImageRetrieved());
    
    // Setup device
    EXPECT_TRUE(inputWebcam_->setupDevice());
    EXPECT_TRUE(inputWebcam_->wasSetupCalled());
    
    // Start capture
    EXPECT_TRUE(inputWebcam_->start());
    EXPECT_TRUE(inputWebcam_->wasStartCalled());
    
    // Get image
    std::unique_ptr<Image> image;
    EXPECT_TRUE(inputWebcam_->getImage(image));
    EXPECT_TRUE(inputWebcam_->wasImageRetrieved());
    EXPECT_NE(image, nullptr);
    
    // Stop capture
    EXPECT_TRUE(inputWebcam_->stop());
    EXPECT_TRUE(inputWebcam_->wasStopCalled());
}

TEST_F(InputWebcamTest, ImageRetrieval)
{
    // Setup and start
    EXPECT_TRUE(inputWebcam_->setupDevice());
    EXPECT_TRUE(inputWebcam_->start());
    
    // Get image
    std::unique_ptr<Image> image;
    EXPECT_TRUE(inputWebcam_->getImage(image));
    EXPECT_NE(image, nullptr);
    
    // Verify image properties
    EXPECT_EQ(image->info.width, 640);
    EXPECT_EQ(image->info.height, 480);
    EXPECT_EQ(image->info.format, ImageFormat::RGB);
    EXPECT_GT(image->size(), 0);
    
    // Verify image data is accessible
    EXPECT_NE(image->data(), nullptr);
    EXPECT_EQ(image->data()[0], 128); // Mock gray value
}

TEST_F(InputWebcamTest, HDImageRetrieval)
{
    // Test HD resolution image retrieval
    EXPECT_TRUE(hdInputWebcam_->setupDevice());
    EXPECT_TRUE(hdInputWebcam_->start());
    
    std::unique_ptr<Image> image;
    EXPECT_TRUE(hdInputWebcam_->getImage(image));
    EXPECT_NE(image, nullptr);
    
    // Verify HD image properties
    EXPECT_EQ(image->info.width, 1920);
    EXPECT_EQ(image->info.height, 1080);
    EXPECT_EQ(image->info.format, ImageFormat::RGB);
}

TEST_F(InputWebcamTest, DeviceDisconnection)
{
    // Disconnect device
    inputWebcam_->setConnected(false);
    
    // Setup should fail when disconnected
    EXPECT_FALSE(inputWebcam_->setupDevice());
    
    // Start should fail when disconnected
    EXPECT_FALSE(inputWebcam_->start());
    
    // Image retrieval should fail when disconnected
    std::unique_ptr<Image> image;
    EXPECT_FALSE(inputWebcam_->getImage(image));
    EXPECT_EQ(image, nullptr);
    
    // Reconnect
    inputWebcam_->setConnected(true);
    EXPECT_TRUE(inputWebcam_->setupDevice());
    EXPECT_TRUE(inputWebcam_->start());
}

TEST_F(InputWebcamTest, ImageConsistency)
{
    EXPECT_TRUE(inputWebcam_->setupDevice());
    EXPECT_TRUE(inputWebcam_->start());
    
    // Retrieve multiple images and verify consistency
    for (int i = 0; i < 5; ++i)
    {
        std::unique_ptr<Image> image;
        EXPECT_TRUE(inputWebcam_->getImage(image));
        EXPECT_NE(image, nullptr);
        EXPECT_EQ(image->info.width, 640);
        EXPECT_EQ(image->info.height, 480);
        EXPECT_EQ(image->info.format, ImageFormat::RGB);
        
        // Verify image data integrity
        EXPECT_EQ(image->data()[0], 128);
        EXPECT_EQ(image->data()[100], 128);
        EXPECT_EQ(image->data()[1000], 128);
    }
}

TEST_F(InputWebcamTest, ImageLoss)
{
    EXPECT_TRUE(inputWebcam_->setupDevice());
    EXPECT_TRUE(inputWebcam_->start());
    
    // Normal image retrieval
    std::unique_ptr<Image> image;
    EXPECT_TRUE(inputWebcam_->getImage(image));
    EXPECT_NE(image, nullptr);
    
    // Simulate image loss
    inputWebcam_->simulateImageLoss();
    
    // Recovery
    inputWebcam_->simulateImageCapture();
    EXPECT_TRUE(inputWebcam_->getImage(image));
    EXPECT_NE(image, nullptr);
}

// Edge case tests for InputWebcam
class InputWebcamEdgeCaseTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(InputWebcamEdgeCaseTest, ZeroDimensions)
{
    MockInputWebcam zeroWebcam("Zero Webcam", "/dev/video99", 0, 0);
    
    // Should handle gracefully
    EXPECT_TRUE(zeroWebcam.setupDevice());
    EXPECT_TRUE(zeroWebcam.start());
    
    std::unique_ptr<Image> image;
    EXPECT_TRUE(zeroWebcam.getImage(image));
    
    // Image should exist but have zero dimensions
    EXPECT_NE(image, nullptr);
    EXPECT_EQ(image->info.width, 0);
    EXPECT_EQ(image->info.height, 0);
}

TEST_F(InputWebcamEdgeCaseTest, LargeDimensions)
{
    // Test with 4K dimensions
    MockInputWebcam largeWebcam("4K Webcam", "/dev/video99", 3840, 2160);
    
    EXPECT_TRUE(largeWebcam.setupDevice());
    EXPECT_TRUE(largeWebcam.start());
    
    std::unique_ptr<Image> image;
    EXPECT_TRUE(largeWebcam.getImage(image));
    
    // Verify large image properties
    EXPECT_NE(image, nullptr);
    EXPECT_EQ(image->info.width, 3840);
    EXPECT_EQ(image->info.height, 2160);
}

TEST_F(InputWebcamEdgeCaseTest, InvalidDevicePath)
{
    MockInputWebcam invalidWebcam("Invalid", "", 640, 480);
    
    EXPECT_TRUE(invalidWebcam.getDevicePath().empty());
    
    // Should still work in mock implementation
    EXPECT_TRUE(invalidWebcam.setupDevice());
}

TEST_F(InputWebcamEdgeCaseTest, RepeatOperations)
{
    MockInputWebcam webcam("Test", "/dev/video0", 320, 240);
    
    // Multiple setup calls
    EXPECT_TRUE(webcam.setupDevice());
    EXPECT_TRUE(webcam.setupDevice());
    EXPECT_TRUE(webcam.setupDevice());
    
    // Multiple start calls
    EXPECT_TRUE(webcam.start());
    EXPECT_TRUE(webcam.start());
    
    // Multiple image retrievals
    for (int i = 0; i < 10; ++i)
    {
        std::unique_ptr<Image> image;
        EXPECT_TRUE(webcam.getImage(image));
        EXPECT_NE(image, nullptr);
    }
    
    // Multiple stop calls
    EXPECT_TRUE(webcam.stop());
    EXPECT_TRUE(webcam.stop());
}

TEST_F(InputWebcamEdgeCaseTest, OperationOrder)
{
    MockInputWebcam webcam("Test", "/dev/video0", 640, 480);
    
    // Try to start without setup (should still work in mock)
    EXPECT_TRUE(webcam.start());
    
    // Try to get image without start (should still work in mock)
    std::unique_ptr<Image> image;
    EXPECT_TRUE(webcam.getImage(image));
    
    // Try to stop without start
    EXPECT_TRUE(webcam.stop());
    
    // Proper order
    EXPECT_TRUE(webcam.setupDevice());
    EXPECT_TRUE(webcam.start());
    EXPECT_TRUE(webcam.getImage(image));
    EXPECT_TRUE(webcam.stop());
}

// Performance test for image retrieval
TEST_F(InputWebcamEdgeCaseTest, ImageRetrievalPerformance)
{
    MockInputWebcam webcam("Perf Test", "/dev/video0", 640, 480);
    
    EXPECT_TRUE(webcam.setupDevice());
    EXPECT_TRUE(webcam.start());
    
    // Retrieve many images quickly (stress test)
    const int numImages = 100;
    for (int i = 0; i < numImages; ++i)
    {
        std::unique_ptr<Image> image;
        EXPECT_TRUE(webcam.getImage(image));
        EXPECT_NE(image, nullptr);
        EXPECT_EQ(image->info.width, 640);
        EXPECT_EQ(image->info.height, 480);
    }
    
    EXPECT_TRUE(webcam.stop());
}
