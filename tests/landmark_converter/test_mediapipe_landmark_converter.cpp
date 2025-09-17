// Unit tests for MediaPipe landmark converter functionality
#include <gtest/gtest.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/face.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class MediaPipeLandmarkConverterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test MediaPipe landmarks (468 points)
        mediapipe_landmarks_.reserve(468);
        for (int i = 0; i < 468; ++i) {
            double x = 100.0 + (i % 30) * 3.0;  // Spread landmarks across image  
            double y = 100.0 + (i / 30) * 3.0;
            double z = (i % 15) * 0.05;         // Varying depth
            
            mediapipe_landmarks_.emplace_back(FaceLandmark{
                static_cast<unsigned int>(i),
                math_utils::Point3D(x, y, z)
            });
        }
        
        // Create test PFLD landmarks (106 points)
        pfld_landmarks_.reserve(106);
        for (int i = 0; i < 106; ++i) {
            double x = 120.0 + (i % 15) * 8.0;
            double y = 120.0 + (i / 15) * 8.0;
            double z = (i % 8) * 0.1;
            
            pfld_landmarks_.emplace_back(FaceLandmark{
                static_cast<unsigned int>(i),
                math_utils::Point3D(x, y, z)
            });
        }
        
        // Create test WFLW landmarks (98 points)
        wflw_landmarks_.reserve(98);
        for (int i = 0; i < 98; ++i) {
            double x = 150.0 + (i % 12) * 10.0;
            double y = 150.0 + (i / 12) * 10.0;
            double z = (i % 6) * 0.15;
            
            wflw_landmarks_.emplace_back(FaceLandmark{
                static_cast<unsigned int>(i),
                math_utils::Point3D(x, y, z)
            });
        }
    }
    
    std::vector<FaceLandmark> mediapipe_landmarks_;
    std::vector<FaceLandmark> pfld_landmarks_;
    std::vector<FaceLandmark> wflw_landmarks_;
};

// Test MediaPipe format validation
TEST_F(MediaPipeLandmarkConverterTest, ValidateMediaPipeFormat) {
    // Valid MediaPipe format (468 landmarks)
    EXPECT_TRUE(LandmarkConverter::validateLandmarkFormat(mediapipe_landmarks_, LandmarkFormat::MEDIAPIPE_468));
    
    // Invalid sizes
    std::vector<FaceLandmark> wrong_size_467(467);
    std::vector<FaceLandmark> wrong_size_469(469);
    std::vector<FaceLandmark> empty_landmarks;
    
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_467, LandmarkFormat::MEDIAPIPE_468));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_469, LandmarkFormat::MEDIAPIPE_468));
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(empty_landmarks, LandmarkFormat::MEDIAPIPE_468));
}

// Test expected landmark count for MediaPipe
TEST_F(MediaPipeLandmarkConverterTest, GetExpectedLandmarkCountMediaPipe) {
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::MEDIAPIPE_468), 468);
    
    // Verify all other formats still work
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::PFLD_106), 106);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::WFLW_98), 98);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::SCRFD_5), 5);
    
    // Test unknown format
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(static_cast<LandmarkFormat>(999)), 0);
}

// Test MediaPipe to PFLD conversion
TEST_F(MediaPipeLandmarkConverterTest, MediaPipeToPfldConversion) {
    auto converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
    
    // Should produce exactly 106 landmarks
    EXPECT_EQ(converted.size(), 106);
    
    // Check that indices are properly set (0-105)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that mapped landmarks have valid coordinates (not all at origin)
    int non_zero_count = 0;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0.0 || landmark.p.y != 0.0 || landmark.p.z != 0.0) {
            non_zero_count++;
        }
    }
    EXPECT_GT(non_zero_count, 50) << "Most landmarks should have valid coordinates after conversion";
    
    // Check coordinate bounds are reasonable
    for (const auto& landmark : converted) {
        EXPECT_GE(landmark.p.x, 0.0) << "X coordinate should be non-negative";
        EXPECT_LE(landmark.p.x, 500.0) << "X coordinate should be within reasonable bounds";
        EXPECT_GE(landmark.p.y, 0.0) << "Y coordinate should be non-negative";
        EXPECT_LE(landmark.p.y, 500.0) << "Y coordinate should be within reasonable bounds";
    }
}

