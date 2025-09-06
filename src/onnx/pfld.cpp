#include "LinuxFace/onnx/pfld.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

PFLDDetector::PFLDDetector(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
    // Model should have 1 output: (1, 212) for 106 landmarks (x, y)
    if (output_node_names_str_.empty() || input_node_dims.size() < 4)
    {
        ready_ = false;
        common::logError("PFLDDetector: Invalid model or input dims");
        common::logError("Problem is %s, input dims: %s",
                         output_node_names_str_.empty() ? "no outputs" : "invalid outputs",
                         input_node_dims.empty() ? "empty" : "not empty");
    }
}

Ort::Value PFLDDetector::transform(const std::unique_ptr<Image>& image)
{
    if (!image || image->info.width == 0 || image->info.height == 0 || !image->data())
    {
        common::logError("PFLDDetector::transform - Invalid input image");
        throw std::runtime_error("Invalid input image for PFLD transform");
    }

    const int targetHeight = static_cast<int>(input_node_dims.at(2));
    const int targetWidth = static_cast<int>(input_node_dims.at(3));

    if (targetHeight != 112 || targetWidth != 112)
    {
        common::logError("PFLDDetector::transform - Unexpected target dimensions: %dx%d", targetWidth, targetHeight);
        throw std::runtime_error("Unexpected target dimensions for PFLD model");
    }

    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    auto* tensorData = inputTensor.GetTensorMutableData<float>();

    if (!tensorData)
    {
        common::logError("PFLDDetector::transform - Failed to create tensor data");
        throw std::runtime_error("Failed to create tensor data");
    }

    // Convert image to tensor
    const image_utils::ImageView<unsigned char> srcView{image->data(), image->info.width, image->info.height,
                                                        image->info.pixelSizeBytes};
    image_utils::ImageView<float> dstView{tensorData, static_cast<size_t>(targetWidth),
                                          static_cast<size_t>(targetHeight), 3};
    image_utils::bicubicScaling<unsigned char, float, NormalizationType::MINMAX, ImageLayout::CHW>(srcView, dstView);

    return inputTensor;
}

void PFLDDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("PFLDDetector", "detect landmarks");

    auto leftEye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
    auto rightEye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);

    if (leftEye.x < 0 || leftEye.y < 0 || rightEye.x < 0 || rightEye.y < 0)
    {
        common::logError("PFLDDetector::detect - Missing or invalid eye landmarks for face alignment");
        return;
    }

    const math_utils::Point<double> eyeCenter = {(leftEye.x + rightEye.x) / 2.0, (leftEye.y + rightEye.y) / 2.0};

    const double dx = rightEye.x - leftEye.x;
    const double dy = rightEye.y - leftEye.y;
    const double angleRad = -std::atan2(dy, dx); // rotate to horizontal
    const double eyeDist = std::sqrt(dx * dx + dy * dy);

    // Use bounding box from SCRFD
    auto bbox = face.getBoundingBox().rect;
    const double bboxScaleFactor = 1.2; // 20% expansion
    const double baseBoxSize = eyeDist * 3.5;
    const double bboxBoxSize = std::max(bbox.width(), bbox.height()) * bboxScaleFactor;
    double boxSize = std::max(bboxBoxSize, baseBoxSize);

    // Store original image dimensions
    const unsigned long origWidth = image->info.width;
    const unsigned long origHeight = image->info.height;
    const double maxBoxSize = std::min(origWidth, origHeight) * 0.9;
    boxSize = std::min(boxSize, maxBoxSize);

    auto alignedFace = image->deepCopy();

    // Rotate the whole image
    alignedFace->rotate(angleRad, eyeCenter);

    // Calculate where the eye center should be in the rotated image
    // We need to simulate the same transformation that the rotation function does
    const double cosA = std::cos(angleRad);
    const double sinA = std::sin(angleRad);

    // Calculate the corners of the original image relative to the eye center
    const double corners[4][2] = {
        {-eyeCenter.x,                -eyeCenter.y                }, // top-left
        {origWidth - 1 - eyeCenter.x, -eyeCenter.y                }, // top-right
        {-eyeCenter.x,                origHeight - 1 - eyeCenter.y}, // bottom-left
        {origWidth - 1 - eyeCenter.x, origHeight - 1 - eyeCenter.y}  // bottom-right
    };

    // Find the bounding box of rotated corners
    double minX = 1e9;
    double minY = 1e9;
    double maxX = -1e9;
    double maxY = -1e9;
    for (const auto& corner : corners)
    {
        const double x = corner[0] * cosA - corner[1] * sinA;
        const double y = corner[0] * sinA + corner[1] * cosA;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }


    // 4. Compute crop center
    const math_utils::Point<double> faceCenterOriginal = {bbox.l + bbox.width() / 2.0, bbox.t + bbox.height() / 2.0};
    const double faceDx = faceCenterOriginal.x - eyeCenter.x;
    const double faceDy = faceCenterOriginal.y - eyeCenter.y;
    const math_utils::Point<double> finalFaceCenter = {cosA * faceDx - sinA * faceDy - minX,
                                                       sinA * faceDx + cosA * faceDy - minY};

    // 5. Crop (reverted to original logic)
    const double halfBox = boxSize / 2.0;
    const double cropLeft = finalFaceCenter.x - halfBox;
    const double cropTop = finalFaceCenter.y - halfBox;
    const math_utils::Rect<float> cropRect = {
        {static_cast<float>(cropLeft), static_cast<float>(cropTop)},
        static_cast<float>(boxSize),
        static_cast<float>(boxSize)
    };
    alignedFace = alignedFace->crop(cropRect);
    if (!alignedFace)
    {
        common::logError("Failed to crop aligned face image for PFLD landmarks detection");
        return;
    }

    // 4. Run PFLD on aligned face
    const Ort::Value inputTensor = this->transform(alignedFace);

    auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                output_node_names_.data(), output_node_names_str_.size());
    if (outputTensors.empty())
    {
        common::logError("PFLDDetector: No output tensors received");
        return;
    }
    // Find the correct output tensor (should be named "output" and have shape (1, 212))
    int outputIndex = 0;
    for (size_t i = 0; i < output_node_names_str_.size(); ++i)
    {
        if (output_node_names_str_[i] == "output")
        {
            outputIndex = static_cast<int>(i);
            break;
        }
    }
    const Ort::Value& landmarksTensor = outputTensors.at(outputIndex); // (1, 212)
    const auto* data = landmarksTensor.GetTensorData<float>();

    if (!data)
    {
        common::logError("PFLDDetector: No tensor data received");
        return;
    }

    // Verify tensor shape
    const auto& tensorShape = landmarksTensor.GetTensorTypeAndShapeInfo().GetShape();
    if (tensorShape.size() != 2 || tensorShape[0] != 1 || tensorShape[1] != 212)
    {
        common::logError("PFLDDetector: Unexpected tensor shape: [%d, %d]", tensorShape[0], tensorShape[1]);
        return;
    }

    const unsigned int numLandmarks = 106;
    face.loadNewFaceLandmarks({});

    const double scale = 112.0 / boxSize;
    // Preserve floating-point precision for landmark coordinates
    std::vector<math_utils::Point<double>> alignedPts(numLandmarks);
    for (unsigned int i = 0; i < numLandmarks; ++i)
    {
        const unsigned int index = 2 * i;
        if (index + 1 >= 212)
        {
            common::logError("PFLDDetector: Landmark index out of bounds: %u", index);
            return;
        }
        const double x = data[index] * 112.0;
        const double y = data[index + 1] * 112.0;
        auto pt = alignedToOriginalCoords(x, y, cropLeft, cropTop, minX, minY, angleRad, eyeCenter, scale);
        alignedPts[i].x = pt.x;
        alignedPts[i].y = pt.y;
    }

    std::vector<FaceLandmark> pfldLandmarks;
    pfldLandmarks.reserve(numLandmarks);
    for (unsigned int i = 0; i < numLandmarks; ++i)
    {
        // Convert Point to Point3D (z=0)
        pfldLandmarks.push_back(FaceLandmark{i, math_utils::Point3D(alignedPts[i].x, alignedPts[i].y, 0.0)});
    }
    face.loadNewFaceLandmarks(pfldLandmarks);
    Profiler::getInstance().stop("PFLDDetector", "detect landmarks");
}


