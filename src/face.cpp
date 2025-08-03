#include "LinuxFace/face.h"

#include <utility>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/scrfd.h"
using namespace linuxface;

Face::Face(std::vector<FaceLandmark> landmarks, FaceBoundingBox boundingBox) : boundingBox_(boundingBox)
{
    if (landmarks.size() == 5)
    {
        // ArcFace order: [left eye, right eye, nose, left mouth, right mouth]
        // Assign 68-landmark indices for compatibility:
        // left eye: 36, right eye: 45, nose: 33, left mouth: 48, right mouth: 54
        landmarks_[LEYE].push_back(FaceLandmark{SCRFDetector::LandmarkIndex::LEYE, landmarks[0].p});
        landmarks_[REYE].push_back(FaceLandmark{SCRFDetector::LandmarkIndex::REYE, landmarks[1].p});
        landmarks_[NOSE].push_back(FaceLandmark{SCRFDetector::LandmarkIndex::NOSE, landmarks[2].p});
        landmarks_[OUTERMOUTH].push_back(
            FaceLandmark{SCRFDetector::LandmarkIndex::LMOUTH, landmarks[3].p}); // left mouth corner
        landmarks_[OUTERMOUTH].push_back(
            FaceLandmark{SCRFDetector::LandmarkIndex::RMOUTH, landmarks[4].p}); // right mouth corner
    }
    else
    {
        // Load 68 landmarks
        loadNewFaceLandmarks(landmarks);
    }
}

Face::Face(FaceBoundingBox boundingBox) : boundingBox_(boundingBox)
{
}

Face::~Face()
{
    freeFaceLandmarks();
}

void Face::freeFaceLandmarks()
{
    // Clear previous landmarks
    for (auto& landmark : landmarks_)
    {
        landmark.second.clear();
    }
    landmarks_.clear();
}

void Face::loadNewFaceLandmarks(std::vector<FaceLandmark> landmarks)
{
    freeFaceLandmarks();

    // Fill the landmarks map with the new landmarks.
    for (const FaceLandmark& landmark : landmarks)
    {
        unsigned int id = landmark.i;
        Face::FaceIndex index = this->get_facepart_from_landmark_id(id);
        auto it = landmarks_.find(index);
        if (it == landmarks_.end())
        {
            std::vector<FaceLandmark> new_landmark;
            new_landmark.push_back(landmark);
            landmarks_.insert(std::pair<Face::FaceIndex, std::vector<FaceLandmark>>(index, new_landmark));
        }
        else
        {
            landmarks_[index].push_back(landmark);
        }
    }
}

Face::FaceIndex Face::get_facepart_from_landmark_id(unsigned long id) const
{
    // Lookup table for landmark -> facepart translation
    if (id >= 0 && id <= 16)
    {
        // Jaw
        return JAW;
    }
    else if (id >= 17 && id <= 21)
    {
        // Left eyebrow

        return LBROW;
    }
    else if (id >= 22 && id <= 26)
    {
        // Right eyebrow
        return RBROW;
    }
    else if (id >= 27 && id <= 35)
    {
        // Nose
        return NOSE;
    }
    else if (id >= 36 && id <= 41)
    {
        // Left eye
        return LEYE;
    }
    else if (id >= 42 && id <= 47)
    {
        // Right eye
        return REYE;
    }
    else if (id >= 48 && id <= 59)
    {
        // Outer mouth
        return OUTERMOUTH;
    }
    else if (id >= 60 && id <= 67)
    {
        // Inner mouth
        return INNERMOUTH;
    }
    return UNKNOWN;
}

void Face::paintAllFaceLandmarks(std::unique_ptr<Image>& image, bool joinPoints, Pixel c, float radius) const
{
    if (landmarks_.empty())
    {
        common::log_warn("Face::paintAllFaceLandmarks: No landmarks to paint.");
        return; // No landmarks to paint
    }

    for (const auto& face_part : landmarks_)
    {
        if (face_part.first == FaceIndex::SILHOUETTE)
        {
            c = Pixel(0, 255, 0);
        }
        paintFaceIndex(image, face_part.first, joinPoints, c, radius);
    }
}