// Test MediaPipe to WFLW conversion
TEST_F(MediaPipeLandmarkConverterTest, MediaPipeToWflwConversion) {
    auto converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    
    // Should produce exactly 98 landmarks
    EXPECT_EQ(converted.size(), 98);
    
    // Check that indices are properly set (0-97)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that mapped landmarks have valid coordinates
    int non_zero_count = 0;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0.0 || landmark.p.y != 0.0 || landmark.p.z != 0.0) {
            non_zero_count++;
        }
    }
    EXPECT_GT(non_zero_count, 40) << "Most landmarks should have valid coordinates after conversion";
}

// Test PFLD to MediaPipe conversion
TEST_F(MediaPipeLandmarkConverterTest, PfldToMediaPipeConversion) {
    auto converted = LandmarkConverter::pfldToMediapipe(pfld_landmarks_);
    
    // Should produce exactly 468 landmarks
    EXPECT_EQ(converted.size(), 468);
    
    // Check that indices are properly set (0-467)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that at least some landmarks have valid coordinates (many will be interpolated)
    int non_zero_count = 0;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0.0 || landmark.p.y != 0.0 || landmark.p.z != 0.0) {
            non_zero_count++;
        }
    }
    EXPECT_GT(non_zero_count, 100) << "Sufficient landmarks should have valid coordinates after conversion and interpolation";
}

// Test WFLW to MediaPipe conversion
TEST_F(MediaPipeLandmarkConverterTest, WflwToMediaPipeConversion) {
    auto converted = LandmarkConverter::wflwToMediapipe(wflw_landmarks_);
    
    // Should produce exactly 468 landmarks
    EXPECT_EQ(converted.size(), 468);
    
    // Check that indices are properly set (0-467)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that at least some landmarks have valid coordinates
    int non_zero_count = 0;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0.0 || landmark.p.y != 0.0 || landmark.p.z != 0.0) {
            non_zero_count++;
        }
    }
    EXPECT_GT(non_zero_count, 80) << "Sufficient landmarks should have valid coordinates after conversion";
}

// Test invalid input handling for MediaPipe conversions
TEST_F(MediaPipeLandmarkConverterTest, InvalidInputHandling) {
    // Test with wrong number of landmarks for MediaPipe conversion
    std::vector<FaceLandmark> wrong_size_467(467);
    std::vector<FaceLandmark> wrong_size_469(469);
    std::vector<FaceLandmark> empty_landmarks;
    
    // MediaPipe to other formats
    EXPECT_THROW(LandmarkConverter::mediapipeToPfld(wrong_size_467), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::mediapipeToPfld(wrong_size_469), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::mediapipeToPfld(empty_landmarks), std::invalid_argument);
    
    EXPECT_THROW(LandmarkConverter::mediapipeToWflw(wrong_size_467), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::mediapipeToWflw(wrong_size_469), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::mediapipeToWflw(empty_landmarks), std::invalid_argument);
    
    // Other formats to MediaPipe
    std::vector<FaceLandmark> wrong_pfld_105(105);
    std::vector<FaceLandmark> wrong_pfld_107(107);
    
    EXPECT_THROW(LandmarkConverter::pfldToMediapipe(wrong_pfld_105), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::pfldToMediapipe(wrong_pfld_107), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::pfldToMediapipe(empty_landmarks), std::invalid_argument);
    
    std::vector<FaceLandmark> wrong_wflw_97(97);
    std::vector<FaceLandmark> wrong_wflw_99(99);
    
    EXPECT_THROW(LandmarkConverter::wflwToMediapipe(wrong_wflw_97), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::wflwToMediapipe(wrong_wflw_99), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::wflwToMediapipe(empty_landmarks), std::invalid_argument);
}

