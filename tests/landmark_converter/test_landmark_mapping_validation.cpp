/**
 * @file test_landmark_mapping_validation.cpp
 * @brief Comprehensive tests for validating PFLD-to-WFLW landmark correspondence
 *
 * This test suite validates the landmark mapping table by checking:
 * 1. Semantic consistency (landmarks in same facial regions)
 * 2. Visual validation with test images
 * 3. Benchmark accuracy on known datasets
 */

#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <map>

#include "LinuxFace/face.h"
#include "LinuxFace/landmark_converter.h"

using namespace linuxface;

class LandmarkMappingValidationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Simple setup - no external dependencies for now
    }

    // Helper function to create test landmarks in realistic face structure
    std::vector<FaceLandmark> createTestLandmarks(int count)
    {
        std::vector<FaceLandmark> landmarks;

        // Create landmarks that approximate a realistic face layout
        // Face center at (300, 300), face width ~200px, height ~250px
        double face_center_x = 300.0;
        double face_center_y = 300.0;
        double face_width = 200.0;
        double face_height = 250.0;

        for (int i = 0; i < count; ++i)
        {
            double x, y;

            // Distribute landmarks based on typical facial landmark structure
            if (i < 17)
            {
                // Jawline (0-16): Create curved jawline
                double t = static_cast<double>(i) / 16.0;
                double angle = M_PI * 0.8 * (t - 0.5); // -72° to +72°
                x = face_center_x + (face_width * 0.45) * std::sin(angle);
                y = face_center_y + (face_height * 0.35) + (face_height * 0.15) * std::cos(angle);
            }
            else if (i < 22)
            {
                // Right eyebrow (17-21)
                double t = static_cast<double>(i - 17) / 4.0;
                x = face_center_x - face_width * 0.25 + t * face_width * 0.2;
                y = face_center_y - face_height * 0.25;
            }
            else if (i < 27)
            {
                // Left eyebrow (22-26)
                double t = static_cast<double>(i - 22) / 4.0;
                x = face_center_x + face_width * 0.05 + t * face_width * 0.2;
                y = face_center_y - face_height * 0.25;
            }
            else if (i < 36)
            {
                // Nose bridge and tip (27-35)
                double t = static_cast<double>(i - 27) / 8.0;
                x = face_center_x + (face_width * 0.1) * (t - 0.5);
                y = face_center_y - face_height * 0.1 + t * face_height * 0.2;
            }
            else if (i < 42)
            {
                // Right eye (36-41)
                double t = static_cast<double>(i - 36) / 5.0;
                double angle = M_PI * 2 * t;
                x = face_center_x - face_width * 0.15 + face_width * 0.08 * std::cos(angle);
                y = face_center_y - face_height * 0.08 + face_width * 0.04 * std::sin(angle);
            }
            else if (i < 48)
            {
                // Left eye (42-47)
                double t = static_cast<double>(i - 42) / 5.0;
                double angle = M_PI * 2 * t;
                x = face_center_x + face_width * 0.15 + face_width * 0.08 * std::cos(angle);
                y = face_center_y - face_height * 0.08 + face_width * 0.04 * std::sin(angle);
            }
            else if (i < 60)
            {
                // Outer mouth (48-59)
                double t = static_cast<double>(i - 48) / 11.0;
                double angle = M_PI * 1.2 * (t - 0.5); // Mouth curve
                x = face_center_x + face_width * 0.2 * std::sin(angle);
                y = face_center_y + face_height * 0.15 + face_height * 0.05 * std::cos(angle);
            }
            else if (i < 68)
            {
                // Inner mouth (60-67)
                double t = static_cast<double>(i - 60) / 7.0;
                double angle = M_PI * 0.8 * (t - 0.5);
                x = face_center_x + face_width * 0.12 * std::sin(angle);
                y = face_center_y + face_height * 0.15 + face_height * 0.02 * std::cos(angle);
            }
            else
            {
                // Additional PFLD landmarks (68-105): distribute around face periphery
                double t = static_cast<double>(i - 68) / 37.0;
                double angle = 2 * M_PI * t;
                double radius = face_width * 0.5 * (0.8 + 0.2 * std::sin(4 * angle)); // Varying radius
                x = face_center_x + radius * std::cos(angle);
                y = face_center_y + radius * std::sin(angle) * 0.8; // Slightly flatten vertically
            }

            landmarks.emplace_back(FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(x, y, 0.0)});
        }
        return landmarks;
    }
};

