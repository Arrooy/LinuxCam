#include "FunnyFace/face.h"

#include <utility>

#include "FunnyFace/math_utils.h"

using namespace funnyface;

Face::Face(std::vector<Landmark> landmarks, math_utils::Rect boundingBox) : boundingBox_(boundingBox)
{
    // Load landmarks
    loadNewLandmarks(landmarks);
}

Face::Face(math_utils::Rect boundingBox) : boundingBox_(boundingBox)
{
}

Face::~Face()
{
    freeLandmarks();
}

void Face::freeLandmarks()
{
    // Clear previous landmarks
    for (auto& landmark : landmarks_)
    {
        landmark.second.clear();
    }
    landmarks_.clear();
}

void Face::loadNewLandmarks(std::vector<Landmark> landmarks)
{
    freeLandmarks();

    // Fill the landmarks map with the new landmarks.
    for (const Landmark& landmark : landmarks)
    {
        unsigned int id = landmark.i;
        Face::FaceIndex index = this->get_facepart_from_landmark_id(id);
        auto it = landmarks_.find(index);
        if (it == landmarks_.end())
        {
            std::vector<Landmark> new_landmark;
            new_landmark.push_back(landmark);
            landmarks_.insert(std::pair<Face::FaceIndex, std::vector<Landmark>>(index, new_landmark));
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

void Face::paintAllLandmarks(std::unique_ptr<Image>& image, bool joinPoints)
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

void Face::paintBoundingBox(std::unique_ptr<Image>& image)
{
    std::vector<math_utils::Point> points;

    // Generate face rectangle points
    auto left = math_utils::DDA(boundingBox_.l, boundingBox_.t, boundingBox_.l, boundingBox_.b);
    points.insert(points.end(), left.begin(), left.end());

    auto top = math_utils::DDA(boundingBox_.l, boundingBox_.t, boundingBox_.r, boundingBox_.t);
    points.insert(points.end(), top.begin(), top.end());

    auto bottom = math_utils::DDA(boundingBox_.l, boundingBox_.b, boundingBox_.r, boundingBox_.b);
    points.insert(points.end(), bottom.begin(), bottom.end());

    auto right = math_utils::DDA(boundingBox_.r, boundingBox_.t, boundingBox_.r, boundingBox_.b);
    points.insert(points.end(), right.begin(), right.end());

    long width = image->info.width;
    long height = image->info.height;
    // Pant each point
    for (const math_utils::Point& p : points)
    {
        // Check image bounds
        if (p.x < 0 || p.x >= width || p.y < 0 || p.y >= height)
        {
            continue;
        }
        image->pxy(p.x, p.y, 0, 255, 0);
    }
}

void Face::paintInside(std::unique_ptr<Image>& image, FaceIndex facepart)
{
    int inside = 0;
    long startIndex = 0;

    long startPixel = (boundingBox_.l + boundingBox_.t * image->info.width) * image->info.pixelSizeBytes;

    // El 25 es per corregir el bottom massa curt. Aixi no talla la cara,
    long endPixel = (boundingBox_.r + (boundingBox_.b + 25) * image->info.width) * image->info.pixelSizeBytes;

    for (long i = startPixel; i < endPixel; i += image->info.pixelSizeBytes)
    {
        for (const Landmark& landmark : landmarks_[facepart])
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

void Face::paintFaceIndex(std::unique_ptr<Image>& image, FaceIndex facepart, bool joinPoints, Pixel color)
{
    std::vector<Landmark> points = landmarks_[facepart];
    math_utils::Point lastPoint(-1, -1);
    for (const Landmark& l : points)
    {
        if (joinPoints)
        {
            if (lastPoint.x != -1 && lastPoint.y != -1)
            {
                // We know a last point. Proceed with DDA and paint it
                std::vector<math_utils::Point> points = math_utils::DDA(lastPoint.x, lastPoint.y, l.p.x, l.p.y);
                for (const math_utils::Point& p : points)
                {
                    image->ppx(p.x, p.y, color);
                }
            }
            lastPoint = l.p;
        }
        image->ppx(l.p.x, l.p.y, color);
    }
}