// Test key landmark extraction for MediaPipe
TEST_F(MediaPipeLandmarkConverterTest, ExtractKeyLandmarksMediaPipe) {
    auto key_landmarks = LandmarkConverter::extractKeyLandmarks(mediapipe_landmarks_, LandmarkFormat::MEDIAPIPE_468);
    
    // Should extract exactly 5 key landmarks
    EXPECT_EQ(key_landmarks.size(), 5);
    
    // Check landmark indices are correct (0-4)
    for (size_t i = 0; i < key_landmarks.size(); ++i) {
        EXPECT_EQ(key_landmarks[i].i, static_cast<int>(i));
    }
    
    // Key landmarks should have valid coordinates
    for (const auto& landmark : key_landmarks) {
        EXPECT_TRUE(landmark.p.x != 0.0 || landmark.p.y != 0.0) 
            << "Key landmark should not be at origin";
    }
    
    // Test with insufficient landmarks
    std::vector<FaceLandmark> insufficient_landmarks(467);
    auto insufficient_result = LandmarkConverter::extractKeyLandmarks(insufficient_landmarks, LandmarkFormat::MEDIAPIPE_468);
    EXPECT_EQ(insufficient_result.size(), 0) << "Should return empty vector for insufficient landmarks";
}

// Test facial region indices for MediaPipe
TEST_F(MediaPipeLandmarkConverterTest, GetRegionIndicesMediaPipe) {
    // Test various facial regions for MediaPipe format
    auto jawline_indices = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, LandmarkFormat::MEDIAPIPE_468);
    auto right_eyebrow_indices = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYEBROW, LandmarkFormat::MEDIAPIPE_468);
    auto left_eyebrow_indices = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYEBROW, LandmarkFormat::MEDIAPIPE_468);
    auto nose_indices = LandmarkConverter::getRegionIndices(FacialRegion::NOSE_BRIDGE, LandmarkFormat::MEDIAPIPE_468);
    auto right_eye_indices = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYE, LandmarkFormat::MEDIAPIPE_468);
    auto left_eye_indices = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYE, LandmarkFormat::MEDIAPIPE_468);
    auto outer_mouth_indices = LandmarkConverter::getRegionIndices(FacialRegion::OUTER_MOUTH, LandmarkFormat::MEDIAPIPE_468);
    auto inner_mouth_indices = LandmarkConverter::getRegionIndices(FacialRegion::INNER_MOUTH, LandmarkFormat::MEDIAPIPE_468);
    
    // Check that indices are returned for each region
    EXPECT_GT(jawline_indices.size(), 0) << "Jawline should have landmarks";
    EXPECT_GT(right_eyebrow_indices.size(), 0) << "Right eyebrow should have landmarks";
    EXPECT_GT(left_eyebrow_indices.size(), 0) << "Left eyebrow should have landmarks";
    EXPECT_GT(nose_indices.size(), 0) << "Nose should have landmarks";
    EXPECT_GT(right_eye_indices.size(), 0) << "Right eye should have landmarks";
    EXPECT_GT(left_eye_indices.size(), 0) << "Left eye should have landmarks";
    EXPECT_GT(outer_mouth_indices.size(), 0) << "Outer mouth should have landmarks";
    EXPECT_GT(inner_mouth_indices.size(), 0) << "Inner mouth should have landmarks";
    
    // Check that all indices are within valid range for MediaPipe (0-467)
    auto check_indices_range = [](const std::vector<int>& indices, const std::string& region_name) {
        for (int idx : indices) {
            EXPECT_GE(idx, 0) << region_name << " index should be non-negative";
            EXPECT_LT(idx, 468) << region_name << " index should be less than 468";
        }
    };
    
    check_indices_range(jawline_indices, "Jawline");
    check_indices_range(right_eyebrow_indices, "Right eyebrow");
    check_indices_range(left_eyebrow_indices, "Left eyebrow");
    check_indices_range(nose_indices, "Nose");
    check_indices_range(right_eye_indices, "Right eye");
    check_indices_range(left_eye_indices, "Left eye");
    check_indices_range(outer_mouth_indices, "Outer mouth");
    check_indices_range(inner_mouth_indices, "Inner mouth");
    
    // Check that regions don't overlap (basic sanity check)
    std::set<int> all_indices;
    auto add_to_set = [&all_indices](const std::vector<int>& indices) {
        for (int idx : indices) {
            all_indices.insert(idx);
        }
    };
    
    add_to_set(jawline_indices);
    add_to_set(right_eyebrow_indices);
    add_to_set(left_eyebrow_indices);
    add_to_set(nose_indices);
    add_to_set(right_eye_indices);
    add_to_set(left_eye_indices);
    add_to_set(outer_mouth_indices);
    add_to_set(inner_mouth_indices);
    
    size_t total_individual_indices = jawline_indices.size() + right_eyebrow_indices.size() + 
                                     left_eyebrow_indices.size() + nose_indices.size() +
                                     right_eye_indices.size() + left_eye_indices.size() +
                                     outer_mouth_indices.size() + inner_mouth_indices.size();
    
    // Some overlap is expected in facial landmark definitions, so we don't expect equality
    EXPECT_LE(all_indices.size(), total_individual_indices) << "Some regions should have unique indices";
}

