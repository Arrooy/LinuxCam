#include "LinuxFace/onnx/metric3d.h"

#include "LinuxFace/common.h"
#include "LinuxFace/depthImage.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

Metric3D::Metric3D(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
    if (output_node_names_str_.size() != 3)
    {
        common::logError("Metric3D expects 3 outputs (predicted_depth, predicted_normal, normal_confidence), got %zu",
                          output_node_names_str_.size());
        ready_ = false;
        return;
    }

    // Reduce inference size significantly for faster processing
    target_height_ = 616; // Reduced from 616 (about 3x smaller)
    target_width_ = 1064; // Reduced from 1064 (about 3x smaller)

    ready_ = true;
}

Ort::Value Metric3D::transform(const std::unique_ptr<Image>& image)
{
    // Create tensor with fixed dimensions instead of dynamic input_node_dims
    std::vector<int64_t> inputShape = {1, 3, target_height_, target_width_}; // [batch, channels, height, width]

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(allocator_, inputShape.data(), inputShape.size());

    // Get pointer to tensor data.
    auto* tensorData = inputTensor.GetTensorMutableData<float>();

    // Store the padding for later use in fromTensor
    padding_ = TensorPadding::metric3d();

    // Use Metric3D specific padding
    image->toTensor(tensorData, padding_, target_width_, target_height_, NormalizationType::NONE);

    return inputTensor;
}

std::unique_ptr<DepthImage> Metric3D::detectDepth(const std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("Metric3D", "Depth detection");
    const Ort::Value inputTensor = this->transform(image);

    try
    {
        auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                    output_node_names_.data(), output_node_names_str_.size());

        if (outputTensors.size() != 3)
        {
            common::logError("Metric3D::detect_depth: expected 3 output tensors, got %zu", outputTensors.size());
            return nullptr;
        }

        // Extract all three outputs
        Ort::Value& depthOutput = outputTensors.at(0);      // predicted_depth
        const Ort::Value& normalOutput = outputTensors.at(1);     // predicted_normal
        const Ort::Value& confidenceOutput = outputTensors.at(2); // normal_confidence

        // Get shape information for all outputs
        auto depthTypeInfo = depthOutput.GetTensorTypeAndShapeInfo();
        auto depthShape = depthTypeInfo.GetShape();

        auto normalTypeInfo = normalOutput.GetTensorTypeAndShapeInfo();
        auto normalShape = normalTypeInfo.GetShape();

        auto confidenceTypeInfo = confidenceOutput.GetTensorTypeAndShapeInfo();
        auto confidenceShape = confidenceTypeInfo.GetShape();

        common::logInfo("Metric3D depth output shape dimensions: %zu", depthShape.size());
        for (size_t i = 0; i < depthShape.size(); ++i)
        {
            common::logInfo("Metric3D depth output shape[%zu]: %lld", i, depthShape[i]);
        }

        common::logInfo("Metric3D normal output shape dimensions: %zu", normalShape.size());
        for (size_t i = 0; i < normalShape.size(); ++i)
        {
            common::logInfo("Metric3D normal output shape[%zu]: %lld", i, normalShape[i]);
        }

        common::logInfo("Metric3D confidence output shape dimensions: %zu", confidenceShape.size());
        for (size_t i = 0; i < confidenceShape.size(); ++i)
        {
            common::logInfo("Metric3D confidence output shape[%zu]: %lld", i, confidenceShape[i]);
        }

        // Calculate actual tensor size from shape - use tensor output dimensions
        int tensorHeight = 0;
        int tensorWidth = 0;

        // Assuming format is [batch, height, width] or [batch, 1, height, width]
        if (depthShape.size() == 3)
        {
            tensorHeight = static_cast<int>(depthShape[1]);
            tensorWidth = static_cast<int>(depthShape[2]);
        }
        else if (depthShape.size() == 4)
        {
            tensorHeight = static_cast<int>(depthShape[2]);
            tensorWidth = static_cast<int>(depthShape[3]);
        }

        common::logInfo("Metric3D using tensor dimensions: %dx%d", tensorWidth, tensorHeight);

        // Get tensor data pointers
        auto* depthData = depthOutput.GetTensorMutableData<float>();
        // auto normalData = normalOutput.GetTensorMutableData<float>();
        // auto confidenceData = confidenceOutput.GetTensorMutableData<float>();

        // Step 1: Remove padding from tensor output to get back to scaled image size
        // Calculate the scaled image dimensions that were used during padding
        const int origW = image->info.width;
        const int origH = image->info.height;
        const float scale =
            std::min(static_cast<float>(target_width_) / origW, static_cast<float>(target_height_) / origH);
        const int scaledW = static_cast<int>(origW * scale);
        const int scaledH = static_cast<int>(origH * scale);

        // Calculate padding that was added (same logic as Python pad_info)
        const int padH = target_height_ - scaledH;
        const int padW = target_width_ - scaledW;
        const int padHHalf = padH / 2;
        const int padWHalf = padW / 2;

        common::logInfo("Metric3D padding info: pad_h_half=%d, pad_w_half=%d, scaledW=%d, scaledH=%d", padHHalf,
                         padWHalf, scaledW, scaledH);

        // Create intermediate depth image with the unpadded (scaled) dimensions
        std::unique_ptr<DepthImage> scaledDepthImage = std::make_unique<DepthImage>(scaledW, scaledH);
        float* scaledDepthData = scaledDepthImage->getDepthData();

        // Copy depth data from tensor, removing padding (equivalent to Python's
        // depth[pad_info[0]:input_size[0]-pad_info[1], ...])
        for (int h = 0; h < scaledH; ++h)
        {
            for (int w = 0; w < scaledW; ++w)
            {
                const int tensorH = h + padHHalf; // Add back the padding offset
                const int tensorW = w + padWHalf; // Add back the padding offset

                // Bounds check
                if (tensorH >= 0 && tensorH < tensorHeight && tensorW >= 0 && tensorW < tensorWidth)
                {
                    const int tensorIdx = tensorH * tensorWidth + tensorW;
                    const int scaledIdx = h * scaledW + w;
                    scaledDepthData[scaledIdx] = depthData[tensorIdx];
                }
            }
        }

        // Step 2: Scale the unpadded depth image back to original dimensions
        auto finalDepthImage = scaledDepthImage->scale(origW, origH);
        if (!finalDepthImage)
        {
            common::logError("Failed to scale depth image back to original dimensions");
            return nullptr;
        }

        Profiler::getInstance().stop("Metric3D", "Depth detection");
        return finalDepthImage;
    }
    catch (const Ort::Exception& e)
    {
        common::logError("Metric3D::detect_depth ONNX error: %s", e.what());
        Profiler::getInstance().stop("Metric3D", "Depth detection");
        return nullptr;
    }
}

