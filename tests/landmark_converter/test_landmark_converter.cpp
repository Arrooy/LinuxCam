/**
 * @file test_landmark_converter.cpp
 * @brief Comprehensive unit tests for LandmarkConverter functionality
 * Covers all methods, edge cases, and error conditions for 100% code coverage
 */

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <chrono>

#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class LandmarkConverterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
                // Initialize test landmark data
        pfld_landmarks_.clear();
        wflw_landmarks_.clear();
        
        // Create valid PFLD landmarks (106 points)
        for (unsigned int i = 0; i < 106; ++i)
        {
            pfld_landmarks_.emplace_back(FaceLandmark{
                i,
                math_utils::Point3D(100.0 + i, 200.0 + i, 0.0)
            });
        }
        
        // Create valid WFLW landmarks (98 points)
        for (unsigned int i = 0; i < 98; ++i)
        {
            wflw_landmarks_.emplace_back(FaceLandmark{
                i,
                math_utils::Point3D(150.0 + i, 250.0 + i, 0.0)
            });
        }

        // Create sample SCRFD 5-point landmarks
        scrfd_landmarks_ = {
            {0, math_utils::Point3D(120, 150, 0)}, // Left eye
            {1, math_utils::Point3D(180, 150, 0)}, // Right eye
            {2, math_utils::Point3D(150, 180, 0)}, // Nose
            {3, math_utils::Point3D(130, 220, 0)}, // Left mouth
            {4, math_utils::Point3D(170, 220, 0)}  // Right mouth
        };
    }

    std::vector<FaceLandmark> pfld_landmarks_;
    std::vector<FaceLandmark> wflw_landmarks_;
    std::vector<FaceLandmark> scrfd_landmarks_;
};

// Test format validation with various edge cases
TEST_F(LandmarkConverterTest, ValidateLandmarkFormat)
{
    EXPECT_TRUE(LandmarkConverter::validateLandmarkFormat(pfld_landmarks_, LandmarkFormat::PFLD_106));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(pfld_landmarks_, LandmarkFormat::WFLW_98));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(pfld_landmarks_, LandmarkFormat::SCRFD_5));

    EXPECT_TRUE(LandmarkConverter::validateLandmarkFormat(wflw_landmarks_, LandmarkFormat::WFLW_98));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wflw_landmarks_, LandmarkFormat::PFLD_106));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wflw_landmarks_, LandmarkFormat::SCRFD_5));

    EXPECT_TRUE(LandmarkConverter::validateLandmarkFormat(scrfd_landmarks_, LandmarkFormat::SCRFD_5));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(scrfd_landmarks_, LandmarkFormat::PFLD_106));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(scrfd_landmarks_, LandmarkFormat::WFLW_98));
    
    // Edge case: empty landmarks
    std::vector<FaceLandmark> empty_landmarks;
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(empty_landmarks, LandmarkFormat::PFLD_106));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(empty_landmarks, LandmarkFormat::WFLW_98));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(empty_landmarks, LandmarkFormat::SCRFD_5));
    
    // Edge case: wrong size landmarks
    std::vector<FaceLandmark> wrong_size_landmarks(50);
    for (int i = 0; i < 50; ++i)
    {
        wrong_size_landmarks[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_landmarks, LandmarkFormat::PFLD_106));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_landmarks, LandmarkFormat::WFLW_98));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_landmarks, LandmarkFormat::SCRFD_5));
}

// Test expected landmark counts including edge cases
TEST_F(LandmarkConverterTest, GetExpectedLandmarkCount)
{
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::PFLD_106), 106);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::WFLW_98), 98);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::SCRFD_5), 5);
    
    // Test with invalid/unknown format (should return 0)
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(static_cast<LandmarkFormat>(999)), 0);
}

