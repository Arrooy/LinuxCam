#include "LinuxFace/onnx/metric3d.h"

#include "LinuxFace/common.h"
#include "LinuxFace/depthImage.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

Metric3D::Metric3D(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    if (output_node_names_str_.size() != 3)
    {
        common::log_error("Metric3D expects 3 outputs (predicted_depth, predicted_normal, normal_confidence), got %zu",
                          output_node_names_str_.size());
        ready_ = false;
        return;
    }

    // Reduce inference size significantly for faster processing
    target_height_ = 616; // Reduced from 616 (about 3x smaller)
    target_width_ = 1064; // Reduced from 1064 (about 3x smaller)

    ready_ = true;
}

std::vector<Ort::Value> Metric3D::transform(const std::unique_ptr<Image>& image)
{
    // Create tensor with fixed dimensions instead of dynamic input_node_dims
    std::vector<int64_t> input_shape = {1, 3, target_height_, target_width_}; // [batch, channels, height, width]

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(allocator_, input_shape.data(), input_shape.size());

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();

    // Store the padding for later use in fromTensor
    padding_ = TensorPadding::metric3d();

    // Use Metric3D specific padding
    image->toTensor(tensor_data, padding_, target_width_, target_height_, NormalizationType::NONE);

    // Use initializer list for efficiency
    std::vector<Ort::Value> input_result;
    input_result.emplace_back(std::move(input_tensor));
    return input_result;
}

std::unique_ptr<DepthImage> Metric3D::detect_depth(const std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("Metric3D", "Depth detection");
    Ort::Value input_tensor = std::move(this->transform(image)[0]);

    try
    {
        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                     1, output_node_names_.data(), output_node_names_str_.size());

        if (output_tensors.size() != 3)
        {
            common::log_error("Metric3D::detect_depth: expected 3 output tensors, got %zu", output_tensors.size());
            return nullptr;
        }

        // Extract all three outputs
        Ort::Value& depth_output = output_tensors.at(0);      // predicted_depth
        Ort::Value& normal_output = output_tensors.at(1);     // predicted_normal
        Ort::Value& confidence_output = output_tensors.at(2); // normal_confidence

        // Get shape information for all outputs
        auto depth_type_info = depth_output.GetTensorTypeAndShapeInfo();
        auto depth_shape = depth_type_info.GetShape();

        auto normal_type_info = normal_output.GetTensorTypeAndShapeInfo();
        auto normal_shape = normal_type_info.GetShape();

        auto confidence_type_info = confidence_output.GetTensorTypeAndShapeInfo();
        auto confidence_shape = confidence_type_info.GetShape();

        common::log_info("Metric3D depth output shape dimensions: %zu", depth_shape.size());
        for (size_t i = 0; i < depth_shape.size(); ++i)
        {
            common::log_info("Metric3D depth output shape[%zu]: %lld", i, depth_shape[i]);
        }

        common::log_info("Metric3D normal output shape dimensions: %zu", normal_shape.size());
        for (size_t i = 0; i < normal_shape.size(); ++i)
        {
            common::log_info("Metric3D normal output shape[%zu]: %lld", i, normal_shape[i]);
        }

        common::log_info("Metric3D confidence output shape dimensions: %zu", confidence_shape.size());
        for (size_t i = 0; i < confidence_shape.size(); ++i)
        {
            common::log_info("Metric3D confidence output shape[%zu]: %lld", i, confidence_shape[i]);
        }

        // Calculate actual tensor size from shape - use tensor output dimensions
        int tensor_height;
        int tensor_width;

        // Assuming format is [batch, height, width] or [batch, 1, height, width]
        if (depth_shape.size() == 3)
        {
            tensor_height = static_cast<int>(depth_shape[1]);
            tensor_width = static_cast<int>(depth_shape[2]);
        }
        else if (depth_shape.size() == 4)
        {
            tensor_height = static_cast<int>(depth_shape[2]);
            tensor_width = static_cast<int>(depth_shape[3]);
        }

        common::log_info("Metric3D using tensor dimensions: %dx%d", tensor_width, tensor_height);

        // Get tensor data pointers
        auto depth_data = depth_output.GetTensorMutableData<float>();
        // auto normal_data = normal_output.GetTensorMutableData<float>();
        // auto confidence_data = confidence_output.GetTensorMutableData<float>();

        // Step 1: Remove padding from tensor output to get back to scaled image size
        // Calculate the scaled image dimensions that were used during padding
        const int origW = image->info.width;
        const int origH = image->info.height;
        float scale = std::min(static_cast<float>(target_width_) / origW, static_cast<float>(target_height_) / origH);
        int scaledW = static_cast<int>(origW * scale);
        int scaledH = static_cast<int>(origH * scale);

        // Calculate padding that was added (same logic as Python pad_info)
        int pad_h = target_height_ - scaledH;
        int pad_w = target_width_ - scaledW;
        int pad_h_half = pad_h / 2;
        int pad_w_half = pad_w / 2;

        common::log_info("Metric3D padding info: pad_h_half=%d, pad_w_half=%d, scaledW=%d, scaledH=%d", pad_h_half,
                         pad_w_half, scaledW, scaledH);

        // Create intermediate depth image with the unpadded (scaled) dimensions
        std::unique_ptr<DepthImage> scaled_depth_image = std::make_unique<DepthImage>(scaledW, scaledH);
        float* scaled_depth_data = scaled_depth_image->getDepthData();

        // Copy depth data from tensor, removing padding (equivalent to Python's
        // depth[pad_info[0]:input_size[0]-pad_info[1], ...])
        for (int h = 0; h < scaledH; ++h)
        {
            for (int w = 0; w < scaledW; ++w)
            {
                int tensor_h = h + pad_h_half; // Add back the padding offset
                int tensor_w = w + pad_w_half; // Add back the padding offset

                // Bounds check
                if (tensor_h >= 0 && tensor_h < tensor_height && tensor_w >= 0 && tensor_w < tensor_width)
                {
                    int tensor_idx = tensor_h * tensor_width + tensor_w;
                    int scaled_idx = h * scaledW + w;
                    scaled_depth_data[scaled_idx] = depth_data[tensor_idx];
                }
            }
        }

        // Step 2: Scale the unpadded depth image back to original dimensions
        auto final_depth_image = scaled_depth_image->scale(origW, origH);
        if (!final_depth_image)
        {
            common::log_error("Failed to scale depth image back to original dimensions");
            return nullptr;
        }

        Profiler::getInstance().stop("Metric3D", "Depth detection");
        return final_depth_image;
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("Metric3D::detect_depth ONNX error: %s", e.what());
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
                common::log_info("Depth stats - Min: %.2f, Max: %.2f, Mean: %.2f, Valid pixels: %lu", stats.minDepth,
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
