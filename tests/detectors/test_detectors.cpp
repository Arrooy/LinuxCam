#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "LinuxFace/detectors.h"
#include "LinuxFace/face.h"
#include "LinuxFace/Image/image.h"

using namespace linuxface;

// Simple mock implementations for testing the detector interfaces
class MockFaceDetector : public FaceDetector
{
  public:
    MockFaceDetector(bool shouldSucceed = true) : shouldSucceed_(shouldSucceed), detectCallCount_(0) {}

    std::vector<Face> detect(const std::unique_ptr<Image>& image) override
    {
        detectCallCount_++;
        lastImageProcessed_ = image.get();

        if (!shouldSucceed_ || !image)
        {
            return {};
        }

        // Return a simple mock face
        std::vector<Face> faces;
        Face mockFace;
        faces.push_back(std::move(mockFace));
        return faces;
    }

    // Test helpers
    int getDetectCallCount() const { return detectCallCount_; }
    const Image* getLastImageProcessed() const { return lastImageProcessed_; }
    void setShouldSucceed(bool succeed) { shouldSucceed_ = succeed; }

  private:
    bool shouldSucceed_;
    int detectCallCount_;
    const Image* lastImageProcessed_;
};

class MockShapeDetector : public ShapeDetector
{
  public:
    MockShapeDetector(bool shouldSucceed = true) : shouldSucceed_(shouldSucceed), detectCallCount_(0) {}

    std::vector<Face> detect(const std::unique_ptr<Image>& image,
                            std::vector<math_utils::Rect<float>>& faces_rect) override
    {
        detectCallCount_++;
        lastImageProcessed_ = image.get();
        lastFaceRects_ = faces_rect;

        if (!shouldSucceed_ || !image)
        {
            return {};
        }

        // Return mock faces based on input rects count
        std::vector<Face> faces;
        for (size_t i = 0; i < faces_rect.size(); ++i)
        {
            Face face;
            faces.push_back(std::move(face));
        }
        return faces;
    }

    // Test helpers
    int getDetectCallCount() const { return detectCallCount_; }
    const Image* getLastImageProcessed() const { return lastImageProcessed_; }
    const std::vector<math_utils::Rect<float>>& getLastFaceRects() const { return lastFaceRects_; }
    void setShouldSucceed(bool succeed) { shouldSucceed_ = succeed; }

  private:
    bool shouldSucceed_;
    int detectCallCount_;
    const Image* lastImageProcessed_;
    std::vector<math_utils::Rect<float>> lastFaceRects_;
};

class DetectorsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create a simple test image using the proper constructor
        testImage_ = std::make_unique<Image>(100 * 100 * 3); // Size in bytes
        testImage_->info.width = 100;
        testImage_->info.height = 100;
        testImage_->info.format = ImageFormat::RGB;
        testImage_->info.pixelSizeBytes = 3;
        
        faceDetector_ = std::make_unique<MockFaceDetector>();
        shapeDetector_ = std::make_unique<MockShapeDetector>();
    }

    void TearDown() override {}

    std::unique_ptr<Image> testImage_;
    std::unique_ptr<MockFaceDetector> faceDetector_;
    std::unique_ptr<MockShapeDetector> shapeDetector_;
};

TEST_F(DetectorsTest, FaceDetectorInterface)
{
    // Test that the face detector interface works correctly
    EXPECT_NE(faceDetector_, nullptr);
    
    auto faces = faceDetector_->detect(testImage_);
    
    EXPECT_EQ(faceDetector_->getDetectCallCount(), 1);
    EXPECT_EQ(faceDetector_->getLastImageProcessed(), testImage_.get());
    EXPECT_EQ(faces.size(), 1);
}

TEST_F(DetectorsTest, FaceDetectorWithNullImage)
{
    std::unique_ptr<Image> nullImage = nullptr;
    
    auto faces = faceDetector_->detect(nullImage);
    
    EXPECT_EQ(faceDetector_->getDetectCallCount(), 1);
    EXPECT_EQ(faceDetector_->getLastImageProcessed(), nullptr);
    EXPECT_TRUE(faces.empty());
}

