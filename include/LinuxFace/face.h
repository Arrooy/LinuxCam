#ifndef FACE_H
#define FACE_H

#include <iostream>
#include <map>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"

namespace linuxface
{

struct FaceLandmark
{
    // Index of the landmark
    unsigned int i{};
    // 3D location of the landmark
    math_utils::Point3D p;
    // Confidence of the landmark
    float score{0.0f};
};

struct FaceBoundingBox
{
    FaceBoundingBox() : rect(0.0f, 0.0f, 0.0f, 0.0f), score(0.0f) {}
    FaceBoundingBox(float left, float top, float right, float bottom) : rect(left, top, right, bottom), score(0.0f) {}

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

    explicit Face(FaceBoundingBox boundingBox);
    Face(std::vector<FaceLandmark> landmarks, FaceBoundingBox boundingBox);
    Face() = default;
    ~Face();

    void loadNewFaceLandmarks(const std::vector<FaceLandmark>& landmarks);

    static FaceIndex getFacepartFromLandmarkId(unsigned long id);

    void paintAllFaceLandmarks(std::unique_ptr<Image>& image, bool joinPoints, Pixel c, float radius = 1) const;
    void paintFaceIndex(std::unique_ptr<Image>& image, FaceIndex facepart, bool joinPoints, Pixel color,
                        float radius = 1) const;

    void paintBoundingBox(std::unique_ptr<Image>& image, Pixel color = Pixel(0, 255, 0)) const;
    void paintInside(std::unique_ptr<Image>& image, FaceIndex facepart) const;

    void paintPoseAxis(std::unique_ptr<Image>& image, float size, float thickness) const;

    FaceBoundingBox getBoundingBox() const { return boundingBox_; }
    std::vector<FaceLandmark> getLandmarks() const
    {
        std::vector<FaceLandmark> allLandmarks;
        for (const auto& facepart : landmarks_)
        {
            const auto& partLandmarks = facepart.second;
            allLandmarks.insert(allLandmarks.end(), partLandmarks.begin(), partLandmarks.end());
        }
        return allLandmarks;
    }
    void setFacePose(FacePose pose) { pose_ = pose; }
    // Retrieve 5-point landmarks in ArcFace order: [left eye, right eye, nose, left mouth, right mouth] (3D)
    std::vector<math_utils::Point3D> getFivePointLandmarksArcFaceOrder() const;
    // Retrieve 5-point landmarks in ArcFace order (2D)
    std::vector<math_utils::Point<>> getFivePointLandmarksArcFaceOrder2D() const;
    math_utils::Point3D getLandmarkByIndex(unsigned int id) const;

    // Face matching utilities
    struct FaceMatchResult
    {
        Face* best_face = nullptr;
        int face_index = -1;
        double iou_score = 0.0;
        bool found_match = false;
    };

    /**
     * Find the best matching face from a list of detected faces based on IoU with ground truth bounding box
     * @param detected_faces List of detected faces to search through
     * @param ground_truth_bbox Ground truth bounding box to match against
     * @param min_iou_threshold Minimum IoU threshold for a valid match (default: 0.1)
     * @return FaceMatchResult containing the best match and its details
     */
    static FaceMatchResult
    findBestMatchingFace(std::vector<Face>& detectedFaces, const math_utils::Rect<double>& groundTruthBbox,
                         double minIouThreshold = 0.1);

    bool hasLandmarks() const { return !landmarks_.empty(); }
    bool isValid() const { return valid_; }
  private:
    void freeFaceLandmarks();

    FaceBoundingBox boundingBox_;
    std::map<FaceIndex, std::vector<FaceLandmark>> landmarks_;
    FacePose pose_{};
    bool valid_ {false};
};

} // namespace linuxface

#endif // FACE_H
