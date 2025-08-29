#include "LinuxFace/onnx/scrfd.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
using namespace linuxface;

SCRFDetector::SCRFDetector(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath), feat_stride_fpn_{8, 16, 32}
{
    const int numOutputs = output_node_names_str_.size();
    using_kps_ = numOutputs == 9;
    if (!using_kps_ && numOutputs != 6)
    {
        common::logError("SCRFDetector only support 6 or 9 outputs");
        ready_ = false;
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

    const int targetHeight = static_cast<int>(input_node_dims.at(2));
    const int targetWidth = static_cast<int>(input_node_dims.at(3));
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    auto* tensorData = inputTensor.GetTensorMutableData<float>();
    auto tensorPadding = TensorPadding::scrfd();
    image->toTensor(tensorData, tensorPadding, targetWidth, targetHeight, NormalizationType::MINMAX);
    return inputTensor;
}

// TODO(arroyo): average the face location so we dont have that much jitter in
// face bounding box.
std::vector<Face> SCRFDetector::detect(const std::unique_ptr<Image>& image)
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
    const Ort::Value inputTensor = this->transform(image);

    // Check if transform succeeded
    if (!inputTensor.IsTensor())
    {
        common::logError("SCRFDetector::detect: Failed to create input tensor");
        Profiler::getInstance().stop("SCRFD", "Face detection");
        return {};
    }
    try
    {
        auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                    output_node_names_.data(), output_node_names_str_.size());

        Profiler::getInstance().stop("SCRFD", "Face detection");
        Profiler::getInstance().start("SCRFD", "Result processing");
        if (outputTensors.size() < 6)
        {
            common::logError("SCRFDetector::detect: outputTensors.size() = %d", outputTensors.size());
            return faces;
        }
        // score8, score16, score32, bbox8, bbox16, bbox32
        Ort::Value& score8 = outputTensors.at(0);  // e.g [1,12800,1]
        Ort::Value& score16 = outputTensors.at(1); // e.g [1,3200,1]
        Ort::Value& score32 = outputTensors.at(2); // e.g [1,800,1]
        Ort::Value& bbox8 = outputTensors.at(3);   // e.g [1,12800,4]
        Ort::Value& bbox16 = outputTensors.at(4);  // e.g [1,3200,4]
        Ort::Value& bbox32 = outputTensors.at(5);  // e.g [1,800,4]
        Ort::Value kps8;
        Ort::Value kps16;
        Ort::Value kps32;

        if (using_kps_)
        {
            if (outputTensors.size() != 9)
            {
                common::logError("SCRFDetector::detect: outputTensors.size() != 9");
                return faces;
            }
            kps8 = std::move(outputTensors.at(6));  // e.g [1,12800,10]
            kps16 = std::move(outputTensors.at(7)); // e.g [1,3200,10]
            kps32 = std::move(outputTensors.at(8)); // e.g [1,800,10]
        }

        // level 8 & 16 & 32 with kps
        generateBboxesKpsSingleStride(score8, bbox8, kps8, 8, ScoreThreshold, image->info.width, image->info.height,
                                      faces);
        generateBboxesKpsSingleStride(score16, bbox16, kps16, 16, ScoreThreshold, image->info.width, image->info.height,
                                      faces);
        generateBboxesKpsSingleStride(score32, bbox32, kps32, 32, ScoreThreshold, image->info.width, image->info.height,
                                      faces);

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

void SCRFDetector::applyNMS(std::vector<Face>& faces) const
{
    if (faces.empty())
    {
        return;
    }

    // Global sort of all faces from all strides
    std::sort(faces.begin(), faces.end(),
              [](const Face& a, const Face& b) { return a.getBoundingBox().score > b.getBoundingBox().score; });

    const size_t facesSize = faces.size();

    std::vector<bool> suppressed(facesSize, false);
    size_t writeIdx = 0;

    for (size_t i = 0; i < facesSize; ++i)
    {
        if (suppressed[i])
        {
            continue;
        }

        // Keep this face by moving it to the write position
        if (writeIdx != i)
        {
            faces[writeIdx] = faces[i];
        }

        const auto& currentRect = faces[writeIdx].getBoundingBox().rect;

        // Early termination if confidence too low
        if (faces[writeIdx].getBoundingBox().score < 0.02f)
        {
            break;
        }
        writeIdx++;

        // Check overlap with remaining faces
        for (size_t j = i + 1; j < facesSize; ++j)
        {
            if (suppressed[j])
            {
                continue;
            }

            const auto& otherRect = faces[j].getBoundingBox().rect;
            const float iou = math_utils::calculateIoU(currentRect, otherRect);

            if (iou > NmsThreshold)
            {
                suppressed[j] = true;
            }
        }
    }

    // Keep only the non-suppressed faces
    faces.resize(writeIdx);
}

void SCRFDetector::generatePoints()
{
    const auto targetHeight = static_cast<float>(input_node_dims.at(2)); // e.g 640
    const auto targetWidth = static_cast<float>(input_node_dims.at(3));  // e.g 640

    // 8, 16, 32
    for (auto stride : feat_stride_fpn_)
    {
        const unsigned int numGridW = targetWidth / stride;
        const unsigned int numGridH = targetHeight / stride;
        // y
        for (unsigned int i = 0; i < numGridH; ++i)
        {
            // x
            for (unsigned int j = 0; j < numGridW; ++j)
            {
                // num_anchors, col major
                for (unsigned int k = 0; k < NumAnchors; ++k)
                {
                    center_points_[stride].emplace_back(static_cast<float>(j), static_cast<float>(i),
                                                        static_cast<float>(stride));
                }
            }
        }
    }
}

void SCRFDetector::generateBboxesKpsSingleStride(Ort::Value& scorePred, Ort::Value& bboxPred, Ort::Value& kpsPred,
                                                 unsigned int stride, float scoreThreshold, float imgWidth,
                                                 float imgHeight, std::vector<Face>& faces)
{
    // generate center points.
    const auto newHeight = static_cast<float>(input_node_dims.at(2)); // e.g 640
    const auto newWidth = static_cast<float>(input_node_dims.at(3));  // e.g 640

    const float ratio = std::min(static_cast<float>(newWidth) / imgWidth, static_cast<float>(newHeight) / imgHeight);

    const int resizedW = static_cast<int>(imgWidth * ratio);
    const int resizedH = static_cast<int>(imgHeight * ratio);

    const int dw = (newWidth - resizedW) / 2;
    const int dh = (newHeight - resizedH) / 2;

    auto strideDims = scorePred.GetTypeInfo().GetTensorTypeAndShapeInfo().GetShape();
    const unsigned int numPoints = strideDims.at(1);                 // 12800
    const float* scorePtr = scorePred.GetTensorMutableData<float>(); // [1,12800,1]
    const float* bboxPtr = bboxPred.GetTensorMutableData<float>();   // [1,12800,4]
    const float* kpsPtr = nullptr;

    if (using_kps_)
    {
        kpsPtr = kpsPred.GetTensorMutableData<float>(); // [1,12800,10]
    }

    // Pre-calculate constants outside the loop
    const float invRatio = 1.0f / ratio;
    const auto dwF = static_cast<float>(dw);
    const auto dhF = static_cast<float>(dh);
    const float imgWidthMinus1 = imgWidth - 1.0f;
    const float imgHeightMinus1 = imgHeight - 1.0f;

    // Use a simpler container - just collect Face objects directly
    std::vector<Face> strideFaces;
    strideFaces.reserve(1000); // Based on max_faces_per_stride

    auto& stridePoints = center_points_[stride];
    for (unsigned int i = 0; i < numPoints; ++i)
    {
        const float clsConf = scorePtr[i];
        if (clsConf < scoreThreshold)
        {
            continue;
        }

        const auto& point = stridePoints[i]; // Remove reference to avoid indirection
        const float cx = point.cx;
        const float cy = point.cy;
        const float s = point.stride;

        // Optimized bbox calculation with pre-calculated constants
        const float* offsets = bboxPtr + (i << 2);
        const float l = offsets[0];
        const float t = offsets[1];
        const float r = offsets[2];
        const float b = offsets[3];

        // Inline coordinate transformation
        float x1 = ((cx - l) * s - dwF) * invRatio;
        float y1 = ((cy - t) * s - dhF) * invRatio;
        float x2 = ((cx + r) * s - dwF) * invRatio;
        float y2 = ((cy + b) * s - dhF) * invRatio;

        // Clamp coordinates
        x1 = std::max(0.0f, std::min(x1, imgWidthMinus1));
        y1 = std::max(0.0f, std::min(y1, imgHeightMinus1));
        x2 = std::max(0.0f, std::min(x2, imgWidthMinus1));
        y2 = std::max(0.0f, std::min(y2, imgHeightMinus1));

        FaceBoundingBox faceBox(x1, y1, x2, y2);
        if (!faceBox.rect.isWithinBounds(imgWidth, imgHeight, 1.2f))
        {
            continue;
        }
        faceBox.score = clsConf;

        if (using_kps_)
        {
            std::vector<FaceLandmark> faceLandmarks;
            faceLandmarks.reserve(5);                    // Exactly 5 landmarks for SCRFD
            const float* kpsOffsets = kpsPtr + (i * 10); // 5 landmarks * 2 coords

            for (unsigned int j = 0; j < 10; j += 2)
            {
                const float kpsL = kpsOffsets[j];
                const float kpsT = kpsOffsets[j + 1];
                const float kpsX = std::max(0.0f, std::min(((cx + kpsL) * s - dwF) * invRatio, imgWidthMinus1));
                const float kpsY = std::max(0.0f, std::min(((cy + kpsT) * s - dhF) * invRatio, imgHeightMinus1));
                faceLandmarks.emplace_back(FaceLandmark{j >> 1, math_utils::Point3D(kpsX, kpsY, 0.0)});
            }
            strideFaces.emplace_back(std::move(faceLandmarks), faceBox);
        }
        else
        {
            strideFaces.emplace_back(faceBox);
        }

        if (strideFaces.size() >= MaxNumberOfFaces) // Hard limit
        {
            break;
        }
    }

    // Sort by score (descending)
    std::sort(strideFaces.begin(), strideFaces.end(), [](const Face& a, const Face& b) noexcept
              { return a.getBoundingBox().score > b.getBoundingBox().score; });

    // Apply stride limit and move to main vector
    const size_t strideLimit = std::min(static_cast<size_t>(MaxFacesPerStride), strideFaces.size());
    faces.reserve(faces.size() + strideLimit); // Reserve exactly what we need

    for (size_t i = 0; i < strideLimit; ++i)
    {
        faces.push_back(std::move(strideFaces[i]));
    }
}
