#ifndef FACE_H
#define FACE_H

#include <iostream>
#include <map>
#include <vector>

#include "FunnyFace/image.h"
#include "FunnyFace/math_utils.h"

namespace funnyface
{

struct FaceLandmark
{
    // Index of the landmark
    unsigned int i;
    // Location of the landmark
    math_utils::Point p;
};

struct FaceBoundingBox
{
    FaceBoundingBox() = default;
    FaceBoundingBox(float left, float top, float right, float bottom) : rect(left, top, right, bottom) {}

    // Location of the bounding box
    math_utils::Rect<float> rect;

    // Confidence of the bounding box
    float score;
};

struct FacePose
{
    float yaw;
    float pitch;
    float roll;
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

    Face(FaceBoundingBox boundingBox);
    Face(std::vector<FaceLandmark> landmarks, FaceBoundingBox boundingBox);
    Face() = default;
    ~Face();

    void loadNewFaceLandmarks(std::vector<FaceLandmark> landmarks);

    FaceIndex get_facepart_from_landmark_id(unsigned long id) const;

    void paintAllFaceLandmarks(std::unique_ptr<Image>& image, bool joinPoints) const;
    void paintFaceIndex(std::unique_ptr<Image>& image, FaceIndex facepart, bool joinPoints, Pixel color) const;

    void paintBoundingBox(std::unique_ptr<Image>& image) const;
    void paintInside(std::unique_ptr<Image>& image, FaceIndex facepart) const;

    void paintPoseAxis(std::unique_ptr<Image>& image, float size, float thickness) const;

    float getScore() const { return boundingBox_.score; };
  private:
    void freeFaceLandmarks();

    FaceBoundingBox boundingBox_;
    std::map<FaceIndex, std::vector<FaceLandmark>> landmarks_;
    FacePose pose_;
};

} // namespace funnyface

#endif // FACE_H
