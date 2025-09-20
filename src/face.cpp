#include "LinuxFace/face.h"

#include <cmath>
#include <utility>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/scrfd.h"
using namespace linuxface;

Face::Face(std::vector<FaceLandmark> landmarks, FaceBoundingBox boundingBox)
    : boundingBox_(boundingBox), pose_{0.0f, 0.0f, 0.0f}, valid_(true)
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

Face::Face(FaceBoundingBox boundingBox) : boundingBox_(boundingBox), pose_{0.0f, 0.0f, 0.0f}, valid_(true)
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

void Face::loadNewFaceLandmarks(const std::vector<FaceLandmark>& landmarks)
{
    freeFaceLandmarks();

    // Fill the landmarks map with the new landmarks.
    for (const FaceLandmark& landmark : landmarks)
    {
        const unsigned int id = landmark.i;
        const Face::FaceIndex index = Face::getFacepartFromLandmarkId(id);
        auto it = landmarks_.find(index);
        if (it == landmarks_.end())
        {
            std::vector<FaceLandmark> newLandmark;
            newLandmark.push_back(landmark);
            landmarks_.insert(std::pair<Face::FaceIndex, std::vector<FaceLandmark>>(index, newLandmark));
        }
        else
        {
            landmarks_[index].push_back(landmark);
        }
    }
}

Face::FaceIndex Face::getFacepartFromLandmarkId(unsigned long id)
{
    // Lookup table for landmark -> facepart translation
    if (id >= 0 && id <= 16)
    {
        // Jaw
        return JAW;
    }
    if (id >= 17 && id <= 21)
    {
        // Left eyebrow

        return LBROW;
    }
    if (id >= 22 && id <= 26)
    {
        // Right eyebrow
        return RBROW;
    }
    if (id >= 27 && id <= 35)
    {
        // Nose
        return NOSE;
    }
    if (id >= 36 && id <= 41)
    {
        // Left eye
        return LEYE;
    }
    if (id >= 42 && id <= 47)
    {
        // Right eye
        return REYE;
    }
    if (id >= 48 && id <= 59)
    {
        // Outer mouth
        return OUTERMOUTH;
    }
    if (id >= 60 && id <= 67)
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
        common::logWarn("Face::paintAllFaceLandmarks: No landmarks to paint.");
        return; // No landmarks to paint
    }

    for (const auto& facePart : landmarks_)
    {
        if (facePart.first == FaceIndex::SILHOUETTE)
        {
            c = Pixel(0, 255, 0);
        }
        paintFaceIndex(image, facePart.first, joinPoints, c, radius);
    }
}

