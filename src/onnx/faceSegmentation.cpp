#include "LinuxFace/onnx/faceSegmentation.h"

#include <array>

#include "LinuxFace/profiler.h"

using namespace linuxface;

Ort::Value FaceSegmentationDetector::transform(const std::unique_ptr<Image>& image)
{
    // Create tensor with fixed dimensions for face segmentation model
    // Always set concrete dimensions to handle dynamic batch size (-1)
    input_node_dims[0] = 1;           // batch size
    input_node_dims[1] = 3;           // RGB channels
    input_node_dims[2] = InputHeight; // height
    input_node_dims[3] = InputWidth;  // width

    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // No padding for face segmentation - we resize the full image
    padding_ = TensorPadding::noPadding();

    // Get pointer to tensor data
    auto* tensorData = inputTensor.GetTensorMutableData<float>();

    // First convert image to tensor without normalization (get raw 0-255 values)
    image->toTensor(tensorData, padding_, InputWidth, InputHeight, NormalizationType::NONE);

    // Apply custom ImageNet normalization manually
    // Formula: normalized = (pixel_value - mean) / std
    // Where std = 1.0 / scale_val, so: normalized = (pixel_value - mean) * scale_val
    const int tensorSize = InputWidth * InputHeight;

    for (int c = 0; c < 3; ++c) // RGB channels
    {
        const float mean = MeanVals[c];
        const float scale = ScaleVals[c];
        float* channelData = tensorData + c * tensorSize;

        for (int i = 0; i < tensorSize; ++i)
        {
            channelData[i] = (channelData[i] - mean) * scale;
        }
    }

    return inputTensor;
}

bool FaceSegmentationDetector::detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& labelMask)
{
    Profiler::getInstance().start("FaceSegmentationDetector", "Face segmentation detection");

    if (image->empty())
    {
        common::logError("FaceSegmentationDetector: Input image is empty");
        return false;
    }
    if (image->info.width != InputWidth || image->info.height != InputHeight)
    {
        common::logWarn("FaceSegmentationDetector: Input image size is %lux%lu, expected %dx%d.", image->info.width,
                        image->info.height, InputWidth, InputHeight);
        return false;
    }
    bool worked{false};
    try
    {
        // Transform input image to tensor
        const Ort::Value inputTensor = this->transform(image);
        auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                    output_node_names_.data(), 1);

        // Generate segmentation mask
        generateMask(outputTensors, image, labelMask);
        worked = true;
    }
    catch (const Ort::Exception& e)
    {
        common::logError("FaceSegmentationDetector: ONNX Runtime error: %s", e.what());
        worked = false;
    }
    catch (const std::exception& e)
    {
        common::logError("FaceSegmentationDetector: Standard exception: %s", e.what());
        worked = false;
    }
    Profiler::getInstance().stop("FaceSegmentationDetector", "Face segmentation detection");
    return worked;
}

void FaceSegmentationDetector::generateMask(std::vector<Ort::Value>& outputTensors,
                                            const std::unique_ptr<Image>& originalImage,
                                            std::unique_ptr<Image>& labelMask)
{
    auto& outputTensor = outputTensors.front(); // Expected shape: (1, height, width)

    // Get tensor shape information
    const auto outputDims = outputTensor.GetTensorTypeAndShapeInfo().GetShape();
    if (outputDims.size() != 3)
    {
        common::logError("FaceSegmentationDetector: Unexpected output tensor shape");
        return;
    }
    const int outHeight = static_cast<int>(outputDims[1]);
    const int outWidth = static_cast<int>(outputDims[2]);
    if (outWidth != InputWidth || outHeight != InputHeight)
    {
        common::logError("FaceSegmentationDetector: Unexpected output tensor dimensions");
        return;
    }


    // Get tensor data
    const unsigned char* outputData = outputTensor.GetTensorMutableData<unsigned char>();
    const size_t pixelCount = outHeight * outWidth;

    // Create label mask
    labelMask = std::make_unique<Image>(pixelCount);
    labelMask->info.width = outWidth;
    labelMask->info.height = outHeight;
    labelMask->info.pixelSizeBytes = 1;
    labelMask->info.format = ImageFormat::GRAYSCALE;

    memcpy(labelMask->data(), outputData, pixelCount);
}

void FaceSegmentationDetector::applySegmentationVisualization(Image& faceImage, const Image& labelMask)
{
    // Define colors for each face segment
    static const std::array<Pixel, 19> segmentColors = {
        {
         Pixel(0, 0, 0),       // BACKGROUND - black
            Pixel(255, 220, 177), // SKIN - light peach
            Pixel(139, 69, 19),   // L_BROW - brown
            Pixel(160, 82, 45),   // R_BROW - saddle brown
            Pixel(0, 100, 0),     // L_EYE - dark green
            Pixel(0, 128, 0),     // R_EYE - green
            Pixel(75, 0, 130),    // EYE_G - indigo (glasses)
            Pixel(255, 192, 203), // L_EAR - pink
            Pixel(255, 182, 193), // R_EAR - light pink
            Pixel(255, 215, 0),   // EAR_R - gold (earring)
            Pixel(255, 105, 180), // NOSE - hot pink
            Pixel(220, 20, 60),   // MOUTH - crimson
            Pixel(255, 0, 0),     // U_LIP - red
            Pixel(178, 34, 34),   // L_LIP - firebrick
            Pixel(210, 180, 140), // NECK - tan
            Pixel(255, 215, 0),   // NECK_L - gold (necklace)
            Pixel(0, 0, 255),     // CLOTH - blue
            Pixel(165, 42, 42),   // HAIR - brown
            Pixel(128, 0, 128)    // HAT - purple
        }
    };

    // Apply colored overlay with transparency
    constexpr float alpha = 0.6f; // 60% opacity for segments, 40% original image
    unsigned char* imageData = faceImage.data();
    const unsigned char* maskData = labelMask.data();

    for (unsigned long y = 0; y < faceImage.info.height; ++y)
    {
        for (unsigned long x = 0; x < faceImage.info.width; ++x)
        {
            const size_t pixelIdx = y * faceImage.info.width + x;
            const size_t imagePixelIdx = pixelIdx * faceImage.info.pixelSizeBytes;

            const unsigned char segmentClass = maskData[pixelIdx];

            // Skip background (class 0)
            if (segmentClass > 0 && segmentClass < segmentColors.size())
            {
                const auto& segmentColor = segmentColors[segmentClass];

                // Blend colors: result = alpha * segmentColor + (1-alpha) * originalColor
                imageData[imagePixelIdx] =
                    static_cast<unsigned char>(alpha * segmentColor.r + (1.0f - alpha) * imageData[imagePixelIdx]);
                imageData[imagePixelIdx + 1] =
                    static_cast<unsigned char>(alpha * segmentColor.g + (1.0f - alpha) * imageData[imagePixelIdx + 1]);
                imageData[imagePixelIdx + 2] =
                    static_cast<unsigned char>(alpha * segmentColor.b + (1.0f - alpha) * imageData[imagePixelIdx + 2]);
            }
        }
    }
}
