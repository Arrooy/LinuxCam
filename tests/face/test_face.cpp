#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "LinuxFace/face.h"

using namespace linuxface;

TEST(FaceTest, BoundingBoxConstructorAndGet)
{
    FaceBoundingBox bbox(1.0f, 2.0f, 3.0f, 4.0f);
    bbox.score = 0.9f;
    Face face(bbox);
    auto out_bbox = face.getBoundingBox();
    EXPECT_FLOAT_EQ(out_bbox.rect.l, 1.0f);
    EXPECT_FLOAT_EQ(out_bbox.rect.t, 2.0f);
    EXPECT_FLOAT_EQ(out_bbox.rect.r, 3.0f);
    EXPECT_FLOAT_EQ(out_bbox.rect.b, 4.0f);
    EXPECT_FLOAT_EQ(out_bbox.score, 0.9f);
}

TEST(FaceTest, LandmarkConstructorAndGetLandmarks)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1,  2,  3) },
        {45, math_utils::Point3D(4,  5,  6) },
        {33, math_utils::Point3D(7,  8,  9) },
        {48, math_utils::Point3D(10, 11, 12)},
        {54, math_utils::Point3D(13, 14, 15)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto all_landmarks = face.getLandmarks();
    EXPECT_EQ(all_landmarks.size(), 5);
    EXPECT_EQ(all_landmarks[0].i, 36);
    EXPECT_EQ(all_landmarks[1].i, 45);
    EXPECT_EQ(all_landmarks[2].i, 33);
    EXPECT_EQ(all_landmarks[3].i, 48);
    EXPECT_EQ(all_landmarks[4].i, 54);
}

TEST(FaceTest, LoadNewFaceLandmarks)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(bbox);
    std::vector<FaceLandmark> landmarks = {
        {0,  math_utils::Point3D(1,  2,  3) },
        {17, math_utils::Point3D(4,  5,  6) },
        {22, math_utils::Point3D(7,  8,  9) },
        {27, math_utils::Point3D(10, 11, 12)},
        {36, math_utils::Point3D(13, 14, 15)}
    };
    face.loadNewFaceLandmarks(landmarks);
    auto all_landmarks = face.getLandmarks();
    EXPECT_EQ(all_landmarks.size(), 5);
}

TEST(FaceTest, GetFacePartFromLandmarkId)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(bbox);
    EXPECT_EQ(face.get_facepart_from_landmark_id(0), Face::JAW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(17), Face::LBROW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(22), Face::RBROW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(27), Face::NOSE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(36), Face::LEYE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(42), Face::REYE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(48), Face::OUTERMOUTH);
    EXPECT_EQ(face.get_facepart_from_landmark_id(60), Face::INNERMOUTH);
    EXPECT_EQ(face.get_facepart_from_landmark_id(100), Face::UNKNOWN);
}

TEST(FaceTest, GetFivePointLandmarksArcFaceOrder)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1,  2,  3) },
        {45, math_utils::Point3D(4,  5,  6) },
        {33, math_utils::Point3D(7,  8,  9) },
        {48, math_utils::Point3D(10, 11, 12)},
        {54, math_utils::Point3D(13, 14, 15)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto five3d = face.getFivePointLandmarksArcFaceOrder();
    EXPECT_EQ(five3d.size(), 5);
    EXPECT_DOUBLE_EQ(five3d[0].x, 1);
    EXPECT_DOUBLE_EQ(five3d[1].x, 4);
    EXPECT_DOUBLE_EQ(five3d[2].x, 7);
    EXPECT_DOUBLE_EQ(five3d[3].x, 10);
    EXPECT_DOUBLE_EQ(five3d[4].x, 13);
    auto five2d = face.getFivePointLandmarksArcFaceOrder2D();
    EXPECT_EQ(five2d.size(), 5);
    EXPECT_EQ(five2d[0].x, 1);
    EXPECT_EQ(five2d[1].x, 4);
    EXPECT_EQ(five2d[2].x, 7);
    EXPECT_EQ(five2d[3].x, 10);
    EXPECT_EQ(five2d[4].x, 13);
}

TEST(FaceTest, GetLandmarkByIndex)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1,  2,  3) },
        {45, math_utils::Point3D(4,  5,  6) },
        {33, math_utils::Point3D(7,  8,  9) },
        {48, math_utils::Point3D(10, 11, 12)},
        {54, math_utils::Point3D(13, 14, 15)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto pt = face.getLandmarkByIndex(45);
    EXPECT_DOUBLE_EQ(pt.x, 4);
    EXPECT_DOUBLE_EQ(pt.y, 5);
    EXPECT_DOUBLE_EQ(pt.z, 6);
    auto pt2 = face.getLandmarkByIndex(99);
    EXPECT_DOUBLE_EQ(pt2.x, -1);
    EXPECT_DOUBLE_EQ(pt2.y, -1);
    EXPECT_DOUBLE_EQ(pt2.z, -1);
}

TEST(FaceTest, SetFacePose)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(bbox);
    FacePose pose{1.0f, 2.0f, 3.0f};
    face.setFacePose(pose);
    // No getter, but this ensures no crash
    SUCCEED();
}

TEST(FaceTest, EmptyLandmarksVector)
{
    std::vector<FaceLandmark> landmarks;
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto all_landmarks = face.getLandmarks();
    EXPECT_TRUE(all_landmarks.empty());
    auto five3d = face.getFivePointLandmarksArcFaceOrder();
    EXPECT_EQ(five3d.size(), 5);
    for (const auto& pt : five3d)
    {
        EXPECT_DOUBLE_EQ(pt.x, 0);
        EXPECT_DOUBLE_EQ(pt.y, 0);
        EXPECT_DOUBLE_EQ(pt.z, 0);
    }
}