void PFLDDetector::detectSimilar(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("PFLDDetector", "detect landmarks");
    // 1. Get 5-point landmarks in ArcFace order (left eye, right eye, nose, left mouth, right mouth)
    const std::vector<math_utils::Point<>> fivePts2d = face.getFivePointLandmarksArcFaceOrder2D();
    if (fivePts2d.size() != 5)
    {
        common::logError("PFLDDetector: Need 5-point landmarks for alignment");
        return;
    }
    auto [alignedFace, affine] =
        image_utils::similarityFaceTransform(*image, fivePts2d, image_utils::TEMPLATE_192_ALT, 192, true);

    if (!alignedFace)
    {
        common::logError("PFLDDetector: Failed to warp face for alignment");
        return;
    }
    // alignedFace->saveToDisk("aligned_face_similar.ppm");
    // 4. Run PFLD on aligned face
    const Ort::Value inputTensor = this->transform(alignedFace);

    auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                output_node_names_.data(), output_node_names_str_.size());
    if (outputTensors.empty())
    {
        common::logError("PFLDDetector: No output tensors received");
        return;
    }
    // Find the correct output tensor (should be named "output" and have shape (1, 212))
    int outputIndex = 0;
    for (size_t i = 0; i < output_node_names_str_.size(); ++i)
    {
        if (output_node_names_str_[i] == "output")
        {
            outputIndex = static_cast<int>(i);
            break;
        }
    }
    const Ort::Value& landmarksTensor = outputTensors.at(outputIndex); // (1, 212)
    const auto* data = landmarksTensor.GetTensorData<float>();
    const unsigned int numLandmarks = 106;
    face.loadNewFaceLandmarks({});

    // // 5. Map landmarks from aligned face back to original image using inverse affine
    double invAffineM[6];
    if (!math_utils::invertAffine(affine.data(), invAffineM))
    {
        common::logError("PFLDDetector: Failed to invert affine for landmark mapping");
        return;
    }

    const double w = alignedFace->info.width;
    const double h = alignedFace->info.height;

    common::logInfo("PFLDDetector: Aligned face size: %fx%f", w, h);
    std::vector<std::pair<double, double>> alignedPts(numLandmarks);
    for (unsigned int i = 0; i < numLandmarks; ++i)
    {
        const double x = std::min(std::max(0.f, data[2 * i]), 1.0f);
        const double y = std::min(std::max(0.f, data[2 * i + 1]), 1.0f);
        alignedPts[i].first = x * w;
        alignedPts[i].second = y * h;
    }

    auto unalignedPts = image_utils::transformPointsAffine(alignedPts, invAffineM);
    std::vector<FaceLandmark> pfldLandmarks;
    pfldLandmarks.reserve(numLandmarks);
    for (unsigned int i = 0; i < numLandmarks; ++i)
    {
        // Convert Point to Point3D (z=0)
        pfldLandmarks.push_back(
            FaceLandmark{i, math_utils::Point3D(unalignedPts[i].first, unalignedPts[i].second, 0.0)});
    }
    face.loadNewFaceLandmarks(pfldLandmarks);
    Profiler::getInstance().stop("PFLDDetector", "detect landmarks");
}

math_utils::Point<double>
PFLDDetector::alignedToOriginalCoords(double xAligned, double yAligned, double cropLeft, double cropTop, double minX,
                                      double minY, double angleRad, const math_utils::Point<double>& eyeCenter,
                                      double scale)
{
    // Step 0: undo scaling (map from 112x112 back to cropped box size)
    const double xUnscaled = xAligned / scale;
    const double yUnscaled = yAligned / scale;

    // Step 1: undo crop
    const double xRotated = xUnscaled + cropLeft;
    const double yRotated = yUnscaled + cropTop;

    // Step 2: get absolute rotated coordinates
    const double xRel = xRotated + minX;
    const double yRel = yRotated + minY;

    // Step 3: un-rotate around eye center
    const double cosA = std::cos(-angleRad);
    const double sinA = std::sin(-angleRad);

    const double xOrig = cosA * xRel - sinA * yRel + eyeCenter.x;
    const double yOrig = sinA * xRel + cosA * yRel + eyeCenter.y;

    return {xOrig, yOrig};
}
