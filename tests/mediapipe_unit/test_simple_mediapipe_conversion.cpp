#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <map>
#include <iomanip>

#include "LinuxFace/landmark_converter.h"

using namespace linuxface;

class SimpleMediaPipeLandmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple test: single MediaPipe landmark at (100, 200)
        mediapipe_landmarks_.clear();
        for (int i = 0; i < 468; ++i) {
            mediapipe_landmarks_.emplace_back(FaceLandmark{
                static_cast<unsigned int>(i),
                math_utils::Point3D(50.0f + i * 0.1f, 100.0f + i * 0.1f, 0.0f)
            });
        }
    }

    std::vector<FaceLandmark> mediapipe_landmarks_;
};

// Simple direct conversion without interpolation/smoothing
std::vector<FaceLandmark> simpleDirectConversion(const std::vector<FaceLandmark>& mediapipeLandmarks) {
    const auto& mapping = LandmarkConverter::getMediapipeToWflwMapping();
    std::vector<FaceLandmark> wflwLandmarks;
    wflwLandmarks.reserve(98);

    // Only direct mapping - no interpolation or smoothing
    for (int wflwIdx = 0; wflwIdx < 98; ++wflwIdx) {
        auto it = mapping.find(wflwIdx);
        if (it != mapping.end()) {
            const int mediapipeIdx = it->second;
            if (mediapipeIdx < static_cast<int>(mediapipeLandmarks.size())) {
                wflwLandmarks.emplace_back(FaceLandmark{
                    static_cast<unsigned int>(wflwIdx), 
                    mediapipeLandmarks[mediapipeIdx].p
                });
            } else {
                // Invalid mapping - use origin
                wflwLandmarks.emplace_back(FaceLandmark{
                    static_cast<unsigned int>(wflwIdx), 
                    math_utils::Point3D(0, 0, 0)
                });
            }
        } else {
            // No mapping - use origin (this should not happen with complete mapping)
            wflwLandmarks.emplace_back(FaceLandmark{
                static_cast<unsigned int>(wflwIdx), 
                math_utils::Point3D(0, 0, 0)
            });
        }
    }
    return wflwLandmarks;
}

TEST_F(SimpleMediaPipeLandmarkTest, CompareDirectVsComplexConversion) {
    // Test both conversion methods
    auto complexResult = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks_);
    auto simpleResult = simpleDirectConversion(mediapipe_landmarks_);
    
    EXPECT_EQ(complexResult.size(), 98);
    EXPECT_EQ(simpleResult.size(), 98);
    
    std::cout << "=== Conversion Comparison (First 20 landmarks) ===" << std::endl;
    std::cout << "WFLW_Idx | MediaPipe_Idx | Complex (x,y) | Simple (x,y) | Difference" << std::endl;
    std::cout << "---------|---------------|---------------|--------------|------------" << std::endl;
    
    const auto& mapping = LandmarkConverter::getMediapipeToWflwMapping();
    
    for (int i = 0; i < std::min(20, 98); ++i) {
        auto it = mapping.find(i);
        int mediapipeIdx = (it != mapping.end()) ? it->second : -1;
        
        const auto& complexPt = complexResult[i].p;
        const auto& simplePt = simpleResult[i].p;
        
        float diff = std::sqrt(std::pow(complexPt.x - simplePt.x, 2) + 
                              std::pow(complexPt.y - simplePt.y, 2));
        
        std::cout << std::setw(8) << i << " | " 
                  << std::setw(13) << mediapipeIdx << " | "
                  << std::setw(5) << std::fixed << std::setprecision(1) 
                  << complexPt.x << "," << std::setw(5) << complexPt.y << " | "
                  << std::setw(5) << simplePt.x << "," << std::setw(5) << simplePt.y << " | "
                  << std::setw(8) << std::setprecision(2) << diff << std::endl;
    }
    
    // Count how many landmarks have zero coordinates in each method
    int complexZeros = 0, simpleZeros = 0;
    for (int i = 0; i < 98; ++i) {
        const auto& complexPt = complexResult[i].p;
        const auto& simplePt = simpleResult[i].p;
        
        if (complexPt.x == 0 && complexPt.y == 0) complexZeros++;
        if (simplePt.x == 0 && simplePt.y == 0) simpleZeros++;
    }
    
    std::cout << std::endl;
    std::cout << "Complex conversion zeros: " << complexZeros << "/98" << std::endl;
    std::cout << "Simple conversion zeros: " << simpleZeros << "/98" << std::endl;
    
    // Verify our perfect mapping should have no missing correspondences
    EXPECT_EQ(simpleZeros, 0) << "Perfect mapping should have no missing correspondences";
}

TEST_F(SimpleMediaPipeLandmarkTest, ValidateMappingCompleteness) {
    const auto& mapping = LandmarkConverter::getMediapipeToWflwMapping();
    
    std::cout << "\n=== Mapping Completeness Check ===" << std::endl;
    std::cout << "Total mapping entries: " << mapping.size() << std::endl;
    
    // Check if we have mappings for all 98 WFLW landmarks
    int missing = 0;
    for (int i = 0; i < 98; ++i) {
        if (mapping.find(i) == mapping.end()) {
            std::cout << "Missing mapping for WFLW index: " << i << std::endl;
            missing++;
        }
    }
    
    std::cout << "Missing mappings: " << missing << "/98" << std::endl;
    EXPECT_EQ(missing, 0) << "All WFLW landmarks should have MediaPipe correspondences";
}
