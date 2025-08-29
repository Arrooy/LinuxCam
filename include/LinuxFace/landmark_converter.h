/**
 * @file landmark_converter.h
 * @brief Utility for converting between different facial landmark formats
 *
 * This header provides conversion utilities between different facial landmark formats:
 * - PFLD 106-point landmarks (detailed facial landmarks)
 * - WFLW 98-point landmarks (benchmark dataset format)
 *
 * Based on research of both landmark formats and common facial landmark correspondences.
 */

#ifndef LINUXFACE_LANDMARK_CONVERTER_H
#define LINUXFACE_LANDMARK_CONVERTER_H

#include <map>
#include <vector>

#include "LinuxFace/face.h"

namespace linuxface
{

/**
 * @brief Landmark format types supported by the converter
 */
enum class LandmarkFormat
{
    PFLD_106, // PFLD 106-point format (indices 0-105)
    WFLW_98,  // WFLW 98-point format (indices 0-97)
    SCRFD_5   // SCRFD 5-point format (left_eye, right_eye, nose, left_mouth, right_mouth)
};

/**
 * @brief Facial region definitions for landmark organization
 */
enum class FacialRegion
{
    JAWLINE,
    RIGHT_EYEBROW,
    LEFT_EYEBROW,
    NOSE_BRIDGE,
    NOSE_TIP,
    RIGHT_EYE,
    LEFT_EYE,
    OUTER_MOUTH,
    INNER_MOUTH,
    CHEEK,
    FOREHEAD
};

/**
 * @brief Landmark conversion utility class
 *
 * Provides methods to convert between different landmark formats with proper
 * correspondence mapping based on facial anatomy.
 */
class LandmarkConverter
{
  public:
    /**
     * @brief Convert PFLD 106-point landmarks to WFLW 98-point format
     *
     * @param pfld_landmarks Vector of PFLD landmarks (should have 106 points)
     * @return Vector of landmarks in WFLW format (98 points)
     * @throws std::invalid_argument if input doesn't have 106 landmarks
     */
    static std::vector<FaceLandmark> pfldToWflw(const std::vector<FaceLandmark>& pfldLandmarks);

    /**
     * @brief Convert WFLW 98-point landmarks to PFLD 106-point format
     *
     * @param wflw_landmarks Vector of WFLW landmarks (should have 98 points)
     * @return Vector of landmarks in PFLD format (106 points, with interpolation
     * for missing points)
     * @throws std::invalid_argument if input doesn't have 98 landmarks
     */
    static std::vector<FaceLandmark> wflwToPfld(const std::vector<FaceLandmark>& wflwLandmarks);

    /**
     * @brief Extract key landmarks from any format for basic face operations
     *
     * @param landmarks Input landmarks in any supported format
     * @param format The format of the input landmarks
     * @return Vector of 5 key landmarks: [left_eye, right_eye, nose, left_mouth,
     * right_mouth]
     */
    static std::vector<FaceLandmark>
    extractKeyLandmarks(const std::vector<FaceLandmark>& landmarks, LandmarkFormat format);

    /**
     * @brief Get landmark indices for a specific facial region in given format
     *
     * @param region The facial region to get indices for
     * @param format The landmark format
     * @return Vector of landmark indices for the specified region
     */
    static std::vector<int> getRegionIndices(FacialRegion region, LandmarkFormat format);

    /**
     * @brief Validate landmark format (check if landmark count matches expected
     * format)
     *
     * @param landmarks Vector of landmarks to validate
     * @param expected_format Expected landmark format
     * @return true if landmarks match the expected format, false otherwise
     */
    static bool validateLandmarkFormat(const std::vector<FaceLandmark>& landmarks, LandmarkFormat expectedFormat);

    /**
     * @brief Get the expected number of landmarks for a given format
     *
     * @param format The landmark format
     * @return Expected number of landmarks for the format
     */
    static int getExpectedLandmarkCount(LandmarkFormat format);

  private:
    /**
     * @brief Get the correspondence mapping from PFLD to WFLW indices
     *
     * @return Map where key=WFLW_index, value=PFLD_index
     */
    static const std::map<int, int>& getPfldToWflwMapping();

    /**
     * @brief Get the reverse correspondence mapping from WFLW to PFLD indices
     *
     * @return Map where key=PFLD_index, value=WFLW_index
     */
    static const std::map<int, int>& getWflwToPfldMapping();

    /**
     * @brief Interpolate missing landmarks using nearby landmarks
     *
     * @param landmarks Existing landmarks
     * @param missing_indices Indices of landmarks to interpolate
     * @return Vector of interpolated landmarks
     */
    static std::vector<FaceLandmark>
    interpolateLandmarks(const std::vector<FaceLandmark>& landmarks, const std::vector<int>& missingIndices);

    /**
     * @brief Enhanced geometric interpolation considering facial structure
     *
     * @param landmarks Existing landmarks
     * @param target_index Index of landmark to interpolate
     * @param available_indices Indices of available landmarks to use
     * @return Interpolated 3D point
     */
    static math_utils::Point3D computeGeometricInterpolation(const std::vector<FaceLandmark>& landmarks,
                                                             int targetIndex, const std::vector<int>& availableIndices);

    /**
     * @brief Compute facial region weight for enhanced interpolation
     *
     * @param target_idx Target landmark index
     * @param source_idx Source landmark index
     * @return Weight factor for interpolation
     */
    static double getFacialRegionWeight(int targetIdx, int sourceIdx);

    /**
     * @brief Apply curve-aware smoothing to improve landmark accuracy
     *
     * @param landmarks Input landmarks
     * @param curve_segments Vector of start/end index pairs defining curve segments
     * @return Smoothed landmarks
     */
    static std::vector<FaceLandmark> applyCurveSmoothing(const std::vector<FaceLandmark>& landmarks,
                                                         const std::vector<std::pair<int, int>>& curveSegments);
};

} // namespace linuxface

#endif // LINUXFACE_LANDMARK_CONVERTER_H
