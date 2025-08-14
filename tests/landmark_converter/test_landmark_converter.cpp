/**
 * @file test_landmark_converter.cpp
 * @brief Unit tests for LandmarkConverter functionality
 */

#include <gtest/gtest.h>
#include <vector>
#include <memory>

#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class LandmarkConverterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create sample PFLD 106-point landmarks
        pfld_landmarks_.clear();
        pfld_landmarks_.reserve(106);
        for (int i = 0; i < 106; ++i)
        {
            pfld_landmarks_.emplace_back(FaceLandmark{
                i, 
                math_utils::Point3D(100 + i * 2.0, 200 + i * 1.5, 0.0)
            });
        }

        // Create sample WFLW 98-point landmarks
        wflw_landmarks_.clear();
        wflw_landmarks_.reserve(98);
        for (int i = 0; i < 98; ++i)
        {
            wflw_landmarks_.emplace_back(FaceLandmark{
                i,
                math_utils::Point3D(150 + i * 1.8, 250 + i * 1.2, 0.0)
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

// Test format validation
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
}

// Test expected landmark counts
TEST_F(LandmarkConverterTest, GetExpectedLandmarkCount)
{
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::PFLD_106), 106);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::WFLW_98), 98);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::SCRFD_5), 5);
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
        test_pfld.emplace_back(FaceLandmark{i, math_utils::Point3D(i * 10.0, i * 5.0, i * 2.0)});
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

// Test that conversion mappings are consistent
TEST_F(LandmarkConverterTest, MappingConsistency)
{
    // Test that the same input always produces the same output
    auto result1 = LandmarkConverter::pfldToWflw(pfld_landmarks_);
    auto result2 = LandmarkConverter::pfldToWflw(pfld_landmarks_);
    
    EXPECT_EQ(result1.size(), result2.size());
    for (size_t i = 0; i < result1.size(); ++i)
    {
        EXPECT_EQ(result1[i].i, result2[i].i);
        EXPECT_DOUBLE_EQ(result1[i].p.x, result2[i].p.x);
        EXPECT_DOUBLE_EQ(result1[i].p.y, result2[i].p.y);
        EXPECT_DOUBLE_EQ(result1[i].p.z, result2[i].p.z);
    }
}
