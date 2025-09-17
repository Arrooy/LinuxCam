// Unit test for MediaPipe landmark conversions
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <iostream>
#include <onnxruntime_cxx_api.h>
#include "config.hpp"
#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class MediaPipeLandmarkConversionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test MediaPipe landmarks (468 points)
        mediapipe_landmarks_.reserve(468);
        for (int i = 0; i < 468; ++i) {
            double x = 100.0 + (i % 20) * 5.0;  // Spread landmarks across image
            double y = 100.0 + (i / 20) * 5.0;
            double z = (i % 10) * 0.1;           // Varying depth
            
            mediapipe_landmarks_.emplace_back(FaceLandmark{
                static_cast<unsigned int>(i),
                math_utils::Point3D(x, y, z)
            });
        }
        
        // Create test PFLD landmarks (106 points)
        pfld_landmarks_.reserve(106);
        for (int i = 0; i < 106; ++i) {
            double x = 120.0 + (i % 15) * 6.0;
            double y = 120.0 + (i / 15) * 6.0;
            double z = (i % 8) * 0.15;
            
            pfld_landmarks_.emplace_back(FaceLandmark{
                static_cast<unsigned int>(i),
                math_utils::Point3D(x, y, z)
            });
        }
        
        // Create test WFLW landmarks (98 points)
        wflw_landmarks_.reserve(98);
        for (int i = 0; i < 98; ++i) {
            double x = 140.0 + (i % 12) * 7.0;
            double y = 140.0 + (i / 12) * 7.0;
            double z = (i % 6) * 0.2;
            
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
TEST_F(MediaPipeLandmarkConversionTest, MediaPipeFormatValidation) {
    // Valid MediaPipe format (468 landmarks)
    EXPECT_TRUE(LandmarkConverter::validateLandmarkFormat(mediapipe_landmarks_, LandmarkFormat::MEDIAPIPE_468));
    
    // Invalid sizes
    std::vector<FaceLandmark> wrong_size_landmarks(467);
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_landmarks, LandmarkFormat::MEDIAPIPE_468));
    
    wrong_size_landmarks.resize(469);
    EXPECT_FALSE(LandmarkConverter::validateLandmarkFormat(wrong_size_landmarks, LandmarkFormat::MEDIAPIPE_468));
}

// Test expected landmark count for MediaPipe
TEST_F(MediaPipeLandmarkConversionTest, MediaPipeExpectedLandmarkCount) {
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::MEDIAPIPE_468), 468);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::PFLD_106), 106);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::WFLW_98), 98);
    EXPECT_EQ(LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat::SCRFD_5), 5);
}

// Test MediaPipe to PFLD conversion
TEST_F(MediaPipeLandmarkConversionTest, MediaPipeToPfldConversion) {
    auto converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
    
    // Should produce exactly 106 landmarks
    EXPECT_EQ(converted.size(), 106);
    
    // Check that indices are properly set (0-105)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that at least some landmarks have valid coordinates
    bool has_non_zero = false;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0) {
            has_non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(has_non_zero) << "All converted landmarks are at origin";
}

// Test MediaPipe to WFLW conversion
TEST_F(MediaPipeLandmarkConversionTest, MediaPipeToWflwConversion) {
    auto converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    
    // Should produce exactly 98 landmarks
    EXPECT_EQ(converted.size(), 98);
    
    // Check that indices are properly set (0-97)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that at least some landmarks have valid coordinates
    bool has_non_zero = false;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0) {
            has_non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(has_non_zero) << "All converted landmarks are at origin";
}

// Test PFLD to MediaPipe conversion
TEST_F(MediaPipeLandmarkConversionTest, PfldToMediaPipeConversion) {
    auto converted = LandmarkConverter::pfldToMediapipe(pfld_landmarks_);
    
    // Should produce exactly 468 landmarks
    EXPECT_EQ(converted.size(), 468);
    
    // Check that indices are properly set (0-467)
    for (size_t i = 0; i < converted.size(); ++i) {
        EXPECT_EQ(converted[i].i, static_cast<int>(i));
    }
    
    // Check that at least some landmarks have valid coordinates (not all should be at origin)
    int non_zero_count = 0;
    for (const auto& landmark : converted) {
        if (landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0) {
            non_zero_count++;
        }
    }
    EXPECT_GT(non_zero_count, 50) << "Too few landmarks have valid coordinates";
}

// Test WFLW to MediaPipe conversion
TEST_F(MediaPipeLandmarkConversionTest, WflwToMediaPipeConversion) {
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
        if (landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0) {
            non_zero_count++;
        }
    }
    EXPECT_GT(non_zero_count, 40) << "Too few landmarks have valid coordinates";
}

