/**
 * BASE CLASS FOR WFLW INTEGRATION TESTS
 *
 * Provides common functionality for all WFLW-based integration tests including:
 * - Model initialization (SCRFD + PFLD)
 * - WFLW dataset loading
 * - Common utility functions for metrics and visualization
 * - Shared test configuration
 */

#pragma once

#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "../../common/dataset_utils.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"
#include "LinuxFace/face.h"
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/scrfd.h"
#include "config.hpp"

using namespace linuxface;

class WFLWTestBase : public ::testing::Test
{
  protected:
    void SetUp() override;
    void TearDown() override;

    // Core functionality
    static bool checkCudaAvailability();

    // Test output directory management
    std::string createTestOutputDirectory(const std::string& test_name) const;

    // Metrics calculation
    double calculateMNE(const std::vector<FaceLandmark>& predicted_landmarks,
                        const std::vector<math_utils::Point<double>>& ground_truth, double interocular_distance) const;

    double calculateInterocularDistance(const Face& face) const;

    // Face matching for multi-face images - using production Face::FaceMatchResult
    Face::FaceMatchResult findBestMatchingFace(const std::vector<Face>& detected_faces,
                                               const math_utils::Rect<double>& ground_truth_bbox,
                                               double min_iou_threshold = 0.1) const
    {
        auto& faces_non_const = const_cast<std::vector<Face>&>(detected_faces);
        return Face::findBestMatchingFace(faces_non_const, ground_truth_bbox, min_iou_threshold);
    }

    // Visualization helpers
    void saveDetectionVisualization(const TestUtils::Datasets::WFLWSample& sample, const std::vector<FaceLandmark>& detected_landmarks,
                                    int image_index, double mne) const;

    void saveDetectionVisualizationWithFaceInfo(const TestUtils::Datasets::WFLWSample& sample,
                                                const std::vector<FaceLandmark>& detected_landmarks, int image_index,
                                                double mne, int face_index, double iou, int total_faces) const;

    // Shared test resources
    std::shared_ptr<SCRFDetector> scrfd_detector_;
    std::shared_ptr<PFLDDetector> pfld_detector_;
    std::unique_ptr<TestUtils::Datasets::SimpleWFLWLoader> wflw_loader_;
    bool has_cuda_available_ = false;
};