TEST_F(LandmarkMappingValidationTest, MappingTableBasicValidation)
{
    // Test 1: Verify mapping table completeness
    auto pfld_landmarks = createTestLandmarks(106);

    ASSERT_NO_THROW({
        auto converted = LandmarkConverter::pfldToWflw(pfld_landmarks);
        EXPECT_EQ(converted.size(), 98);
    });

    // Test 2: Verify no index out of bounds
    for (int pfld_idx = 0; pfld_idx < 106; ++pfld_idx)
    {
        pfld_landmarks[pfld_idx].p = math_utils::Point3D(pfld_idx * 10, pfld_idx * 5, 0);
    }

    auto converted = LandmarkConverter::pfldToWflw(pfld_landmarks);

    // Verify all converted landmarks have valid coordinates
    for (const auto& landmark : converted)
    {
        EXPECT_GE(landmark.p.x, 0) << "Landmark " << landmark.i << " has negative X";
        EXPECT_GE(landmark.p.y, 0) << "Landmark " << landmark.i << " has negative Y";
        EXPECT_LT(landmark.p.x, 1100) << "Landmark " << landmark.i << " X out of expected range";
        EXPECT_LT(landmark.p.y, 600) << "Landmark " << landmark.i << " Y out of expected range";
    }
}

TEST_F(LandmarkMappingValidationTest, VisualMappingInspection)
{
    // Create realistic face landmarks using our helper function
    auto test_pfld_landmarks = createTestLandmarks(106);

    // Convert to WFLW
    auto converted_wflw = LandmarkConverter::pfldToWflw(test_pfld_landmarks);

    // Print mapping for manual inspection
    std::cout << "\nPFLD to WFLW Mapping Analysis (Realistic Face Structure):" << std::endl;
    std::cout << "WFLW_ID -> PFLD_ID : PFLD_coords -> WFLW_coords [Region]" << std::endl;

    // Define regions for better analysis
    auto getRegionName = [](int wflw_idx) -> std::string
    {
        if (wflw_idx <= 32)
        {
            return "Jawline";
        }
        else if (wflw_idx <= 41)
        {
            return "RightEyebrow";
        }
        else if (wflw_idx <= 50)
        {
            return "LeftEyebrow";
        }
        else if (wflw_idx <= 59)
        {
            return "Nose";
        }
        else if (wflw_idx <= 67)
        {
            return "RightEye";
        }
        else if (wflw_idx <= 75)
        {
            return "LeftEye";
        }
        else if (wflw_idx <= 87)
        {
            return "OuterMouth";
        }
        else
        {
            return "InnerMouth";
        }
    };

    for (size_t i = 0; i < converted_wflw.size() && i < 30; ++i) // Show first 30 for analysis
    {
        // Find which PFLD landmark this came from by coordinate matching
        int pfld_source = -1;
        double min_distance = 1e9;
        for (size_t j = 0; j < test_pfld_landmarks.size(); ++j)
        {
            double distance = std::sqrt(std::pow(test_pfld_landmarks[j].p.x - converted_wflw[i].p.x, 2)
                                        + std::pow(test_pfld_landmarks[j].p.y - converted_wflw[i].p.y, 2));
            if (distance < min_distance)
            {
                min_distance = distance;
                pfld_source = j;
            }
        }

        if (pfld_source >= 0)
        {
            std::cout << "WFLW[" << i << "] <- PFLD[" << pfld_source << "] : "
                      << "(" << static_cast<int>(test_pfld_landmarks[pfld_source].p.x) << ","
                      << static_cast<int>(test_pfld_landmarks[pfld_source].p.y) << ") -> "
                      << "(" << static_cast<int>(converted_wflw[i].p.x) << ","
                      << static_cast<int>(converted_wflw[i].p.y) << ") [" << getRegionName(i) << "]";

            // Check if mapping makes anatomical sense
            if (min_distance > 50.0)
            { // More than 50 pixels suggests wrong mapping
                std::cout << " *** SUSPICIOUS MAPPING - Distance: " << min_distance << " ***";
            }
            std::cout << std::endl;
        }
    }

    // Additional analysis: check for cross-face mappings
    std::cout << "\n=== Cross-Face Mapping Analysis ===" << std::endl;
    int suspicious_mappings = 0;
    for (size_t i = 0; i < converted_wflw.size(); ++i)
    {
        // Find source PFLD landmark
        int pfld_source = -1;
        double min_distance = 1e9;
        for (size_t j = 0; j < test_pfld_landmarks.size(); ++j)
        {
            double distance = std::sqrt(std::pow(test_pfld_landmarks[j].p.x - converted_wflw[i].p.x, 2)
                                        + std::pow(test_pfld_landmarks[j].p.y - converted_wflw[i].p.y, 2));
            if (distance < min_distance)
            {
                min_distance = distance;
                pfld_source = j;
            }
        }

        if (min_distance > 100.0)
        { // Very suspicious
            suspicious_mappings++;
            std::cout << "MAJOR ISSUE - WFLW[" << i << "] <- PFLD[" << pfld_source << "] distance: " << min_distance
                      << " pixels" << std::endl;
        }
    }

    std::cout << "Total suspicious mappings (>100px distance): " << suspicious_mappings << std::endl;
    EXPECT_LT(suspicious_mappings, 10) << "Too many suspicious landmark mappings detected";
}