// Test invalid input handling for MediaPipe conversions
TEST_F(MediaPipeLandmarkConversionTest, InvalidInputHandlingMediaPipe) {
    // Test with wrong number of landmarks for MediaPipe conversion
    std::vector<FaceLandmark> wrong_size_landmarks(467);
    
    EXPECT_THROW(LandmarkConverter::mediapipeToPfld(wrong_size_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::mediapipeToWflw(wrong_size_landmarks), std::invalid_argument);
    
    // Test with wrong number of landmarks for other formats
    std::vector<FaceLandmark> wrong_pfld_landmarks(105);
    std::vector<FaceLandmark> wrong_wflw_landmarks(97);
    
    EXPECT_THROW(LandmarkConverter::pfldToMediapipe(wrong_pfld_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::wflwToMediapipe(wrong_wflw_landmarks), std::invalid_argument);
}

// Test key landmark extraction for MediaPipe
TEST_F(MediaPipeLandmarkConversionTest, ExtractKeyLandmarksMediaPipe) {
    auto key_landmarks = LandmarkConverter::extractKeyLandmarks(mediapipe_landmarks_, LandmarkFormat::MEDIAPIPE_468);
    
    // Should extract exactly 5 key landmarks
    EXPECT_EQ(key_landmarks.size(), 5);
    
    // Check landmark indices are correct (0-4)
    for (size_t i = 0; i < key_landmarks.size(); ++i) {
        EXPECT_EQ(key_landmarks[i].i, static_cast<int>(i));
    }
    
    // Key landmarks should have valid coordinates
    for (const auto& landmark : key_landmarks) {
        EXPECT_TRUE(landmark.p.x != 0 || landmark.p.y != 0 || landmark.p.z != 0) 
            << "Key landmark should not be at origin";
    }
}

// Test facial region indices for MediaPipe
TEST_F(MediaPipeLandmarkConversionTest, GetRegionIndicesMediaPipe) {
    // Test various facial regions for MediaPipe format
    auto jawline_indices = LandmarkConverter::getRegionIndices(FacialRegion::JAWLINE, LandmarkFormat::MEDIAPIPE_468);
    auto right_eye_indices = LandmarkConverter::getRegionIndices(FacialRegion::RIGHT_EYE, LandmarkFormat::MEDIAPIPE_468);
    auto left_eye_indices = LandmarkConverter::getRegionIndices(FacialRegion::LEFT_EYE, LandmarkFormat::MEDIAPIPE_468);
    auto nose_indices = LandmarkConverter::getRegionIndices(FacialRegion::NOSE_BRIDGE, LandmarkFormat::MEDIAPIPE_468);
    auto mouth_indices = LandmarkConverter::getRegionIndices(FacialRegion::OUTER_MOUTH, LandmarkFormat::MEDIAPIPE_468);
    
    // Check that indices are returned for each region
    EXPECT_GT(jawline_indices.size(), 0) << "Jawline should have landmarks";
    EXPECT_GT(right_eye_indices.size(), 0) << "Right eye should have landmarks";
    EXPECT_GT(left_eye_indices.size(), 0) << "Left eye should have landmarks";
    EXPECT_GT(nose_indices.size(), 0) << "Nose should have landmarks";
    EXPECT_GT(mouth_indices.size(), 0) << "Mouth should have landmarks";
    
    // Check that all indices are within valid range for MediaPipe (0-467)
    auto check_indices_range = [](const std::vector<int>& indices, const std::string& region_name) {
        for (int idx : indices) {
            EXPECT_GE(idx, 0) << region_name << " index should be non-negative";
            EXPECT_LT(idx, 468) << region_name << " index should be less than 468";
        }
    };
    
    check_indices_range(jawline_indices, "Jawline");
    check_indices_range(right_eye_indices, "Right eye");
    check_indices_range(left_eye_indices, "Left eye");
    check_indices_range(nose_indices, "Nose");
    check_indices_range(mouth_indices, "Mouth");
}

// Test round-trip conversion MediaPipe -> PFLD -> MediaPipe
TEST_F(MediaPipeLandmarkConversionTest, RoundTripMediaPipePfld) {
    // MediaPipe -> PFLD -> MediaPipe
    auto pfld_converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
    auto mediapipe_restored = LandmarkConverter::pfldToMediapipe(pfld_converted);
    
    EXPECT_EQ(mediapipe_restored.size(), 468);
    
    // The round-trip won't be perfect due to format differences and interpolation
    // But key structural landmarks should be preserved reasonably well
    int preserved_landmarks = 0;
    for (size_t i = 0; i < 468; ++i) {
        if (mediapipe_restored[i].p.x != 0 || mediapipe_restored[i].p.y != 0) {
            preserved_landmarks++;
        }
    }
    
    // Expect at least 100 landmarks to be preserved in some form
    EXPECT_GT(preserved_landmarks, 100) << "Too few landmarks preserved in round-trip conversion";
}

// Test round-trip conversion MediaPipe -> WFLW -> MediaPipe
TEST_F(MediaPipeLandmarkConversionTest, RoundTripMediaPipeWflw) {
    // MediaPipe -> WFLW -> MediaPipe
    auto wflw_converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    auto mediapipe_restored = LandmarkConverter::wflwToMediapipe(wflw_converted);
    
    EXPECT_EQ(mediapipe_restored.size(), 468);
    
    // Check preservation
    int preserved_landmarks = 0;
    for (size_t i = 0; i < 468; ++i) {
        if (mediapipe_restored[i].p.x != 0 || mediapipe_restored[i].p.y != 0) {
            preserved_landmarks++;
        }
    }
    
    // Expect at least 80 landmarks to be preserved in some form
    EXPECT_GT(preserved_landmarks, 80) << "Too few landmarks preserved in round-trip conversion";
}

// Test conversion performance
TEST_F(MediaPipeLandmarkConversionTest, ConversionPerformance) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform multiple conversions
    for (int i = 0; i < 100; ++i) {
        auto pfld_converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
        auto wflw_converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Conversions should complete within reasonable time
    EXPECT_LT(duration.count(), 5000) << "Conversions took too long: " << duration.count() << "ms";
}

// Test coordinate preservation accuracy
TEST_F(MediaPipeLandmarkConversionTest, CoordinatePreservationAccuracy) {
    // Create MediaPipe landmarks with known coordinates
    std::vector<FaceLandmark> known_mediapipe;
    known_mediapipe.reserve(468);
    
    for (int i = 0; i < 468; ++i) {
        double x = 100.0 + i * 0.5;
        double y = 200.0 + i * 0.3;
        double z = i * 0.01;
        
        known_mediapipe.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(x, y, z)
        });
    }
    
    // Convert to PFLD and check that mapped landmarks preserve coordinates reasonably
    auto pfld_converted = LandmarkConverter::mediapipeToPfld(known_mediapipe);
    
    // Since we know the mapping, we can check specific correspondences
    // This is a basic check - in practice would need more sophisticated validation
    bool has_reasonable_coordinates = false;
    for (const auto& landmark : pfld_converted) {
        if (landmark.p.x >= 100.0 && landmark.p.x <= 400.0 &&
            landmark.p.y >= 200.0 && landmark.p.y <= 500.0) {
            has_reasonable_coordinates = true;
            break;
        }
    }
    
    EXPECT_TRUE(has_reasonable_coordinates) << "Converted landmarks should preserve coordinate ranges";
}