// Test PFLD to WFLW conversion
TEST_F(LandmarkConverterTest, PfldToWflwConversion)
{
    auto converted = LandmarkConverter::pfldToWflw(pfld_landmarks_);
    
    // Should produce exactly 98 landmarks
    EXPECT_EQ(converted.size(), 98);
    
    // Check that indices are properly set (0-97)
    for (size_t i = 0; i < converted.size(); ++i)
    {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that coordinates are reasonable (not all zeros)
    bool has_non_zero = false;
    for (const auto& landmark : converted)
    {
        if (landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0)
        {
            has_non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(has_non_zero) << "All converted landmarks are at origin";
}

// Test WFLW to PFLD conversion
TEST_F(LandmarkConverterTest, WflwToPfldConversion)
{
    auto converted = LandmarkConverter::wflwToPfld(wflw_landmarks_);
    
    // Should produce exactly 106 landmarks
    EXPECT_EQ(converted.size(), 106);
    
    // Check that indices are properly set (0-105)
    for (size_t i = 0; i < converted.size(); ++i)
    {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that at least some landmarks have valid coordinates
    bool has_non_zero = false;
    for (const auto& landmark : converted)
    {
        if (landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0)
        {
            has_non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(has_non_zero) << "All converted landmarks are at origin";
}

// Test invalid input handling
TEST_F(LandmarkConverterTest, InvalidInputHandling)
{
    // Test invalid PFLD input
    std::vector<FaceLandmark> invalid_pfld = {pfld_landmarks_[0]}; // Only 1 landmark
    EXPECT_THROW(LandmarkConverter::pfldToWflw(invalid_pfld), std::invalid_argument);
    
    // Test invalid WFLW input
    std::vector<FaceLandmark> invalid_wflw = {wflw_landmarks_[0]}; // Only 1 landmark
    EXPECT_THROW(LandmarkConverter::wflwToPfld(invalid_wflw), std::invalid_argument);
}

// Test key landmark extraction
TEST_F(LandmarkConverterTest, ExtractKeyLandmarks)
{
    // Test PFLD key landmark extraction
    auto pfld_key = LandmarkConverter::extractKeyLandmarks(pfld_landmarks_, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_key.size(), 5);
    for (size_t i = 0; i < pfld_key.size(); ++i)
    {
        EXPECT_EQ(pfld_key[i].i, static_cast<int>(i));
    }
    
    // Test WFLW key landmark extraction
    auto wflw_key = LandmarkConverter::extractKeyLandmarks(wflw_landmarks_, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_key.size(), 5);
    for (size_t i = 0; i < wflw_key.size(); ++i)
    {
        EXPECT_EQ(wflw_key[i].i, static_cast<int>(i));
    }
    
    // Test SCRFD key landmark extraction (should be identity)
    auto scrfd_key = LandmarkConverter::extractKeyLandmarks(scrfd_landmarks_, LandmarkFormat::SCRFD_5);
    EXPECT_EQ(scrfd_key.size(), 5);
    for (size_t i = 0; i < scrfd_key.size(); ++i)
    {
        EXPECT_EQ(scrfd_key[i].i, scrfd_landmarks_[i].i);
        EXPECT_EQ(scrfd_key[i].p.x, scrfd_landmarks_[i].p.x);
        EXPECT_EQ(scrfd_key[i].p.y, scrfd_landmarks_[i].p.y);
    }
}

// Test region indices retrieval
TEST_F(LandmarkConverterTest, GetRegionIndices)
{
    // Test WFLW region indices
    auto wflw_jawline = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_jawline.size(), 33); // Indices 0-32
    EXPECT_EQ(wflw_jawline[0], 0);
    EXPECT_EQ(wflw_jawline[32], 32);
    
    auto wflw_right_eyebrow = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYEBROW, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_right_eyebrow.size(), 9); // Indices 33-41
    EXPECT_EQ(wflw_right_eyebrow[0], 33);
    EXPECT_EQ(wflw_right_eyebrow[8], 41);
    
    // Test PFLD region indices
    auto pfld_jawline = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_jawline.size(), 17); // Indices 0-16
    EXPECT_EQ(pfld_jawline[0], 0);
    EXPECT_EQ(pfld_jawline[16], 16);
}

// Test round-trip conversion accuracy
TEST_F(LandmarkConverterTest, RoundTripConversion)
{
    // PFLD -> WFLW -> PFLD
    auto wflw_converted = LandmarkConverter::pfldToWflw(pfld_landmarks_);
    auto pfld_restored = LandmarkConverter::wflwToPfld(wflw_converted);
    
    EXPECT_EQ(pfld_restored.size(), 106);
    
    // The round-trip won't be perfect due to format differences, but should preserve key structure
    // Check that at least the first 98 landmarks have reasonable coordinates
    int valid_landmarks = 0;
    for (size_t i = 0; i < 98 && i < pfld_restored.size(); ++i)
    {
        if (pfld_restored[i].p.x != 0 || pfld_restored[i].p.y != 0)
        {
            valid_landmarks++;
        }
    }
    EXPECT_GT(valid_landmarks, 90) << "Most landmarks should be preserved in round-trip conversion";
}

// Test edge case: empty landmarks
TEST_F(LandmarkConverterTest, EmptyLandmarks)
{
    std::vector<FaceLandmark> empty_landmarks;
    
    EXPECT_THROW(LandmarkConverter::pfldToWflw(empty_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::wflwToPfld(empty_landmarks), std::invalid_argument);
    
    auto key_landmarks = LandmarkConverter::extractKeyLandmarks(empty_landmarks, LandmarkFormat::PFLD_106);
    EXPECT_TRUE(key_landmarks.empty());
}

// Test landmark coordinate preservation
TEST_F(LandmarkConverterTest, CoordinatePreservation)
{
    // Create PFLD landmarks with specific known coordinates
    std::vector<FaceLandmark> test_pfld;
    for (int i = 0; i < 106; ++i)
    {
        test_pfld.emplace_back(FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i * 10.0, i * 5.0, i * 2.0)});
    }
    
    auto converted_wflw = LandmarkConverter::pfldToWflw(test_pfld);
    
    // Check that some coordinates are preserved (not all will be due to mapping)
    bool coordinates_preserved = false;
    for (const auto& wflw_landmark : converted_wflw)
    {
        // Check if this coordinate exists in the original PFLD set
        for (const auto& pfld_landmark : test_pfld)
        {
            if (std::abs(wflw_landmark.p.x - pfld_landmark.p.x) < 0.001 &&
                std::abs(wflw_landmark.p.y - pfld_landmark.p.y) < 0.001)
            {
                coordinates_preserved = true;
                break;
            }
        }
        if (coordinates_preserved) break;
    }
    
    EXPECT_TRUE(coordinates_preserved) << "Some coordinates should be preserved in conversion";
}

// Test facial region indices retrieval with comprehensive coverage
TEST_F(LandmarkConverterTest, GetRegionIndicesComprehensive)
{
    // Test all WFLW regions
    auto wflw_jawline = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_jawline.size(), 33); // Indices 0-32
    EXPECT_EQ(wflw_jawline[0], 0);
    EXPECT_EQ(wflw_jawline[32], 32);
    
    auto wflw_right_eyebrow = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYEBROW, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_right_eyebrow.size(), 9); // Indices 33-41
    EXPECT_EQ(wflw_right_eyebrow[0], 33);
    EXPECT_EQ(wflw_right_eyebrow[8], 41);
    
    auto wflw_left_eyebrow = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYEBROW, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_left_eyebrow.size(), 9); // Indices 42-50
    EXPECT_EQ(wflw_left_eyebrow[0], 42);
    EXPECT_EQ(wflw_left_eyebrow[8], 50);
    
    auto wflw_nose = LandmarkConverter::getRegionIndices(FacialRegion::NOSE_BRIDGE, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_nose.size(), 9); // Indices 51-59
    EXPECT_EQ(wflw_nose[0], 51);
    EXPECT_EQ(wflw_nose[8], 59);
    
    auto wflw_right_eye = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYE, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_right_eye.size(), 8); // Indices 60-67
    EXPECT_EQ(wflw_right_eye[0], 60);
    EXPECT_EQ(wflw_right_eye[7], 67);
    
    auto wflw_left_eye = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYE, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_left_eye.size(), 8); // Indices 68-75
    EXPECT_EQ(wflw_left_eye[0], 68);
    EXPECT_EQ(wflw_left_eye[7], 75);
    
    auto wflw_outer_mouth = LandmarkConverter::getRegionIndices(FacialRegion::OUTER_MOUTH, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_outer_mouth.size(), 12); // Indices 76-87
    EXPECT_EQ(wflw_outer_mouth[0], 76);
    EXPECT_EQ(wflw_outer_mouth[11], 87);
    
    auto wflw_inner_mouth = LandmarkConverter::getRegionIndices(FacialRegion::INNER_MOUTH, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_inner_mouth.size(), 10); // Indices 88-97
    EXPECT_EQ(wflw_inner_mouth[0], 88);
    EXPECT_EQ(wflw_inner_mouth[9], 97);
    
    // Test all PFLD regions
    auto pfld_jawline = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_jawline.size(), 17); // Indices 0-16
    EXPECT_EQ(pfld_jawline[0], 0);
    EXPECT_EQ(pfld_jawline[16], 16);
    
    auto pfld_right_eyebrow = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYEBROW, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_right_eyebrow.size(), 5); // Indices 17-21
    EXPECT_EQ(pfld_right_eyebrow[0], 17);
    EXPECT_EQ(pfld_right_eyebrow[4], 21);
    
    auto pfld_left_eyebrow = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYEBROW, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_left_eyebrow.size(), 5); // Indices 22-26
    EXPECT_EQ(pfld_left_eyebrow[0], 22);
    EXPECT_EQ(pfld_left_eyebrow[4], 26);
    
    auto pfld_nose = LandmarkConverter::getRegionIndices(FacialRegion::NOSE_BRIDGE, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_nose.size(), 9); // Indices 27-35
    EXPECT_EQ(pfld_nose[0], 27);
    EXPECT_EQ(pfld_nose[8], 35);
    
    auto pfld_right_eye = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYE, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_right_eye.size(), 6); // Indices 36-41
    EXPECT_EQ(pfld_right_eye[0], 36);
    EXPECT_EQ(pfld_right_eye[5], 41);
    
    auto pfld_left_eye = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYE, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_left_eye.size(), 6); // Indices 42-47
    EXPECT_EQ(pfld_left_eye[0], 42);
    EXPECT_EQ(pfld_left_eye[5], 47);
    
    auto pfld_outer_mouth = LandmarkConverter::getRegionIndices(FacialRegion::OUTER_MOUTH, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_outer_mouth.size(), 12); // Indices 48-59
    EXPECT_EQ(pfld_outer_mouth[0], 48);
    EXPECT_EQ(pfld_outer_mouth[11], 59);
    
    auto pfld_inner_mouth = LandmarkConverter::getRegionIndices(FacialRegion::INNER_MOUTH, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_inner_mouth.size(), 8); // Indices 60-67
    EXPECT_EQ(pfld_inner_mouth[0], 60);
    EXPECT_EQ(pfld_inner_mouth[7], 67);
    
    // Test invalid/unknown region (should return empty)
    auto unknown_region = LandmarkConverter::getRegionIndices(static_cast<FacialRegion>(999), LandmarkFormat::WFLW_98);
    EXPECT_TRUE(unknown_region.empty());
    
    // Test invalid format (should return empty)
    auto invalid_format = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, static_cast<LandmarkFormat>(999));
    EXPECT_TRUE(invalid_format.empty());
}

