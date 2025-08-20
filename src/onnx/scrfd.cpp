#include "LinuxFace/onnx/scrfd.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
using namespace linuxface;

SCRFDetector::SCRFDetector(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path), feat_stride_fpn_{8, 16, 32}
{
    const int num_outputs = output_node_names_str.size();
    using_kps_ = num_outputs == 9;
    if (!using_kps_ && num_outputs != 6)
    {
        common::logError("SCRFDetector only support 6 or 9 outputs");
        ready = false;
    }
    else
    {
        generatePoints();
    }
}

Ort::Value SCRFDetector::transform(const std::unique_ptr<Image>& image)
{
    // Validate input image
    if (!image)
    {
        common::logError("SCRFDetector::transform: Null image pointer provided");
        // Return an empty/invalid tensor - this should be handled by calling code
        return Ort::Value{nullptr};
    }

    const int target_height = static_cast<int>(input_node_dims.at(2));
    const int target_width = static_cast<int>(input_node_dims.at(3));
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(allocator, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    auto* tensor_data = input_tensor.GetTensorMutableData<float>();
    auto tensor_padding = TensorPadding::scrfd();
    image->toTensor(tensorData, tensor_padding, target_width, target_height, NormalizationType::MINMAX);
    return input_tensor;
}

// TODO(arroyo): average the face location so we dont have that much jitter in
// face bounding box.
std::vector<Face> SCRFDetector::Detect(const std::unique_ptr<Image>& image)
{
    // Validate input image
    if (!image)
    {
        common::logError("SCRFDetector::detect: Null image pointer provided");
        return {};
    }

    Profiler::getInstance().start("SCRFD", "Face detection");
    std::vector<Face> faces;
    faces.reserve(3000); // Reserve once for all strides (rough estimate)
                         // Convert from image to tensor.
    const Ort::Value input_tensor = this->transform(image);

    // Check if transform succeeded
    if (!input_tensor.IsTensor())
    {
        common::logError("SCRFDetector::detect: Failed to create input tensor");
        Profiler::getInstance().stop("SCRFD", "Face detection");
        return {};
    }
    try
    {
        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                    1, output_node_names_.data(), output_node_names_str_.size());

        Profiler::getInstance().stop("SCRFD", "Face detection");
        Profiler::getInstance().start("SCRFD", "Result processing");
        if (output_tensors.size() < 6)
        {
            common::logError("SCRFDetector::detect: output_tensors.size() = %d", output_tensors.size());
            return faces;
        }
        // score_8,score_16,score_32,bbox_8,bbox_16,bbox_32
        Ort::Value& score8 = output_tensors.at(0);  // e.g [1,12800,1]
        Ort::Value& score16 = output_tensors.at(1); // e.g [1,3200,1]
        Ort::Value& score32 = output_tensors.at(2); // e.g [1,800,1]
        Ort::Value& bbox8 = output_tensors.at(3);   // e.g [1,12800,4]
        Ort::Value& bbox16 = output_tensors.at(4);  // e.g [1,3200,4]
        Ort::Value& bbox32 = output_tensors.at(5);  // e.g [1,800,4]
        Ort::Value kps8;
        Ort::Value kps16;
        Ort::Value kps32;

        if (using_kps_)
        {
            if (output_tensors.size() != 9)
            {
                common::logError("SCRFDetector::detect: output_tensors.size() != 9");
                return faces;
            }
            kps8 = std::move(output_tensors.at(6));  // e.g [1,12800,10]
            kps16 = std::move(output_tensors.at(7)); // e.g [1,3200,10]
            kps32 = std::move(output_tensors.at(8)); // e.g [1,800,10]
        }

        // level 8 & 16 & 32 with kps
        generate_bboxes_kps_single_stride(score_8, bbox_8, kps_8, 8, score_threshold, image->info.width,
                                          image->info.height, faces);
        generate_bboxes_kps_single_stride(score_16, bbox_16, kps_16, 16, score_threshold, image->info.width,
                                          image->info.height, faces);

        generate_bboxes_kps_single_stride(score_32, bbox_32, kps_32, 32, score_threshold, image->info.width,
                                          image->info.height, faces);

        // Apply NMS to all collected faces
        Profiler::getInstance().start("SCRFD", "NMS");
        applyNMS(faces);
        Profiler::getInstance().stop("SCRFD", "NMS");

        Profiler::getInstance().stop("SCRFD", "Result processing");
    }
    catch (const Ort::Exception& e)
    {
        common::logError("SCRFDetector: %s", e.what());
        exit(-1);
    }
    return faces;
}