// Test empty landmarks handling
TEST_F(MediaPipeLandmarkConversionTest, EmptyLandmarksHandling) {
    std::vector<FaceLandmark> empty_landmarks;
    
    // Should throw for empty input
    EXPECT_THROW(LandmarkConverter::mediapipeToPfld(empty_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::mediapipeToWflw(empty_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::pfldToMediapipe(empty_landmarks), std::invalid_argument);
    EXPECT_THROW(LandmarkConverter::wflwToMediapipe(empty_landmarks), std::invalid_argument);
}

// Test coordinate bounds after conversion
TEST_F(MediaPipeLandmarkConversionTest, CoordinateBoundsAfterConversion) {
    auto pfld_converted = LandmarkConverter::mediapipeToPfld(mediapipe_landmarks_);
    auto wflw_converted = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    
    // Check that converted coordinates are within reasonable bounds
    for (const auto& landmark : pfld_converted) {
        EXPECT_GT(landmark.p.x, -1000.0) << "X coordinate out of reasonable bounds";
        EXPECT_LT(landmark.p.x, 2000.0) << "X coordinate out of reasonable bounds";
        EXPECT_GT(landmark.p.y, -1000.0) << "Y coordinate out of reasonable bounds";
        EXPECT_LT(landmark.p.y, 2000.0) << "Y coordinate out of reasonable bounds";
    }
    
    for (const auto& landmark : wflw_converted) {
        EXPECT_GT(landmark.p.x, -1000.0) << "X coordinate out of reasonable bounds";
        EXPECT_LT(landmark.p.x, 2000.0) << "X coordinate out of reasonable bounds";
        EXPECT_GT(landmark.p.y, -1000.0) << "Y coordinate out of reasonable bounds";
        EXPECT_LT(landmark.p.y, 2000.0) << "Y coordinate out of reasonable bounds";
    }
}