// Test conversion error handling and edge cases
TEST_F(LandmarkConverterTest, ConversionErrorHandling)
{
    // Test PFLD to WFLW with wrong number of landmarks
    std::vector<FaceLandmark> wrong_size_pfld(50);
    for (int i = 0; i < 50; ++i)
    {
        wrong_size_pfld[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    
    EXPECT_THROW(LandmarkConverter::pfldToWflw(wrong_size_pfld), std::invalid_argument);
    
    // Test WFLW to PFLD with wrong number of landmarks
    std::vector<FaceLandmark> wrong_size_wflw(50);
    for (int i = 0; i < 50; ++i)
    {
        wrong_size_wflw[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    
    EXPECT_THROW(LandmarkConverter::wflwToPfld(wrong_size_wflw), std::invalid_argument);
    
    // Test with empty vectors
    std::vector<FaceLandmark> empty_landmarks;
    EXPECT_THROW(LandmarkConverter::pfldToWflw(empty_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::wflwToPfld(empty_landmarks), std::invalid_argument);
    
    // Test with extremely large vectors (should also throw)
    std::vector<FaceLandmark> too_large_pfld(200);
    for (int i = 0; i < 200; ++i)
    {
        too_large_pfld[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    EXPECT_THROW(LandmarkConverter::pfldToWflw(too_large_pfld), std::invalid_argument);
}

// Test coordinate preservation and accuracy
TEST_F(LandmarkConverterTest, CoordinateAccuracy)
{
    // Create PFLD landmarks with specific known coordinates that should have direct mappings
    std::vector<FaceLandmark> precise_pfld;
    for (int i = 0; i < 106; ++i)
    {
        precise_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i), 
            math_utils::Point3D(i * 1.5, i * 2.5, i * 0.1)
        });
    }
    
    auto converted_wflw = LandmarkConverter::pfldToWflw(precise_pfld);
    EXPECT_EQ(converted_wflw.size(), 98);
    
    // Test that indices are sequential
    for (size_t i = 0; i < converted_wflw.size(); ++i)
    {
        EXPECT_EQ(converted_wflw[i].i, i);
    }
    
    // Test reverse conversion
    auto restored_pfld = LandmarkConverter::wflwToPfld(converted_wflw);
    EXPECT_EQ(restored_pfld.size(), 106);
    
    // Test that indices are sequential
    for (size_t i = 0; i < restored_pfld.size(); ++i)
    {
        EXPECT_EQ(restored_pfld[i].i, i);
    }
    
    // Check that some coordinates are reasonably preserved (not expecting perfect due to mapping)
    int preserved_count = 0;
    const double tolerance = 0.001;
    for (size_t i = 0; i < std::min(precise_pfld.size(), restored_pfld.size()); ++i)
    {
        if (std::abs(precise_pfld[i].p.x - restored_pfld[i].p.x) < tolerance &&
            std::abs(precise_pfld[i].p.y - restored_pfld[i].p.y) < tolerance)
        {
            preserved_count++;
        }
    }
    
    // Should preserve at least some coordinates due to direct mappings
    EXPECT_GT(preserved_count, 0) << "Should preserve some coordinates through round-trip conversion";
}

// Test boundary conditions and special values
TEST_F(LandmarkConverterTest, BoundaryConditions)
{
    // Test with landmarks at origin
    std::vector<FaceLandmark> origin_pfld;
    for (int i = 0; i < 106; ++i)
    {
        origin_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(0, 0, 0)
        });
    }
    
    auto origin_wflw = LandmarkConverter::pfldToWflw(origin_pfld);
    EXPECT_EQ(origin_wflw.size(), 98);
    
    // All should be at origin
    for (const auto& landmark : origin_wflw)
    {
        EXPECT_EQ(landmark.p.x, 0.0);
        EXPECT_EQ(landmark.p.y, 0.0);
        EXPECT_EQ(landmark.p.z, 0.0);
    }
    
    // Test with very large coordinates
    std::vector<FaceLandmark> large_pfld;
    for (int i = 0; i < 106; ++i)
    {
        large_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(1000000.0 + i, -1000000.0 - i, 1000.0)
        });
    }
    
    auto large_wflw = LandmarkConverter::pfldToWflw(large_pfld);
    EXPECT_EQ(large_wflw.size(), 98);
    
    // Should handle large values without issues
    bool has_large_values = false;
    for (const auto& landmark : large_wflw)
    {
        if (std::abs(landmark.p.x) > 1000.0 || std::abs(landmark.p.y) > 1000.0)
        {
            has_large_values = true;
            break;
        }
    }
    EXPECT_TRUE(has_large_values) << "Should preserve large coordinate values";
    
    // Test with negative coordinates
    std::vector<FaceLandmark> negative_pfld;
    for (int i = 0; i < 106; ++i)
    {
        negative_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(-i * 2.0, -i * 3.0, -i * 0.5)
        });
    }
    
    auto negative_wflw = LandmarkConverter::pfldToWflw(negative_pfld);
    EXPECT_EQ(negative_wflw.size(), 98);
    
    // Should handle negative values
    bool has_negative_values = false;
    for (const auto& landmark : negative_wflw)
    {
        if (landmark.p.x < 0.0 || landmark.p.y < 0.0)
        {
            has_negative_values = true;
            break;
        }
    }
    EXPECT_TRUE(has_negative_values) << "Should preserve negative coordinate values";
}

