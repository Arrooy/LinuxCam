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

#include "../wflw_loader.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"
#include "LinuxFace/face.h"
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/scrfd.h"
#include "config.hpp"

using namespace linuxface;
using namespace linuxface::test;

class WFLWTestBase : public ::testing::Test
{
  public:
    // Path utilities for WFLW dataset (made public for use by other test classes)
    static std::string getWFLWBasePath();
    static std::string getWFLWImagesPath();
    static std::string getWFLWAnnotationsPath();
    static std::string normalizePath(const std::string& path);

  protected:
    void SetUp() override;
    void TearDown() override;

    // Core functionality
    static bool checkCudaAvailability();

    // Metrics calculation
    double calculateMNE(const std::vector<FaceLandmark>& predicted_landmarks,
                        const std::vector<math_utils::Point<double>>& ground_truth, double interocular_distance) const;

    double calculateInterocularDistance(const Face& face) const;

    // Face matching for multi-face images
    struct FaceMatchResult
    {
        Face* best_face = nullptr;
        int face_index = -1;
        double iou_score = 0.0;
        bool found_match = false;
    };

    FaceMatchResult findBestMatchingFace(const std::vector<Face>& detected_faces,
                                         const math_utils::Rect<double>& gt_bbox, double min_iou_threshold = 0.1) const;

    // Visualization helpers
    void saveDetectionVisualization(const WFLWExample& example, const std::vector<FaceLandmark>& detected_landmarks,
                                    int image_index, double mne) const;

    void saveDetectionVisualizationWithFaceInfo(const WFLWExample& example,
                                                const std::vector<FaceLandmark>& detected_landmarks, int image_index,
                                                double mne, int face_index, double iou, int total_faces) const;

    // Benchmark results structure
    struct BenchmarkResults
    {
        double mean_mne = 0.0;
        double median_mne = 0.0;
        double std_dev_mne = 0.0;
        double success_rate = 0.0;
        int total_samples = 0;
        int successful_detections = 0;
        std::vector<double> individual_mne_scores;

        // Performance metrics
        double avg_scrfd_time_ms = 0.0;
        double avg_pfld_time_ms = 0.0;
        double total_pipeline_time_ms = 0.0;

        // Failure analysis
        int scrfd_failures = 0;
        int pfld_failures = 0;
        int iod_failures = 0;
        int landmark_bound_failures = 0;
        int image_load_failures = 0;

        // Error distribution by attribute
        struct AttributeStats
        {
            int pose_failures = 0;
            int expression_failures = 0;
            int illumination_failures = 0;
            int makeup_failures = 0;
            int occlusion_failures = 0;
            int blur_failures = 0;
        } attribute_failures;
    };

    // Shared test resources
    std::shared_ptr<SCRFDetector> scrfd_detector_;
    std::shared_ptr<PFLDDetector> pfld_detector_;
    std::unique_ptr<WFLWLoader> wflw_loader_;
    bool has_cuda_available_ = false;
};
