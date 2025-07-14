#include "LinuxFace/onnx/pfld.h"

#include "LinuxFace/common.h"
#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

PFLDDetector::PFLDDetector(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    // Model should have 1 output: (1, 212) for 106 landmarks (x, y)
    if (output_node_names_str_.empty() || input_node_dims.size() < 4)
    {
        ready_ = false;
        common::log_error("PFLDDetector: Invalid model or input dims");
        common::log_error("Problem is %s, input dims: %s",
                          output_node_names_str_.empty() ? "no outputs" : "invalid outputs",
                          input_node_dims.empty() ? "empty" : "not empty");
    }
}

Ort::Value PFLDDetector::transform(const std::unique_ptr<Image>& image)
{
    int target_height = static_cast<int>(input_node_dims.at(2));
    int target_width = static_cast<int>(input_node_dims.at(3));
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    // Use a member TensorPadding to store the transform for landmark mapping
    pfld_padding_ = TensorPadding::scrfd();
    image->toTensor(tensor_data, pfld_padding_, target_width, target_height, NormalizationType::MINMAX);
    auto test = image_utils::convertToRawImage<NormalizationType::MINMAX>(tensor_data, target_width, target_height);
    if(test)
    {
        if(!test->saveToDisk("pfld_input_tensor.ppm"))
        {
            common::log_info("PFLDDetector: Not Saved test image to disk.");
        }
    }
    return input_tensor;
}

void PFLDDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("PFLDDetector", "detect landmarks");
    // 1. Get 5-point landmarks in ArcFace order (left eye, right eye, nose, left mouth, right mouth)
    std::vector<math_utils::Point> five_pts = face.getFivePointLandmarksArcFaceOrder2D();
    if (five_pts.size() != 5) {
        common::log_error("PFLDDetector: Need 5-point landmarks for alignment");
        return;
    }

    // 2. Align face using affine transform to canonical template (reuse ArcFace/SwapPipeline logic)
    constexpr int pfld_input_size = 112; // PFLD expects 112x112 input
    // Use template_112 from image_utils (should be normalized [0,1] coordinates)
    std::vector<math_utils::Point> template_pts(5);
    for (int i = 0; i < 5; ++i) {
        template_pts[i].x = image_utils::template_112[i][0] * pfld_input_size;
        template_pts[i].y = image_utils::template_112[i][1] * pfld_input_size;
    }
    double src_points[10], dst_points[10];
    for (int i = 0; i < 5; ++i) {
        src_points[2 * i] = five_pts[i].x;
        src_points[2 * i + 1] = five_pts[i].y;
        dst_points[2 * i] = template_pts[i].x;
        dst_points[2 * i + 1] = template_pts[i].y;
    }
    double affineM[6];
    if (!math_utils::estimate_affine_2d(src_points, dst_points, 5, affineM)) {
        common::log_error("PFLDDetector: Failed to estimate affine transform for alignment");
        return;
    }
    // 3. Warp the face region to canonical pose
    std::unique_ptr<Image> aligned_face = image->affineWarpBilinear(affineM, pfld_input_size, pfld_input_size);
    if (!aligned_face) {
        common::log_error("PFLDDetector: Failed to warp face for alignment");
        return;
    }

    // 4. Run PFLD on aligned face
    Ort::Value input_tensor = this->transform(aligned_face);
    auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor, 1,
                                                 output_node_names_.data(), output_node_names_str_.size());
    if (output_tensors.empty())
    {
        common::log_error("PFLDDetector: No output tensors received");   
        return;
    }
    // Find the correct output tensor (should be named "output" and have shape (1, 212))
    int output_index = 0;
    for (size_t i = 0; i < output_node_names_str_.size(); ++i) {
        if (output_node_names_str_[i] == "output") {
            output_index = static_cast<int>(i);
            break;
        }
    }
    Ort::Value& landmarks_tensor = output_tensors.at(output_index); // (1, 212)
    const float* data = landmarks_tensor.GetTensorData<float>();
    unsigned int num_landmarks = 106;
    face.loadNewFaceLandmarks({});

    // 5. Map landmarks from aligned face back to original image using inverse affine
    double inv_affineM[6];
    if (!math_utils::invert_affine(affineM, inv_affineM)) {
        common::log_error("PFLDDetector: Failed to invert affine for landmark mapping");
        return;
    }
    std::vector<math_utils::Point> aligned_pts(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        float x = std::min(std::max(0.f, data[2 * i]), 1.0f) * pfld_input_size;
        float y = std::min(std::max(0.f, data[2 * i + 1]), 1.0f) * pfld_input_size;
        aligned_pts[i] = math_utils::Point(x, y);
    }
    std::vector<math_utils::Point> mapped_pts = image_utils::transform_points_affine(aligned_pts, inv_affineM);
    std::vector<FaceLandmark> pfld_landmarks;
    pfld_landmarks.reserve(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        // Convert Point to Point3D (z=0)
        pfld_landmarks.push_back(FaceLandmark{i, math_utils::Point3D(mapped_pts[i].x, mapped_pts[i].y, 0.0)});
    }
    face.loadNewFaceLandmarks(std::move(pfld_landmarks));
    Profiler::getInstance().stop("PFLDDetector", "detect landmarks");
}