// Test mapping consistency and correctness
TEST_F(LandmarkConverterTest, MappingConsistency)
{
    // Test that mapping and reverse mapping are consistent
    // This indirectly tests the private mapping methods through the public interface
    
    // Create landmarks where we can verify specific mappings
    std::vector<FaceLandmark> test_pfld;
    for (int i = 0; i < 106; ++i)
    {
        // Use unique coordinates so we can track them
        test_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(1000 + i, 2000 + i, i)
        });
    }
    
    auto converted_wflw = LandmarkConverter::pfldToWflw(test_pfld);
    EXPECT_EQ(converted_wflw.size(), 98);
    
    // Verify that the first few landmarks have the expected mappings
    // Based on our mapping, PFLD indices 0-32 should map to WFLW indices 0-32 (jawline)
    for (int i = 0; i <= 16; ++i) // Only test first 17 due to extended jawline mapping
    {
        EXPECT_EQ(converted_wflw[i].p.x, test_pfld[i].p.x) << "Direct mapping should preserve coordinates for index " << i;
        EXPECT_EQ(converted_wflw[i].p.y, test_pfld[i].p.y) << "Direct mapping should preserve coordinates for index " << i;
        EXPECT_EQ(converted_wflw[i].p.z, test_pfld[i].p.z) << "Direct mapping should preserve coordinates for index " << i;
    }
}