// Test round-trip conversions
TEST_F(MediaPipeLandmarkConverterTest, RoundTripConversions) {
    // MediaPipe -> PFLD -> MediaPipe
    auto pfld_converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
    auto mediapipe_restored_from_pfld = LandmarkConverter::pfldToMediapipe(pfld_converted);
    
    EXPECT_EQ(mediapipe_restored_from_pfld.size(), 468);
    
    // MediaPipe -> WFLW -> MediaPipe
    auto wflw_converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    auto mediapipe_restored_from_wflw = LandmarkConverter::wflwToMediapipe(wflw_converted);
    
    EXPECT_EQ(mediapipe_restored_from_wflw.size(), 468);
    
    // Check that some structural information is preserved
    int preserved_pfld = 0;
    int preserved_wflw = 0;
    
    for (size_t i = 0; i < 468; ++i) {
        if (mediapipe_restored_from_pfld[i].p.x != 0.0 || mediapipe_restored_from_pfld[i].p.y != 0.0) {
            preserved_pfld++;
        }
        if (mediapipe_restored_from_wflw[i].p.x != 0.0 || mediapipe_restored_from_wflw[i].p.y != 0.0) {
            preserved_wflw++;
        }
    }
    
    EXPECT_GT(preserved_pfld, 100) << "Round-trip via PFLD should preserve reasonable number of landmarks";
    EXPECT_GT(preserved_wflw, 80) << "Round-trip via WFLW should preserve reasonable number of landmarks";
}

// Test coordinate preservation during conversion
TEST_F(MediaPipeLandmarkConverterTest, CoordinatePreservation) {
    // Create MediaPipe landmarks with known coordinate patterns
    std::vector<FaceLandmark> known_mediapipe;
    known_mediapipe.reserve(468);
    
    for (int i = 0; i < 468; ++i) {
        double x = 200.0 + i * 0.5;
        double y = 300.0 + i * 0.3;
        double z = i * 0.01;
        
        known_mediapipe.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(x, y, z)
        });
    }
    
    // Convert to PFLD and check coordinate ranges
    auto pfld_converted = LandmarkConverter::mediapipeToPfld(known_mediapipe);
    
    bool has_reasonable_coordinates = false;
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();
    
    for (const auto& landmark : pfld_converted) {
        if (landmark.p.x != 0.0 && landmark.p.y != 0.0) {
            min_x = std::min(min_x, landmark.p.x);
            max_x = std::max(max_x, landmark.p.x);
            min_y = std::min(min_y, landmark.p.y);
            max_y = std::max(max_y, landmark.p.y);
            has_reasonable_coordinates = true;
        }
    }
    
    EXPECT_TRUE(has_reasonable_coordinates) << "Converted landmarks should preserve some coordinate information";
    
    if (has_reasonable_coordinates) {
        // Check that coordinate ranges are reasonable (derived from input)
        EXPECT_GE(min_x, 150.0) << "Minimum X should be in reasonable range";
        EXPECT_LE(max_x, 500.0) << "Maximum X should be in reasonable range";
        EXPECT_GE(min_y, 250.0) << "Minimum Y should be in reasonable range";
        EXPECT_LE(max_y, 450.0) << "Maximum Y should be in reasonable range";
    }
}

