/**
 * @file test_face_iou_matching.cpp
 * @brief Unit tests for Face IoU calculation and face matching functionality
 *
 * This test suite validates the Face::findBestMatchingFace function and ensures
 * that IoU calculations work correctly for face bounding box matching.
 *
 * Tests cover:
 * - Basic IoU calculation accuracy
 * - Face matching with various overlap scenarios
 * - Edge cases (no overlap, perfect overlap, partial overlap)
 * - Multiple face selection (best match)
 * - Threshold behavior
 */

#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <vector>

#include "LinuxFace/face.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class FaceIoUMatchingTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create some test faces with known bounding boxes
        face1 = Face(FaceBoundingBox(100.0f, 100.0f, 200.0f, 200.0f)); // 100x100 square at (100,100)
        face2 = Face(FaceBoundingBox(150.0f, 150.0f, 250.0f, 250.0f)); // 100x100 square at (150,150) - 50% overlap
        face3 = Face(FaceBoundingBox(300.0f, 300.0f, 400.0f, 400.0f)); // 100x100 square at (300,300) - no overlap
        face4 = Face(FaceBoundingBox(100.0f, 100.0f, 200.0f, 200.0f)); // Identical to face1_

        // Ground truth bounding box for comparison
        ground_truth_box = math_utils::Rect<double>(100.0, 100.0, 200.0, 200.0); // Same as face1_
    }

    Face face1, face2, face3, face4;
    math_utils::Rect<double> ground_truth_box;
};

TEST_F(FaceIoUMatchingTest, PerfectOverlapReturnsIoUOfOne)
{
    std::vector<Face> faces = {face1}; // Face1 matches ground truth perfectly

    auto result = Face::findBestMatchingFace(faces, ground_truth_box, 0.1);

    EXPECT_TRUE(result.found_match) << "Should find perfect match";
    EXPECT_EQ(result.face_index, 0) << "Should select first (and only) face";
    EXPECT_NEAR(result.iou_score, 1.0, 0.001) << "Perfect overlap should give IoU = 1.0";
    EXPECT_EQ(result.best_face, &faces[0]) << "Should return pointer to the matched face";
}

TEST_F(FaceIoUMatchingTest, PartialOverlapCalculatesCorrectIoU)
{
    std::vector<Face> faces = {face2}; // Face2 has 50% overlap with ground truth

    auto result = Face::findBestMatchingFace(faces, ground_truth_box, 0.1);

    EXPECT_TRUE(result.found_match) << "Should find match above threshold";
    EXPECT_EQ(result.face_index, 0) << "Should select the face";

    // Calculate expected IoU manually:
    // Ground truth: (100,100,200,200) = 100x100 = 10,000 area
    // Face2: (150,150,250,250) = 100x100 = 10,000 area
    // Intersection: (150,150,200,200) = 50x50 = 2,500 area
    // Union: 10,000 + 10,000 - 2,500 = 17,500
    // IoU: 2,500 / 17,500 = 0.1429 (approximately 1/7)
    double expected_iou = 2500.0 / 17500.0; // ≈ 0.1429
    EXPECT_NEAR(result.iou_score, expected_iou, 0.001)
        << "IoU should be " << expected_iou << " for 50% overlap of equal-sized boxes";
}

TEST_F(FaceIoUMatchingTest, NoOverlapReturnsZeroIoU)
{
    std::vector<Face> faces = {face3}; // Face3 has no overlap with ground truth

    auto result = Face::findBestMatchingFace(faces, ground_truth_box, 0.1);

    EXPECT_FALSE(result.found_match) << "Should not find match below threshold";
    EXPECT_EQ(result.face_index, -1) << "Should not select any face";
    EXPECT_NEAR(result.iou_score, 0.0, 0.001) << "No overlap should give IoU = 0.0";
    EXPECT_EQ(result.best_face, nullptr) << "Should not return face pointer";
}

TEST_F(FaceIoUMatchingTest, MultipleFacesSelectsBestMatch)
{
    std::vector<Face> faces = {face3, face2, face1}; // No overlap, partial overlap, perfect overlap

    auto result = Face::findBestMatchingFace(faces, ground_truth_box, 0.1);

    EXPECT_TRUE(result.found_match) << "Should find best match";
    EXPECT_EQ(result.face_index, 2) << "Should select face1_ (index 2) as best match";
    EXPECT_NEAR(result.iou_score, 1.0, 0.001) << "Best match should have IoU = 1.0";
    EXPECT_EQ(result.best_face, &faces[2]) << "Should return pointer to face1_";
}

