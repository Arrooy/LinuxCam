#include "FunnyFace/onnx/scrfd.h"

#include "FunnyFace/common.h"

using namespace funnyface;

SCRFDetector::SCRFDetector(const std::string& onnx_model_path)
    : OnnxDetector(onnx_model_path), feat_stride_fpn_{8, 16, 32}
{
    const int num_outputs = output_node_names_str_.size();
    using_kps_ = num_outputs == 9;
    if (!using_kps_ && num_outputs != 6)
    {
        common::log_error("SCRFDetector only support 6 or 9 outputs");
        ready_ = false;
    }
    else
    {
        generate_points();
    }
}

Ort::Value SCRFDetector::transform(const std::unique_ptr<Image>& image)
{
    const int target_height = static_cast<int>(input_node_dims.at(2));
    const int target_width = static_cast<int>(input_node_dims.at(3));
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    image->toTensor(tensor_data, 0, target_width, target_height);

    return input_tensor;
}

std::vector<Face> SCRFDetector::detect(const std::unique_ptr<Image>& image)
{
    std::vector<Face> faces;
    // Convert from image to tensor.
    Ort::Value input_tensor = this->transform(image);
    try
    {
        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                     1, output_node_names_.data(), output_node_names_str_.size());

        if (output_tensors.size() < 6)
        {
            common::log_error("SCRFDetector::detect: output_tensors.size() = %d", output_tensors.size());
            return faces;
        }
        // score_8,score_16,score_32,bbox_8,bbox_16,bbox_32
        Ort::Value& score_8 = output_tensors.at(0);  // e.g [1,12800,1]
        Ort::Value& score_16 = output_tensors.at(1); // e.g [1,3200,1]
        Ort::Value& score_32 = output_tensors.at(2); // e.g [1,800,1]
        Ort::Value& bbox_8 = output_tensors.at(3);   // e.g [1,12800,4]
        Ort::Value& bbox_16 = output_tensors.at(4);  // e.g [1,3200,4]
        Ort::Value& bbox_32 = output_tensors.at(5);  // e.g [1,800,4]
        Ort::Value kps_8;
        Ort::Value kps_16;
        Ort::Value kps_32;

        if (using_kps_)
        {
            if (output_tensors.size() != 9)
            {
                common::log_error("SCRFDetector::detect: output_tensors.size() != 9");
                return faces;
            }
            kps_8 = std::move(output_tensors.at(6));  // e.g [1,12800,10]
            kps_16 = std::move(output_tensors.at(7)); // e.g [1,3200,10]
            kps_32 = std::move(output_tensors.at(8)); // e.g [1,800,10]
        }

        // level 8 & 16 & 32 with kps
        generate_bboxes_kps_single_stride(score_8, bbox_8, kps_8, 8, score_threshold, image->info.width,
                                          image->info.height, faces);
        generate_bboxes_kps_single_stride(score_16, bbox_16, kps_16, 16, score_threshold, image->info.width,
                                          image->info.height, faces);

        generate_bboxes_kps_single_stride(score_32, bbox_32, kps_32, 32, score_threshold, image->info.width,
                                          image->info.height, faces);

        // TODO: nms_bboxes_kps
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("SCRFDetector: %s", e.what());
        exit(-1);
    }
    return faces;
}


