/* -*- c++ -*- */

#ifndef FACE_H
#define FACE_H

#include <iostream>
#include <map>
#include <vector>

#include "image.h"
#include "math_utils.h"

namespace funnyface
{

struct Landmark
{
    // Index of the landmark
    unsigned int i;
    // Location of the landmark
    math_utils::Point p;
};

// Represents a human face. With all its landmarks. Currently suported 64
// Interpolate all landmarks into a face.
// Divides all landmarks into the different face parts.
class Face
{
  public:
    // Order in the definition is important!
    enum FaceIndex
    {
        JAW,
        LEYE,
        REYE,
        LBROW,
        RBROW,
        NOSE,
        OUTERMOUTH,
        INNERMOUTH,
        SILHOUETTE,
        UNKNOWN
    };

    Face(math_utils::Rect boundingBox);
    Face(std::vector<Landmark> landmarks, math_utils::Rect boundingBox);
    ~Face();

    void loadNewLandmarks(std::vector<Landmark> landmarks);

    FaceIndex get_facepart_from_landmark_id(unsigned long id) const;

    void paintAllLandmarks(Image& image, bool joinPoints);
    void paintFaceIndex(Image& image, FaceIndex facepart, bool joinPoints, Pixel color);

    void paintBoundingBox(Image& image);
    void paintInside(Image& image, FaceIndex facepart);
  private:
    void freeLandmarks();

    math_utils::Rect boundingBox_;
    std::map<FaceIndex, std::vector<Landmark>> landmarks_;
};

} // namespace funnyface

#endif // FACE_H