#if 0
// 
if (metric3dDetector_ != nullptr && metric3dDetector_->isReady())
    {
        auto depth_image = metric3dDetector_->detect_depth(testImg_);
        if (depth_image)
        {
            // Get depth statistics to normalize the visualization
            auto stats = depth_image->getDepthStats();

            if (stats.validPixels > 0)
            {
                common::logInfo("Depth stats - Min: %.2f, Max: %.2f, Mean: %.2f, Valid pixels: %lu", stats.minDepth,
                                 stats.maxDepth, stats.meanDepth, stats.validPixels);


                // Create a grayscale visualization of the depth data
                auto depth_viz = std::make_unique<Image>(depth_image->info.width * depth_image->info.height * 3);
                depth_viz->info.width = depth_image->info.width;
                depth_viz->info.height = depth_image->info.height;
                depth_viz->info.pixelSizeBytes = 3;
                depth_viz->info.format = ImageFormat::RGB;
                depth_viz->info.x = 0;
                depth_viz->info.y = 0;

                const float* depthData = depth_image->getDepthData();
                unsigned char* vizData = testImg_->data();

                // Normalize depth values to 0-255 range for visualization
                float depthRange = stats.maxDepth - stats.minDepth;
                if (depthRange > 0.0f)
                {
                    for (unsigned long i = 0; i < depth_image->info.width * depth_image->info.height; ++i)
                    {
                        float depth = depthData[i];
                        unsigned char grayValue = 0;

                        if (depth > 0.0f) // Valid depth value
                        {
                            // Normalize to 0-1 range, then to 0-255
                            float normalized = (depth - stats.minDepth) / depthRange;
                            grayValue = static_cast<unsigned char>(normalized * 255.0f);
                        }

                        // Set RGB values (grayscale)
                        vizData[i * 3] = grayValue;     // R
                        vizData[i * 3 + 1] = grayValue; // G
                        vizData[i * 3 + 2] = grayValue; // B
                    }
                }
            }
        }
    }
#endif