static void SCRFDetector::ApplyNms(std::vector<Face>& faces)
{
    if (faces.empty())
    {
        return;
    }

    // Global sort of all faces from all strides
    std::sort(faces.begin(), faces.end(),
              [](const Face& a, const Face& b) { return a.getBoundingBox().score > b.getBoundingBox().score; });

    const size_t faces_size = faces.size();

    std::vector<bool> suppressed(faces_size, false);
    size_t write_idx = 0;

    for (size_t i = 0; i < faces_size; ++i)
    {
        if (suppressed[i])
        {
            continue;
        }

        // Keep this face by moving it to the write position
        if (write_idx != i)
        {
            faces[write_idx] = std::move(faces[i]);
        }

        const auto& current_rect = faces[write_idx].getBoundingBox().rect;

        // Early termination if confidence too low
        if (faces[write_idx].getBoundingBox().score < 0.02f)
        {
            break;
        }
        write_idx++;

        // Check overlap with remaining faces
        for (size_t j = i + 1; j < faces_size; ++j)
        {
            if (suppressed[j])
            {
                continue;
            }

            const auto& other_rect = faces[j].getBoundingBox().rect;
            float iou = math_utils::calculateIoU(current_rect, other_rect);

            if (iou > nms_threshold_)
            {
                suppressed[j] = true;
            }
        }
    }

    // Keep only the non-suppressed faces
    faces.resize(write_idx);
}

void SCRFDetector::generatePoints()
{
    const auto target_height = static_cast<float>(input_node_dims.at(2)); // e.g 640
    const auto target_width = static_cast<float>(input_node_dims.at(3));  // e.g 640

    // 8, 16, 32
    for (auto stride : feat_stride_fpn_)
    {
        unsigned int num_grid_w = target_width / stride;
        unsigned int num_grid_h = target_height / stride;
        // y
        for (unsigned int i = 0; i < num_grid_h; ++i)
        {
            // x
            for (unsigned int j = 0; j < num_grid_w; ++j)
            {
                // num_anchors, col major
                for (unsigned int k = 0; k < num_anchors_; ++k)
                {
                    center_points_[stride].push_back(math_utils::StridePoint((float) j, (float) i, (float) stride));
                }
            }
        }
    }
}