void Face::paintBoundingBox(std::unique_ptr<Image>& image, Pixel color) const
{
    std::vector<math_utils::Point<>> points;

    // Clamp bounding box coordinates to image bounds
    const int imageWidth = static_cast<int>(image->info.width);
    const int imageHeight = static_cast<int>(image->info.height);

    const int left = std::max(0, std::min(static_cast<int>(boundingBox_.rect.l), imageWidth - 1));
    const int right = std::max(0, std::min(static_cast<int>(boundingBox_.rect.r), imageWidth - 1));
    const int top = std::max(0, std::min(static_cast<int>(boundingBox_.rect.t), imageHeight - 1));
    const int bottom = std::max(0, std::min(static_cast<int>(boundingBox_.rect.b), imageHeight - 1));

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

    const long startPixel =
        static_cast<long>((boundingBox_.rect.l + boundingBox_.rect.t * static_cast<float>(image->info.width))
                          * static_cast<float>(image->info.pixelSizeBytes));

    // El 25 es per corregir el bottom massa curt. Aixi no talla la cara,
    const long endPixel = static_cast<long>(boundingBox_.rect.r + boundingBox_.rect.b + 25.0f)
                          * static_cast<long>(image->info.width) * static_cast<long>(image->info.pixelSizeBytes);

    for (long i = startPixel; i < endPixel; i += image->info.pixelSizeBytes)
    {
        for (const FaceLandmark& landmark : landmarks_.at(facepart))
        {
            const math_utils::Point3D& p = landmark.p;
            const long index = static_cast<long>(p.x + p.y * static_cast<double>(image->info.width))
                               * static_cast<long>(image->info.pixelSizeBytes);
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
    const std::vector<FaceLandmark> points = landmarks_.at(facepart);
    math_utils::Point3D lastPoint(-1, -1, -1);
    for (const FaceLandmark& l : points)
    {
        if (joinPoints)
        {
            if (lastPoint.x != -1 && lastPoint.y != -1)
            {
                // We know a last point. Proceed with DDA and paint it
                const std::vector<math_utils::Point<>> points =
                    math_utils::DDA(static_cast<double>(lastPoint.x), static_cast<double>(lastPoint.y), l.p.x, l.p.y);
                image->paintPoints(points, color);
            }
            lastPoint = l.p;
        }
        else
        {
            if (radius <= 1.0f)
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
// TODO(arroyo): use thickness
void Face::paintPoseAxis(std::unique_ptr<Image>& image, float size, float /*thickness*/) const
{
    // Convert to radians
    const auto pitch = static_cast<float>(pose_.pitch * M_PI / 180.0);
    const auto yaw = static_cast<float>(-pose_.yaw * M_PI / 180.0);
    const auto roll = static_cast<float>(pose_.roll * M_PI / 180.0);

    int tdx;
    int tdy;

    if ((landmarks_.count(NOSE) != 0u) && !landmarks_.at(NOSE).empty())
    {
        const auto nose_point = landmarks_.at(NOSE)[0].p;
        tdx = static_cast<int>(nose_point.x);
        tdy = static_cast<int>(nose_point.y);
    } 
    else
    {
        // Since Nose landmark is missing, use the center of the bounding box
        tdx = static_cast<int>(boundingBox_.rect.x() + boundingBox_.rect.width() / 2.0f);
        tdy = static_cast<int>(boundingBox_.rect.y() + boundingBox_.rect.height() / 2.0f);
    }

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

    const auto xColor = Pixel(0, 0, 255);
    const auto yColor = Pixel(0, 255, 0);
    const auto zColor = Pixel(255, 0, 0);

    auto x = math_utils::DDA(tdx, tdy, x1, y1);
    image->paintPoints(x, xColor);
    auto y = math_utils::DDA(tdx, tdy, x2, y2);
    image->paintPoints(y, yColor);
    auto z = math_utils::DDA(tdx, tdy, x3, y3);
    image->paintPoints(z, zColor);
}

// Retrieve 5-point landmarks in ArcFace order (3D)
std::vector<linuxface::math_utils::Point3D> Face::getFivePointLandmarksArcFaceOrder() const
{
    std::vector<math_utils::Point3D> result(5);
    if ((landmarks_.count(LEYE) != 0u) && !landmarks_.at(LEYE).empty())
    {
        result[0] = landmarks_.at(LEYE)[0].p;
    }
    if ((landmarks_.count(REYE) != 0u) && !landmarks_.at(REYE).empty())
    {
        result[1] = landmarks_.at(REYE)[0].p;
    }
    if ((landmarks_.count(NOSE) != 0u) && !landmarks_.at(NOSE).empty())
    {
        result[2] = landmarks_.at(NOSE)[0].p;
    }
    if ((landmarks_.count(OUTERMOUTH) != 0u) && landmarks_.at(OUTERMOUTH).size() >= 2)
    {
        result[3] = landmarks_.at(OUTERMOUTH)[0].p;
        result[4] = landmarks_.at(OUTERMOUTH)[1].p;
    }
    return result;
}

// Retrieve 5-point landmarks in ArcFace order (2D)
std::vector<linuxface::math_utils::Point<>> Face::getFivePointLandmarksArcFaceOrder2D() const
{
    std::vector<math_utils::Point<>> result;
    auto pts3d = getFivePointLandmarksArcFaceOrder();
    result.reserve(pts3d.size());

    for (const auto& pt : pts3d)
    {
        const long rx = std::lround(pt.x);
        const long ry = std::lround(pt.y);
        result.emplace_back(rx, ry);
    }
    return result;
}

linuxface::math_utils::Point3D Face::getLandmarkByIndex(unsigned int id) const
{
    for (const auto& facePart : landmarks_)
    {
        for (const auto& landmark : facePart.second)
        {
            if (landmark.i == id)
            {
                return landmark.p;
            }
        }
    }
    return {-1, -1, -1};
}

Face::FaceMatchResult
Face::findBestMatchingFace(std::vector<Face>& detectedFaces, const math_utils::Rect<double>& groundTruthBbox,
                           double minIouThreshold)
{
    FaceMatchResult result;

    if (detectedFaces.empty())
    {
        return result; // No faces to match
    }

    double bestIou = 0.0;
    int bestIndex = -1;
    Face* bestFace = nullptr;

    for (size_t i = 0; i < detectedFaces.size(); ++i)
    {
        // Get the detected face's bounding box
        auto detectedBboxRect = detectedFaces[i].getBoundingBox().rect;

        // Convert to double precision for IoU calculation
        const math_utils::Rect<double> detectedBboxDouble(
            static_cast<double>(detectedBboxRect.l), static_cast<double>(detectedBboxRect.t),
            static_cast<double>(detectedBboxRect.r), static_cast<double>(detectedBboxRect.b));

        // Calculate IoU between ground truth and detected face
        const double iou = math_utils::calculateIoU(groundTruthBbox, detectedBboxDouble);

        // Track the best match
        if (iou > bestIou)
        {
            bestIou = iou;
            bestIndex = static_cast<int>(i);
            bestFace = &detectedFaces[i];
        }
    }

    // Set result based on whether we found a match above threshold
    result.iou_score = bestIou;
    if (bestFace != nullptr && bestIou >= minIouThreshold)
    {
        result.found_match = true;
        result.best_face = bestFace;
        result.face_index = bestIndex;
    }

    return result;
}