TEST_F(DetectorsTest, FaceDetectorFailure)
{
    faceDetector_->setShouldSucceed(false);
    
    auto faces = faceDetector_->detect(testImage_);
    
    EXPECT_EQ(faceDetector_->getDetectCallCount(), 1);
    EXPECT_TRUE(faces.empty());
}

TEST_F(DetectorsTest, ShapeDetectorInterface)
{
    // Test that the shape detector interface works correctly
    EXPECT_NE(shapeDetector_, nullptr);
    
    std::vector<math_utils::Rect<float>> inputRects = {
        math_utils::Rect<float>(10.0f, 20.0f, 50.0f, 60.0f),
        math_utils::Rect<float>(100.0f, 150.0f, 80.0f, 90.0f)
    };
    
    auto faces = shapeDetector_->detect(testImage_, inputRects);
    
    EXPECT_EQ(shapeDetector_->getDetectCallCount(), 1);
    EXPECT_EQ(shapeDetector_->getLastImageProcessed(), testImage_.get());
    EXPECT_EQ(shapeDetector_->getLastFaceRects().size(), 2);
    EXPECT_EQ(faces.size(), 2);
}

TEST_F(DetectorsTest, ShapeDetectorWithEmptyRects)
{
    std::vector<math_utils::Rect<float>> emptyRects;
    
    auto faces = shapeDetector_->detect(testImage_, emptyRects);
    
    EXPECT_EQ(shapeDetector_->getDetectCallCount(), 1);
    EXPECT_TRUE(faces.empty());
}

TEST_F(DetectorsTest, ShapeDetectorWithNullImage)
{
    std::unique_ptr<Image> nullImage = nullptr;
    std::vector<math_utils::Rect<float>> inputRects = {
        math_utils::Rect<float>(10.0f, 20.0f, 50.0f, 60.0f)
    };
    
    auto faces = shapeDetector_->detect(nullImage, inputRects);
    
    EXPECT_EQ(shapeDetector_->getDetectCallCount(), 1);
    EXPECT_EQ(shapeDetector_->getLastImageProcessed(), nullptr);
    EXPECT_TRUE(faces.empty());
}

TEST_F(DetectorsTest, ShapeDetectorFailure)
{
    shapeDetector_->setShouldSucceed(false);
    
    std::vector<math_utils::Rect<float>> inputRects = {
        math_utils::Rect<float>(10.0f, 20.0f, 50.0f, 60.0f)
    };
    
    auto faces = shapeDetector_->detect(testImage_, inputRects);
    
    EXPECT_EQ(shapeDetector_->getDetectCallCount(), 1);
    EXPECT_TRUE(faces.empty());
}

TEST_F(DetectorsTest, MultipleDetectorCalls)
{
    // Test multiple calls to the same detector
    faceDetector_->detect(testImage_);
    faceDetector_->detect(testImage_);
    faceDetector_->detect(testImage_);
    
    EXPECT_EQ(faceDetector_->getDetectCallCount(), 3);
    
    std::vector<math_utils::Rect<float>> inputRects = {
        math_utils::Rect<float>(10.0f, 20.0f, 50.0f, 60.0f)
    };
    
    shapeDetector_->detect(testImage_, inputRects);
    shapeDetector_->detect(testImage_, inputRects);
    
    EXPECT_EQ(shapeDetector_->getDetectCallCount(), 2);
}

// Integration test - using face detector output as shape detector input
TEST_F(DetectorsTest, FaceToShapeDetectionPipeline)
{
    // Detect faces first
    auto faces = faceDetector_->detect(testImage_);
    EXPECT_FALSE(faces.empty());
    
    // Create mock face rectangles since Face doesn't expose bbox directly
    std::vector<math_utils::Rect<float>> faceRects = {
        math_utils::Rect<float>(10.0f, 10.0f, 100.0f, 100.0f)
    };
    
    // Use face rectangles for shape detection
    auto facesWithLandmarks = shapeDetector_->detect(testImage_, faceRects);
    
    EXPECT_EQ(facesWithLandmarks.size(), faceRects.size());
    EXPECT_FALSE(facesWithLandmarks.empty());
}