TEST(FaceTest, FewerThanFiveLandmarksArcFaceOrder)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1, 2, 3)},
        {45, math_utils::Point3D(4, 5, 6)},
        {33, math_utils::Point3D(7, 8, 9)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto five3d = face.getFivePointLandmarksArcFaceOrder();
    EXPECT_EQ(five3d.size(), 5);
}

TEST(FaceTest, MoreThanFiveLandmarksArcFaceOrder)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1,  2,  3) },
        {45, math_utils::Point3D(4,  5,  6) },
        {33, math_utils::Point3D(7,  8,  9) },
        {48, math_utils::Point3D(10, 11, 12)},
        {54, math_utils::Point3D(13, 14, 15)},
        {60, math_utils::Point3D(16, 17, 18)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto five3d = face.getFivePointLandmarksArcFaceOrder();
    EXPECT_EQ(five3d.size(), 5);
}

TEST(FaceTest, FaceBoundingBoxDefaultConstructor)
{
    FaceBoundingBox bbox;
    EXPECT_FLOAT_EQ(bbox.rect.l, 0.0f);
    EXPECT_FLOAT_EQ(bbox.rect.t, 0.0f);
    EXPECT_FLOAT_EQ(bbox.rect.r, 0.0f);
    EXPECT_FLOAT_EQ(bbox.rect.b, 0.0f);
    EXPECT_FLOAT_EQ(bbox.score, 0.0f);
}

TEST(FaceTest, FaceBoundingBoxNegativeWidthHeight)
{
    FaceBoundingBox bbox(-1.0f, -2.0f, -3.0f, -4.0f);
    EXPECT_LT(bbox.rect.width(), 0);
    EXPECT_LT(bbox.rect.height(), 0);
}

TEST(FaceTest, FacePoseExtremeValues)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(bbox);
    FacePose pose{9999.0f, -9999.0f, 360.0f};
    face.setFacePose(pose);
    SUCCEED();
}

TEST(FaceTest, FreeFaceLandmarksClearsData)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1, 2, 3)},
        {45, math_utils::Point3D(4, 5, 6)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    face.loadNewFaceLandmarks(landmarks);
    face.loadNewFaceLandmarks({});
    auto all_landmarks = face.getLandmarks();
    EXPECT_TRUE(all_landmarks.empty());
}

TEST(FaceTest, LoadNewFaceLandmarksRepeatedCalls)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(bbox);
    std::vector<FaceLandmark> landmarks1 = {
        {36, math_utils::Point3D(1, 2, 3)}
    };
    std::vector<FaceLandmark> landmarks2 = {
        {45, math_utils::Point3D(4, 5, 6)}
    };
    face.loadNewFaceLandmarks(landmarks1);
    face.loadNewFaceLandmarks(landmarks2);
    auto all_landmarks = face.getLandmarks();
    EXPECT_EQ(all_landmarks.size(), 1);
    EXPECT_EQ(all_landmarks[0].i, 45);
}

TEST(FaceTest, GetLandmarkByIndexDuplicate)
{
    std::vector<FaceLandmark> landmarks = {
        {36, math_utils::Point3D(1, 2, 3)},
        {36, math_utils::Point3D(4, 5, 6)}
    };
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(landmarks, bbox);
    auto pt = face.getLandmarkByIndex(36);
    EXPECT_DOUBLE_EQ(pt.x, 1);
    EXPECT_DOUBLE_EQ(pt.y, 2);
    EXPECT_DOUBLE_EQ(pt.z, 3);
}

TEST(FaceTest, GetFacePartFromLandmarkIdBoundaries)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face face(bbox);
    EXPECT_EQ(face.get_facepart_from_landmark_id(16), Face::JAW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(17), Face::LBROW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(21), Face::LBROW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(22), Face::RBROW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(26), Face::RBROW);
    EXPECT_EQ(face.get_facepart_from_landmark_id(27), Face::NOSE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(35), Face::NOSE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(36), Face::LEYE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(41), Face::LEYE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(42), Face::REYE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(47), Face::REYE);
    EXPECT_EQ(face.get_facepart_from_landmark_id(48), Face::OUTERMOUTH);
    EXPECT_EQ(face.get_facepart_from_landmark_id(59), Face::OUTERMOUTH);
    EXPECT_EQ(face.get_facepart_from_landmark_id(60), Face::INNERMOUTH);
    EXPECT_EQ(face.get_facepart_from_landmark_id(67), Face::INNERMOUTH);
}

TEST(FaceTest, DestructorNoCrash)
{
    FaceBoundingBox bbox(0, 0, 10, 10);
    Face* face = new Face(bbox);
    delete face;
    SUCCEED();
}

TEST(FaceTest, PaintPoseAxisNoCrash)
{
    // Test that paintPoseAxis works without crashing
    FaceBoundingBox bbox(10, 10, 100, 100);
    Face face(bbox);
    
    // Set a pose
    FacePose pose{30.0f, 45.0f, 15.0f};
    face.setFacePose(pose);
    
    // Create a test image using the constructor that takes color, width, height
    Pixel backgroundColor(0, 0, 0, 255); // Black background
    auto image = std::make_unique<Image>(backgroundColor, 200, 200);
    
    // This should not crash and should work with the simplified interface
    EXPECT_NO_THROW(face.paintPoseAxis(image, 50.0f, 2.0f));
    
    // Verify image is still valid
    EXPECT_TRUE(image->data() != nullptr);
    EXPECT_EQ(image->info.width, 200);
    EXPECT_EQ(image->info.height, 200);
}