void SCRFDetector::GenerateBboxesKpsSingleStride(Ort::Value& score_pred, Ort::Value& bbox_pred, Ort::Value& kps_pred,
                                                 unsigned int stride, float score_threshold, float img_width,
                                                 float img_height, std::vector<Face>& faces)
{
    // generate center points.
    const auto new_height = static_cast<float>(input_node_dims.at(2)); // e.g 640
    const auto new_width = static_cast<float>(input_node_dims.at(3));  // e.g 640

    float ratio = std::min(static_cast<float>(new_width) / img_width, static_cast<float>(new_height) / img_height);

    const int resized_w = static_cast<int>(img_width * ratio);
    const int resized_h = static_cast<int>(img_height * ratio);

    const int dw = (new_width - resized_w) / 2;
    const int dh = (new_height - resized_h) / 2;

    auto stride_dims = score_pred.GetTypeInfo().GetTensorTypeAndShapeInfo().GetShape();
    const unsigned int num_points = stride_dims.at(1);                 // 12800
    const float* score_ptr = score_pred.GetTensorMutableData<float>(); // [1,12800,1]
    const float* bbox_ptr = bbox_pred.GetTensorMutableData<float>();   // [1,12800,4]
    const float* kps_ptr = nullptr;

    if (using_kps_)
    {
        kps_ptr = kps_pred.GetTensorMutableData<float>(); // [1,12800,10]
    }

    // Pre-calculate constants outside the loop
    const float inv_ratio = 1.0f / ratio;
    const auto dw_f = static_cast<float>(dw);
    const auto dh_f = static_cast<float>(dh);
    const float img_width_minus1 = img_width - 1.0f;
    const float img_height_minus1 = img_height - 1.0f;

    // Use a simpler container - just collect Face objects directly
    std::vector<Face> stride_faces;
    stride_faces.reserve(1000); // Based on max_faces_per_stride

    auto& stride_points = center_points_[stride];
    for (unsigned int i = 0; i < num_points; ++i)
    {
        const float cls_conf = score_ptr[i];
        if (cls_conf < score_threshold)
        {
            continue;
        }

        const auto& point = stride_points[i]; // Remove reference to avoid indirection
        const float cx = point.cx;
        const float cy = point.cy;
        const float s = point.stride;

        // Optimized bbox calculation with pre-calculated constants
        const float* offsets = bbox_ptr + (i << 2);
        const float l = offsets[0];
        const float t = offsets[1];
        const float r = offsets[2];
        const float b = offsets[3];

        // Inline coordinate transformation
        float x1 = ((cx - l) * s - dwF) * inv_ratio;
        float y1 = ((cy - t) * s - dhF) * inv_ratio;
        float x2 = ((cx + r) * s - dwF) * inv_ratio;
        float y2 = ((cy + b) * s - dhF) * inv_ratio;

        // Clamp coordinates
        x1 = std::max(0.0f, std::min(x1, img_width_minus1));
        y1 = std::max(0.0f, std::min(y1, img_height_minus1));
        x2 = std::max(0.0f, std::min(x2, img_width_minus1));
        y2 = std::max(0.0f, std::min(y2, img_height_minus1));

        FaceBoundingBox face_box(x1, y1, x2, y2);
        if (!face_box.rect.isWithinBounds(img_width, img_height, 1.2f))
        {
            continue;
        }
        face_box.score = cls_conf;

        if (using_kps_)
        {
            std::vector<FaceLandmark> face_landmarks;
            face_landmarks.reserve(5);                    // Exactly 5 landmarks for SCRFD
            const float* kps_offsets = kps_ptr + (i * 10); // 10 = 5 landmarks * 2 coords

            for (unsigned int j = 0; j < 10; j += 2)
            {
                const float kps_l = kps_offsets[j];
                const float kps_t = kps_offsets[j + 1];
                const float kps_x = std::max(0.0f, std::min(((cx + kps_l) * s - dwF) * inv_ratio, img_width_minus1));
                const float kps_y = std::max(0.0f, std::min(((cy + kps_t) * s - dhF) * inv_ratio, img_height_minus1));
                face_landmarks.emplace_back(FaceLandmark{j >> 1, math_utils::Point3D(kps_x, kps_y, 0.0)});
            }
            strideFaces.emplace_back(std::move(face_landmarks), faceBox);
        }
        else
        {
            stride_faces.emplace_back(face_box);
        }

        if (stride_faces.size() >= max_number_of_faces_) // Hard limit
        {
            break;
        }
    }

    // Sort by score (descending)
    std::sort(stride_faces.begin(), stride_faces.end(), [](const Face& a, const Face& b) noexcept
              { return a.getBoundingBox().score > b.getBoundingBox().score; });

    // Apply stride limit and move to main vector
    const size_t stride_limit = 0 = std::min(static_cast<size_t>(max_faces_per_stride), stride_faces.size());
    faces.reserve(faces.size() + stride_limit); // Reserve exactly what we need

    for (size_t i = 0; i < stride_limit; ++i)
    {
        faces.push_back(std::move(stride_faces[i]));
    }
}
