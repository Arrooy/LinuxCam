#include "FunnyFace/face.h"

#include <utility>

#include "FunnyFace/math_utils.h"

using namespace funnyface;

Face::Face(std::vector<FaceLandmark> landmarks, FaceBoundingBox boundingBox) : boundingBox_(boundingBox)
{
    // Load landmarks
    loadNewFaceLandmarks(landmarks);
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

void Face::paintAllFaceLandmarks(std::unique_ptr<Image>& image, bool joinPoints) const
{
    for (auto const& face_part : landmarks_)
    {
        Pixel c(0, 255, 255);
        if (face_part.first == FaceIndex::SILHOUETTE)
        {
            c = Pixel(0, 255, 0);
        }
        paintFaceIndex(image, face_part.first, joinPoints, c);
    }
}

void Face::paintBoundingBox(std::unique_ptr<Image>& image) const
{
    std::vector<math_utils::Point> points;

    // Generate face rectangle points
    auto left = math_utils::DDA(boundingBox_.rect.l, boundingBox_.rect.t, boundingBox_.rect.l, boundingBox_.rect.b);
    points.insert(points.end(), left.begin(), left.end());

    auto top = math_utils::DDA(boundingBox_.rect.l, boundingBox_.rect.t, boundingBox_.rect.r, boundingBox_.rect.t);
    points.insert(points.end(), top.begin(), top.end());

    auto bottom = math_utils::DDA(boundingBox_.rect.l, boundingBox_.rect.b, boundingBox_.rect.r, boundingBox_.rect.b);
    points.insert(points.end(), bottom.begin(), bottom.end());

    auto right = math_utils::DDA(boundingBox_.rect.r, boundingBox_.rect.t, boundingBox_.rect.r, boundingBox_.rect.b);
    points.insert(points.end(), right.begin(), right.end());

    image->paintPoints(points, Pixel(0, 255, 0));
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
            const math_utils::Point& p = landmark.p;
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

void Face::paintFaceIndex(std::unique_ptr<Image>& image, FaceIndex facepart, bool joinPoints, Pixel color) const
{
    std::vector<FaceLandmark> points = landmarks_.at(facepart);
    math_utils::Point lastPoint(-1, -1);
    for (const FaceLandmark& l : points)
    {
        if (joinPoints)
        {
            if (lastPoint.x != -1 && lastPoint.y != -1)
            {
                // We know a last point. Proceed with DDA and paint it
                std::vector<math_utils::Point> points = math_utils::DDA(lastPoint.x, lastPoint.y, l.p.x, l.p.y);
                image->paintPoints(points, color);
            }
            lastPoint = l.p;
        }
        image->ppx(l.p.x, l.p.y, color);
    }
}

void Face::paintPoseAxis(std::unique_ptr<Image>& image, float size, float thickness) const
{
    // Convert to radians
    const float pitch = pose_.pitch * M_PI / 180.f;
    const float yaw = -pose_.yaw * M_PI / 180.f;
    const float roll = pose_.roll * M_PI / 180.f;

    // TODO: Set tdx tdy to the center of the face.
    const int tdx = 250;
    const int tdy = 250;

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


    auto X = math_utils::DDA(tdx, tdy, x1, y1);
    image->paintPoints(X, Pixel(0, 0, 255));
    auto Y = math_utils::DDA(tdx, tdy, x2, y2);
    image->paintPoints(Y, Pixel(0, 255, 0));
    auto Z = math_utils::DDA(tdx, tdy, x3, y3);
    image->paintPoints(Z, Pixel(255, 0, 0));
}