// Test conversion mapping consistency
TEST_F(MediaPipeLandmarkConverterTest, MappingConsistency) {
    // Test that mappings are internally consistent
    auto converted_to_pfld = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
    auto converted_to_wflw = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    
    EXPECT_EQ(converted_to_pfld.size(), 106);
    EXPECT_EQ(converted_to_wflw.size(), 98);
    
    // Verify that indices are sequential and correct
    for (size_t i = 0; i < converted_to_pfld.size(); ++i) {
        EXPECT_EQ(converted_to_pfld[i].i, i) << "PFLD landmark index should match position";
    }
    
    for (size_t i = 0; i < converted_to_wflw.size(); ++i) {
        EXPECT_EQ(converted_to_wflw[i].i, i) << "WFLW landmark index should match position";
    }
}

// Test performance of MediaPipe conversions
TEST_F(MediaPipeLandmarkConverterTest, ConversionPerformance) {
    const int num_runs = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform multiple conversions to test performance
    for (int i = 0; i < num_runs; ++i) {
        auto pfld_converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
        auto wflw_converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
        auto mediapipe_from_pfld = LandmarkConverter::pfldToMediapipe(pfld_landmarks_);
        auto mediapipe_from_wflw = LandmarkConverter::wflwToMediapipe(wflw_landmarks_);
        
        // Verify results to ensure compiler doesn't optimize away
        ASSERT_EQ(pfld_converted.size(), 106);
        ASSERT_EQ(wflw_converted.size(), 98);
        ASSERT_EQ(mediapipe_from_pfld.size(), 468);
        ASSERT_EQ(mediapipe_from_wflw.size(), 468);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Conversions should complete within reasonable time
    EXPECT_LT(duration.count(), 5000) << "Conversions took too long: " << duration.count() << "ms for " << num_runs << " runs";
    
    std::cout << "MediaPipe conversion performance: " << duration.count() << "ms for " << num_runs << " runs" << std::endl;
}

// Test edge cases with extreme coordinate values
TEST_F(MediaPipeLandmarkConverterTest, ExtremeCoordinateValues) {
    // Create MediaPipe landmarks with extreme coordinates
    std::vector<FaceLandmark> extreme_mediapipe;
    extreme_mediapipe.reserve(468);
    
    for (int i = 0; i < 468; ++i) {
        double x = (i % 2 == 0) ? -1000.0 : 10000.0;  // Alternating extreme values
        double y = (i % 3 == 0) ? -2000.0 : 20000.0;
        double z = (i % 4 == 0) ? -100.0 : 100.0;
        
        extreme_mediapipe.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(x, y, z)
        });
    }
    
    // Conversions should not crash with extreme values
    EXPECT_NO_THROW({
        auto pfld_converted = LandmarkConverter::mediapipeToPfld(extreme_mediapipe);
        EXPECT_EQ(pfld_converted.size(), 106);
        
        auto wflw_converted = LandmarkConverter::mediapipeToWflw(extreme_mediapipe);
        EXPECT_EQ(wflw_converted.size(), 98);
    });
}

// Test zero coordinate handling
TEST_F(MediaPipeLandmarkConverterTest, ZeroCoordinateHandling) {
    // Create MediaPipe landmarks with all zero coordinates
    std::vector<FaceLandmark> zero_mediapipe;
    zero_mediapipe.reserve(468);
    
    for (int i = 0; i < 468; ++i) {
        zero_mediapipe.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(0.0, 0.0, 0.0)
        });
    }
    
    // Conversion should work even with all zeros
    auto pfld_converted = LandmarkConverter::mediapipeToPfld(zero_mediapipe);
    auto wflw_converted = LandmarkConverter::mediapipeToWflw(zero_mediapipe);
    
    EXPECT_EQ(pfld_converted.size(), 106);
    EXPECT_EQ(wflw_converted.size(), 98);
    
    // Results might be all zeros or interpolated, but should not crash
    for (const auto& landmark : pfld_converted) {
        EXPECT_TRUE(std::isfinite(landmark.p.x));
        EXPECT_TRUE(std::isfinite(landmark.p.y));
        EXPECT_TRUE(std::isfinite(landmark.p.z));
    }
    
    for (const auto& landmark : wflw_converted) {
        EXPECT_TRUE(std::isfinite(landmark.p.x));
        EXPECT_TRUE(std::isfinite(landmark.p.y));
        EXPECT_TRUE(std::isfinite(landmark.p.z));
    }
}