TEST_F(FaceIoUMatchingTest, ThresholdFilteringWorks)
{
    std::vector<Face> faces = {face2}; // Face2 has IoU ≈ 0.1429

    // Test with threshold below IoU value - should match
    auto result_low = Face::findBestMatchingFace(faces, ground_truth_box, 0.1);
    EXPECT_TRUE(result_low.found_match) << "Should match with low threshold";

    // Test with threshold above IoU value - should not match
    auto result_high = Face::findBestMatchingFace(faces, ground_truth_box, 0.2);
    EXPECT_FALSE(result_high.found_match) << "Should not match with high threshold";
    EXPECT_NEAR(result_high.iou_score, result_low.iou_score, 0.001)
        << "IoU score should be same regardless of threshold";
}

TEST_F(FaceIoUMatchingTest, EmptyFaceListReturnsNoMatch)
{
    std::vector<Face> empty_faces;

    auto result = Face::findBestMatchingFace(empty_faces, ground_truth_box, 0.1);

    EXPECT_FALSE(result.found_match) << "Should not find match in empty list";
    EXPECT_EQ(result.face_index, -1) << "Should not select any face";
    EXPECT_NEAR(result.iou_score, 0.0, 0.001) << "Should have zero IoU score";
    EXPECT_EQ(result.best_face, nullptr) << "Should not return face pointer";
}

TEST_F(FaceIoUMatchingTest, IdenticalFacesReturnSameIoU)
{
    std::vector<Face> faces = {face1, face4}; // Both identical to ground truth

    auto result = Face::findBestMatchingFace(faces, ground_truth_box, 0.1);

    EXPECT_TRUE(result.found_match) << "Should find match";
    EXPECT_EQ(result.face_index, 0) << "Should select first face (both are equally good)";
    EXPECT_NEAR(result.iou_score, 1.0, 0.001) << "Both should have perfect IoU";
}

TEST_F(FaceIoUMatchingTest, RealWorldCoordinatesExample)
{
    // Test with realistic coordinates from actual WFLW data
    // Ground truth: [198,242,331,385] (width=133, height=143)
    math_utils::Rect<double> real_gt(198.0, 242.0, 331.0, 385.0);

    // Detected face: [193.885,241.836,333.760,389.455] (should have high IoU)
    Face real_face(FaceBoundingBox(193.885f, 241.836f, 333.760f, 389.455f));
    std::vector<Face> faces = {real_face};

    auto result = Face::findBestMatchingFace(faces, real_gt, 0.1);

    EXPECT_TRUE(result.found_match) << "Should find match with realistic coordinates";
    EXPECT_GT(result.iou_score, 0.8) << "Should have high IoU (>80%) for close match";
    EXPECT_LT(result.iou_score, 1.0) << "Should not be perfect match";

    // This test ensures our fix for the coordinate system bug is working
    std::cout << "Real-world IoU: " << std::fixed << std::setprecision(3) << result.iou_score << std::endl;
}

TEST_F(FaceIoUMatchingTest, EdgeCasesHandledCorrectly)
{
    // Test with zero-area boxes and other edge cases
    Face zero_area_face(FaceBoundingBox(100.0f, 100.0f, 100.0f, 100.0f)); // Zero area
    Face tiny_face(FaceBoundingBox(100.0f, 100.0f, 101.0f, 101.0f));      // 1x1 pixel

    std::vector<Face> edge_faces = {zero_area_face, tiny_face};

    auto result = Face::findBestMatchingFace(edge_faces, ground_truth_box, 0.1);

    // Should handle gracefully without crashing
    EXPECT_NO_THROW({ Face::findBestMatchingFace(edge_faces, ground_truth_box, 0.1); })
        << "Should handle edge cases without throwing";

    // Result behavior for edge cases may vary, but should not crash
    std::cout << "Edge case IoU: " << result.iou_score << ", found_match: " << result.found_match << std::endl;
}

TEST_F(FaceIoUMatchingTest, IoUCalculationMatchesManualCalculation)
{
    // Test IoU calculation with known values for verification
    // Box A: (0,0,100,100) - area = 10,000
    // Box B: (50,50,150,150) - area = 10,000
    // Intersection: (50,50,100,100) - area = 2,500
    // Union: 10,000 + 10,000 - 2,500 = 17,500
    // IoU: 2,500 / 17,500 = 1/7 ≈ 0.14286

    Face test_face(FaceBoundingBox(50.0f, 50.0f, 150.0f, 150.0f));
    math_utils::Rect<double> test_gt(0.0, 0.0, 100.0, 100.0);

    std::vector<Face> faces = {test_face};
    auto result = Face::findBestMatchingFace(faces, test_gt, 0.1);

    double expected_iou = 1.0 / 7.0; // Exact mathematical result
    EXPECT_NEAR(result.iou_score, expected_iou, 0.001) << "IoU should match manual calculation: " << expected_iou;
}