// Test conversion with missing landmarks (interpolation testing through public interface)
TEST_F(LandmarkConverterTest, InterpolationThroughConversion)
{
    // Create WFLW landmarks and convert to PFLD (this tests interpolation of missing PFLD landmarks)
    std::vector<FaceLandmark> test_wflw;
    for (int i = 0; i < 98; ++i)
    {
        test_wflw.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(i * 1.5, i * 2.0, 0.0)
        });
    }
    
    auto converted_pfld = LandmarkConverter::wflwToPfld(test_wflw);
    EXPECT_EQ(converted_pfld.size(), 106);
    
    // Check that all landmarks have been populated (none should be at exact origin unless intended)
    int non_zero_count = 0;
    for (const auto& landmark : converted_pfld)
    {
        if (landmark.p.x != 0.0 || landmark.p.y != 0.0 || landmark.p.z != 0.0)
        {
            non_zero_count++;
        }
    }
    
    // Should have interpolated the missing landmarks, so most should be non-zero
    EXPECT_GT(non_zero_count, 90) << "Most landmarks should be interpolated and non-zero";
}

// Test numerical stability with extreme values
TEST_F(LandmarkConverterTest, NumericalStability)
{
    // Test with very small values
    std::vector<FaceLandmark> tiny_pfld;
    for (int i = 0; i < 106; ++i)
    {
        tiny_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(i * 1e-10, i * 1e-10, i * 1e-10)
        });
    }
    
    auto tiny_wflw = LandmarkConverter::pfldToWflw(tiny_pfld);
    EXPECT_EQ(tiny_wflw.size(), 98);
    
    // Should handle tiny values without numerical issues
    bool has_reasonable_values = true;
    for (const auto& landmark : tiny_wflw)
    {
        if (std::isnan(landmark.p.x) || std::isnan(landmark.p.y) || std::isnan(landmark.p.z) ||
            std::isinf(landmark.p.x) || std::isinf(landmark.p.y) || std::isinf(landmark.p.z))
        {
            has_reasonable_values = false;
            break;
        }
    }
    EXPECT_TRUE(has_reasonable_values) << "Should handle tiny values without NaN or Inf";
    
    // Test with mixed positive and negative large values
    std::vector<FaceLandmark> mixed_pfld;
    for (int i = 0; i < 106; ++i)
    {
        double sign = (i % 2 == 0) ? 1.0 : -1.0;
        mixed_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(sign * i * 1000.0, -sign * i * 500.0, sign * i * 10.0)
        });
    }
    
    auto mixed_wflw = LandmarkConverter::pfldToWflw(mixed_pfld);
    EXPECT_EQ(mixed_wflw.size(), 98);
    
    // Should handle mixed large values
    bool has_mixed_values = false;
    bool has_positive = false, has_negative = false;
    for (const auto& landmark : mixed_wflw)
    {
        if (landmark.p.x > 0) has_positive = true;
        if (landmark.p.x < 0) has_negative = true;
    }
    has_mixed_values = has_positive && has_negative;
    EXPECT_TRUE(has_mixed_values) << "Should preserve mixed positive and negative values";
}