void Face::paintBoundingBox(std::unique_ptr<Image>& image, Pixel color) const
{
    std::vector<math_utils::Point<>> points;

    // Clamp bounding box coordinates to image bounds
    int imageWidth = static_cast<int>(image->info.width);
    int imageHeight = static_cast<int>(image->info.height);

    int left = std::max(0, std::min(static_cast<int>(boundingBox_.rect.l), imageWidth - 1));
    int right = std::max(0, std::min(static_cast<int>(boundingBox_.rect.r), imageWidth - 1));
    int top = std::max(0, std::min(static_cast<int>(boundingBox_.rect.t), imageHeight - 1));
    int bottom = std::max(0, std::min(static_cast<int>(boundingBox_.rect.b), imageHeight - 1));

    // Only draw if we have a valid rectangle
    if (left < right && top < bottom)
    {
        // Generate face rectangle points
        auto leftLine = math_utils::DDA(left, top, left, bottom);
        points.insert(points.end(), leftLine.begin(), leftLine.end());

        auto topLine = math_utils::DDA(left, top, right, top);
        points.insert(points.end(), topLine.begin(), topLine.end());

        auto bottomLine = math_utils::DDA(left, bottom, right, bottom);
        points.insert(points.end(), bottomLine.begin(), bottomLine.end());

        auto rightLine = math_utils::DDA(right, top, right, bottom);
        points.insert(points.end(), rightLine.begin(), rightLine.end());

        image->paintPoints(points, color);
    }
}

void Face::paintInside(std::unique_ptr<Image>& image, FaceIndex facepart) const
{
    int inside = 0;
    long startIndex = 0;

    long startPixel =
        static_cast<long>((boundingBox_.rect.l + boundingBox_.rect.t * static_cast<float>(image->info.width))
                          * static_cast<float>(image->info.pixelSizeBytes));

    // El 25 es per corregir el bottom massa curt. Aixi no talla la cara,
    long endPixel = (boundingBox_.rect.r + (boundingBox_.rect.b + 25.0f) * static_cast<float>(image->info.width))
                    * static_cast<float>(image->info.pixelSizeBytes);

    for (long i = startPixel; i < endPixel; i += image->info.pixelSizeBytes)
    {
        for (const FaceLandmark& landmark : landmarks_.at(facepart))
        {
            const math_utils::Point3D& p = landmark.p;
            long index = static_cast<long>((p.x + p.y * image->info.width) * image->info.pixelSizeBytes);
            if (index == i)
            {
                if (inside == 0)
                {
                    startIndex = i;
                }
                else
                {
                    for (long v = startIndex; v <= i; v += image->info.pixelSizeBytes)
                    {
                        image->pidx(v, 0, 0, 0);
                    }
                }

                // Ingora pixels seguits.
                if (startIndex != i - image->info.pixelSizeBytes)
                {
                    inside = 1 - inside;
                }
                else
                {
                    image->pidx(startIndex, 0, 0, 0);
                    startIndex = i;
                }
                break;
            }
        }

        if (i % (image->info.width * image->info.pixelSizeBytes) == 0)
        {
            // S'ha acabat la linia i estavem buscant un pixel parella.
            if (inside == 1)
            {
                image->pidx(startIndex, 0, 0, 0);
            }
            inside = 0;
        }
    }
}

void Face::paintFaceIndex(std::unique_ptr<Image>& image, FaceIndex facepart, bool joinPoints, Pixel color,
                          float radius) const
{
    std::vector<FaceLandmark> points = landmarks_.at(facepart);
    math_utils::Point3D lastPoint(-1, -1, -1);
    for (const FaceLandmark& l : points)
    {
        if (joinPoints)
        {
            if (lastPoint.x != -1 && lastPoint.y != -1)
            {
                // We know a last point. Proceed with DDA and paint it
                std::vector<math_utils::Point<>> points =
                    math_utils::DDA(static_cast<double>(lastPoint.x), static_cast<double>(lastPoint.y), l.p.x, l.p.y);
                image->paintPoints(points, color);
            }
            lastPoint = l.p;
        }
        else
        {
            if (radius < 1.0f)
            {
                image->ppx(l.p.x, l.p.y, color);
            }
            else
            {
                // Paint a circle around the landmark point
                image_utils::paintCircle(image, l.p, radius, color);
            }
        }
    }
}