void SCRFDetector::generate_points()
{
    const float target_height = static_cast<float>(input_node_dims.at(2)); // e.g 640
    const float target_width = static_cast<float>(input_node_dims.at(3));  // e.g 640

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


std::vector<Face>
SCRFDetector::generate_bboxes_kps_single_stride(Ort::Value& score_pred, Ort::Value& bbox_pred, Ort::Value& kps_pred,
                                                unsigned int stride, float score_threshold, float img_width,
                                                float img_height, std::vector<Face>& faces)
{
    // generate center points.
    const float new_height = 640; // TODO: make dinamic.
    const float new_width = 640;
    float ratio = std::min(static_cast<float>(new_width) / img_width, static_cast<float>(new_height) / img_height);

    const int resizedW = static_cast<int>(img_width * ratio);
    const int resizedH = static_cast<int>(img_height * ratio);

    const int dw = (new_width - resizedW) / 2;
    const int dh = (new_height - resizedH) / 2;

    // Generate max of 30000 boxes.
    constexpr const unsigned int max_nms = 30000;
    // Sort boxes and return best ones.
    constexpr const unsigned int nms_pre = 1000;

    unsigned int nms_pre_ = (stride / 8) * nms_pre; // 1 * 1000,2*1000,...
    nms_pre_ = nms_pre_ >= nms_pre ? nms_pre_ : nms_pre;

    auto stride_dims = score_pred.GetTypeInfo().GetTensorTypeAndShapeInfo().GetShape();
    const unsigned int num_points = stride_dims.at(1);                 // 12800
    const float* score_ptr = score_pred.GetTensorMutableData<float>(); // [1,12800,1]
    const float* bbox_ptr = bbox_pred.GetTensorMutableData<float>();   // [1,12800,4]
    const float* kps_ptr;
    if (using_kps_)
    {
        kps_ptr = kps_pred.GetTensorMutableData<float>(); // [1,12800,10]
    }

    unsigned int count = 0;
    auto& stride_points = center_points_[stride];
    for (unsigned int i = 0; i < num_points; ++i)
    {
        const float cls_conf = score_ptr[i];
        if (cls_conf < score_threshold)
        {
            continue; // filter
        }
        auto& point = stride_points.at(i);
        const float cx = point.cx;    // cx
        const float cy = point.cy;    // cy
        const float s = point.stride; // stride

        // bbox
        const float* offsets = bbox_ptr + i * 4;
        float l = offsets[0]; // left
        float t = offsets[1]; // top
        float r = offsets[2]; // right
        float b = offsets[3]; // bottom

        float x1 = ((cx - l) * s - (float) dw) / ratio; // cx - l x1
        float y1 = ((cy - t) * s - (float) dh) / ratio; // cy - t y1
        float x2 = ((cx + r) * s - (float) dw) / ratio; // cx + r x2
        float y2 = ((cy + b) * s - (float) dh) / ratio; // cy + b y2
        x1 = std::max(0.f, x1);
        y1 = std::max(0.f, y1);
        x2 = std::min(img_width - 1.f, x2);
        y2 = std::min(img_height - 1.f, y2);

        FaceBoundingBox face_box(x1, y1, x2, y2);
        if(!face_box.rect.isWithinBounds(img_width, img_height, 1.2f))
        {
            continue; // filter out of bounds boxes
        }
        face_box.score = cls_conf;

        if (using_kps_)
        {
            std::vector<FaceLandmark> face_landmarks;
            const float* kps_offsets = kps_ptr + i * 10;
            int index = 0;
            for (unsigned int j = 0; j < 10; j += 2)
            {
                FaceLandmark landmark;
                float kps_l = kps_offsets[j];
                float kps_t = kps_offsets[j + 1];
                float kps_x = ((cx + kps_l) * s - (float) dw) / ratio; // cx + l x
                float kps_y = ((cy + kps_t) * s - (float) dh) / ratio; // cy + t y
                landmark.p.x = std::min(std::max(0.f, kps_x), img_width - 1.f);
                landmark.p.y = std::min(std::max(0.f, kps_y), img_height - 1.f);
                landmark.i = index++; // TODO: find the corresponding index of the face.
                face_landmarks.push_back(landmark);
            }
            faces.push_back(Face(face_landmarks, face_box));
        }
        else
        {
            faces.push_back(Face(face_box));
        }

        count += 1; // limit boxes for nms.
        if (count > max_nms)
        {
            break;
        }
    }

    if (faces.size() > nms_pre_)
    {
        std::sort(faces.begin(), faces.end(),
                  [](const Face& a, const Face& b) { return a.getBoundingBox().score > b.getBoundingBox().score; }); // sort inplace
        // truncate data after sorting.
        faces.resize(nms_pre_);
    }
    // TODO: maybe is not optimal to return a copy.
    return faces;
}