// Test sequential index integrity
TEST_F(LandmarkConverterTest, SequentialIndexIntegrity)
{
    auto converted_wflw = LandmarkConverter::pfldToWflw(pfld_landmarks_);
    
    // Check that indices are sequential from 0 to 97
    for (size_t i = 0; i < converted_wflw.size(); ++i)
    {
        EXPECT_EQ(converted_wflw[i].i, i) << "WFLW landmarks should have sequential indices";
    }
    
    auto converted_pfld = LandmarkConverter::wflwToPfld(wflw_landmarks_);
    
    // Check that indices are sequential from 0 to 105
    for (size_t i = 0; i < converted_pfld.size(); ++i)
    {
        EXPECT_EQ(converted_pfld[i].i, i) << "PFLD landmarks should have sequential indices";
    }
}

// Test that conversions maintain reasonable geometric relationships
TEST_F(LandmarkConverterTest, GeometricRelationshipPreservation)
{
    // Create landmarks with known geometric relationships (e.g., a simple face structure)
    std::vector<FaceLandmark> geometric_pfld;
    for (int i = 0; i < 106; ++i)
    {
        // Create a simple pattern where landmarks form recognizable geometric relationships
        double angle = (i * 2.0 * M_PI) / 106.0; // Distribute around a circle
        double radius = 100.0 + (i % 10) * 5.0; // Varying radius
        
        geometric_pfld.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(
                radius * std::cos(angle) + 200.0, // Center at (200, 200)
                radius * std::sin(angle) + 200.0,
                i * 0.1
            )
        });
    }
    
    auto converted_wflw = LandmarkConverter::pfldToWflw(geometric_pfld);
    EXPECT_EQ(converted_wflw.size(), 98);
    
    // Check that the converted landmarks maintain reasonable geometric properties
    // Calculate centroid of converted landmarks
    double centroid_x = 0.0, centroid_y = 0.0;
    for (const auto& landmark : converted_wflw)
    {
        centroid_x += landmark.p.x;
        centroid_y += landmark.p.y;
    }
    centroid_x /= converted_wflw.size();
    centroid_y /= converted_wflw.size();
    
    // Centroid should be reasonably close to our original center (200, 200)
    EXPECT_NEAR(centroid_x, 200.0, 50.0) << "Converted landmarks should maintain approximate centroid";
    EXPECT_NEAR(centroid_y, 200.0, 50.0) << "Converted landmarks should maintain approximate centroid";
    
    // Check that landmarks are distributed around the centroid (not all collapsed to a point)
    double max_distance = 0.0;
    for (const auto& landmark : converted_wflw)
    {
        double distance = std::sqrt(
            (landmark.p.x - centroid_x) * (landmark.p.x - centroid_x) +
            (landmark.p.y - centroid_y) * (landmark.p.y - centroid_y)
        );
        max_distance = std::max(max_distance, distance);
    }
    
    EXPECT_GT(max_distance, 10.0) << "Converted landmarks should maintain reasonable spread";
}