void Face::paintPoseAxis(std::unique_ptr<Image>& image, float size, float thickness) const
{
    // Convert to radians
    const float pitch = pose_.pitch * M_PI / 180.f;
    const float yaw = -pose_.yaw * M_PI / 180.f;
    const float roll = pose_.roll * M_PI / 180.f;

    // TODO: Set tdx tdy to the center of the face.
    const int tdx = static_cast<int>(boundingBox_.rect.x() + boundingBox_.rect.width() / 2.0f);
    const int tdy = static_cast<int>(boundingBox_.rect.y() + boundingBox_.rect.height() / 2.0f);

    // X-Axis pointing to right. drawn in red
    const int x1 = static_cast<int>(size * std::cos(yaw) * std::cos(roll)) + tdx;
    const int y1 =
        static_cast<int>(size * (std::cos(pitch) * std::sin(roll) + std::cos(roll) * std::sin(pitch) * std::sin(yaw)))
        + tdy;

    // Y-Axis | drawn in green
    const int x2 = static_cast<int>(-size * std::cos(yaw) * std::sin(roll)) + tdx;
    const int y2 =
        static_cast<int>(size * (std::cos(pitch) * std::cos(roll) - std::sin(pitch) * std::sin(yaw) * std::sin(roll)))
        + tdy;

    // Z-Axis (out of the screen) drawn in blue
    const int x3 = static_cast<int>(size * std::sin(yaw)) + tdx;
    const int y3 = static_cast<int>(-size * std::cos(yaw) * std::sin(pitch)) + tdy;

    auto x_color = Pixel(0, 0, 255);
    auto y_color = Pixel(0, 255, 0);
    auto z_color = Pixel(255, 0, 0);
    
    auto X = math_utils::DDA(tdx, tdy, x1, y1);
    image->paintPoints(X, x_color);
    auto Y = math_utils::DDA(tdx, tdy, x2, y2);
    image->paintPoints(Y, y_color);
    auto Z = math_utils::DDA(tdx, tdy, x3, y3);
    image->paintPoints(Z, z_color);
}

// Retrieve 5-point landmarks in ArcFace order (3D)
std::vector<math_utils::Point3D> Face::getFivePointLandmarksArcFaceOrder() const
{
    std::vector<math_utils::Point3D> result(5);
    if (landmarks_.count(LEYE) && !landmarks_.at(LEYE).empty())
    {
        result[0] = landmarks_.at(LEYE)[0].p;
    }
    if (landmarks_.count(REYE) && !landmarks_.at(REYE).empty())
    {
        result[1] = landmarks_.at(REYE)[0].p;
    }
    if (landmarks_.count(NOSE) && !landmarks_.at(NOSE).empty())
    {
        result[2] = landmarks_.at(NOSE)[0].p;
    }
    if (landmarks_.count(OUTERMOUTH) && landmarks_.at(OUTERMOUTH).size() >= 2)
    {
        result[3] = landmarks_.at(OUTERMOUTH)[0].p;
        result[4] = landmarks_.at(OUTERMOUTH)[1].p;
    }
    return result;
}

// Retrieve 5-point landmarks in ArcFace order (2D)
std::vector<math_utils::Point<>> Face::getFivePointLandmarksArcFaceOrder2D() const
{
    std::vector<math_utils::Point<>> result;
    auto pts3d = getFivePointLandmarksArcFaceOrder();
    result.reserve(pts3d.size());
    for (const auto& pt : pts3d)
    {
        result.emplace_back(static_cast<long>(pt.x), static_cast<long>(pt.y));
    }
    return result;
}

math_utils::Point3D Face::getLandmarkByIndex(unsigned int id) const
{
    for (const auto& face_part : landmarks_)
    {
        for (const auto& landmark : face_part.second)
        {
            if (landmark.i == id)
            {
                return landmark.p;
            }
        }
    }
    return math_utils::Point3D(-1, -1, -1);
}