// Test extract key landmarks with all formats
TEST_F(LandmarkConverterTest, ExtractKeyLandmarksComprehensive)
{
    // Test PFLD key landmarks
    auto pfld_key = LandmarkConverter::extractKeyLandmarks(pfld_landmarks_, LandmarkFormat::PFLD_106);
    EXPECT_EQ(pfld_key.size(), 5);
    for (size_t i = 0; i < pfld_key.size(); ++i)
    {
        EXPECT_EQ(pfld_key[i].i, i);
    }
    
    // Test WFLW key landmarks
    auto wflw_key = LandmarkConverter::extractKeyLandmarks(wflw_landmarks_, LandmarkFormat::WFLW_98);
    EXPECT_EQ(wflw_key.size(), 5);
    for (size_t i = 0; i < wflw_key.size(); ++i)
    {
        EXPECT_EQ(wflw_key[i].i, i);
    }
    
    // Test SCRFD key landmarks (should be identity)
    auto scrfd_key = LandmarkConverter::extractKeyLandmarks(scrfd_landmarks_, LandmarkFormat::SCRFD_5);
    EXPECT_EQ(scrfd_key.size(), 5);
    for (size_t i = 0; i < scrfd_key.size(); ++i)
    {
        EXPECT_EQ(scrfd_key[i].i, scrfd_landmarks_[i].i);
        EXPECT_EQ(scrfd_key[i].p.x, scrfd_landmarks_[i].p.x);
        EXPECT_EQ(scrfd_key[i].p.y, scrfd_landmarks_[i].p.y);
    }
    
    // Test with insufficient landmarks (PFLD)
    std::vector<FaceLandmark> small_pfld(50);
    for (int i = 0; i < 50; ++i)
    {
        small_pfld[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    auto small_pfld_key = LandmarkConverter::extractKeyLandmarks(small_pfld, LandmarkFormat::PFLD_106);
    EXPECT_TRUE(small_pfld_key.empty());
    
    // Test with insufficient landmarks (WFLW)
    std::vector<FaceLandmark> small_wflw(50);
    for (int i = 0; i < 50; ++i)
    {
        small_wflw[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    auto small_wflw_key = LandmarkConverter::extractKeyLandmarks(small_wflw, LandmarkFormat::WFLW_98);
    EXPECT_TRUE(small_wflw_key.empty());
    
    // Test with insufficient landmarks (SCRFD)
    std::vector<FaceLandmark> small_scrfd(3);
    for (int i = 0; i < 3; ++i)
    {
        small_scrfd[i] = FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(i, i, 0)};
    }
    auto small_scrfd_key = LandmarkConverter::extractKeyLandmarks(small_scrfd, LandmarkFormat::SCRFD_5);
    EXPECT_TRUE(small_scrfd_key.empty());
    
    // Test with invalid format
    auto invalid_key = LandmarkConverter::extractKeyLandmarks(pfld_landmarks_, static_cast<LandmarkFormat>(999));
    EXPECT_TRUE(invalid_key.empty());
}

// Performance test for conversion speed
TEST_F(LandmarkConverterTest, ConversionPerformance)
{
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform multiple conversions
    for (int i = 0; i < 1000; ++i)
    {
        auto wflw_converted = LandmarkConverter::pfldToWflw(pfld_landmarks_);
        auto pfld_converted = LandmarkConverter::wflwToPfld(wflw_landmarks_);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should complete 2000 conversions in reasonable time (less than 1 second)
    EXPECT_LT(duration.count(), 1000) << "Conversion should be fast enough for real-time use";
}
