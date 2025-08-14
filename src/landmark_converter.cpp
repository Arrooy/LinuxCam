/**
 * @file landmark_converter.cpp
 * @brief Implementation of landmark conversion utilities
 */

#include "LinuxFace/landmark_converter.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace linuxface
{

std::vector<FaceLandmark> LandmarkConverter::pfldToWflw(const std::vector<FaceLandmark>& pfld_landmarks)
{
    if (pfld_landmarks.size() != 106)
    {
        throw std::invalid_argument("PFLD landmarks must have exactly 106 points, got " + 
                                   std::to_string(pfld_landmarks.size()));
    }

    const auto& mapping = getPfldToWflwMapping();
    std::vector<FaceLandmark> wflw_landmarks;
    wflw_landmarks.reserve(98);

    // Convert landmarks using the correspondence mapping
    for (int wflw_idx = 0; wflw_idx < 98; ++wflw_idx)
    {
        auto it = mapping.find(wflw_idx);
        if (it != mapping.end())
        {
            int pfld_idx = it->second;
            if (pfld_idx < static_cast<int>(pfld_landmarks.size()))
            {
                // Direct mapping available
                wflw_landmarks.emplace_back(FaceLandmark{
                    wflw_idx,
                    pfld_landmarks[pfld_idx].p
                });
            }
            else
            {
                // Should not happen with valid mapping, but add safeguard
                wflw_landmarks.emplace_back(FaceLandmark{
                    wflw_idx,
                    math_utils::Point3D(0, 0, 0)
                });
            }
        }
        else
        {
            // No direct correspondence - use interpolation or approximation
            // For now, use first 98 landmarks as fallback (our current approach)
            if (wflw_idx < static_cast<int>(pfld_landmarks.size()))
            {
                wflw_landmarks.emplace_back(FaceLandmark{
                    wflw_idx,
                    pfld_landmarks[wflw_idx].p
                });
            }
            else
            {
                // Fallback to origin - should not happen
                wflw_landmarks.emplace_back(FaceLandmark{
                    wflw_idx,
                    math_utils::Point3D(0, 0, 0)
                });
            }
        }
    }

    // Apply curve-aware smoothing for better accuracy
    std::vector<std::pair<int, int>> wflw_curve_segments = {
        {0, 16},   // Jawline left half
        {16, 32},  // Jawline right half
        {33, 41},  // Right eyebrow
        {42, 50},  // Left eyebrow
        {51, 59},  // Nose
        {60, 67},  // Right eye
        {68, 75},  // Left eye
        {76, 87},  // Outer lip
        {88, 95}   // Inner lip
    };
    
    auto smoothed_result = applyCurveSmoothing(wflw_landmarks, wflw_curve_segments);
    
    return smoothed_result;
}

std::vector<FaceLandmark> LandmarkConverter::wflwToPfld(const std::vector<FaceLandmark>& wflw_landmarks)
{
    if (wflw_landmarks.size() != 98)
    {
        throw std::invalid_argument("WFLW landmarks must have exactly 98 points, got " + 
                                   std::to_string(wflw_landmarks.size()));
    }

    const auto& mapping = getWflwToPfldMapping();
    std::vector<FaceLandmark> pfld_landmarks(106);

    // Initialize all landmarks to origin
    for (int i = 0; i < 106; ++i)
    {
        pfld_landmarks[i] = FaceLandmark{i, math_utils::Point3D(0, 0, 0)};
    }

    // Map existing landmarks
    for (int wflw_idx = 0; wflw_idx < 98; ++wflw_idx)
    {
        auto it = mapping.find(wflw_idx);
        if (it != mapping.end())
        {
            int pfld_idx = it->second;
            if (pfld_idx < 106)
            {
                pfld_landmarks[pfld_idx] = FaceLandmark{
                    pfld_idx,
                    wflw_landmarks[wflw_idx].p
                };
            }
        }
    }

    // Interpolate missing landmarks (the extra 8 landmarks in PFLD)
    // This is a simplified interpolation - in practice, would need more sophisticated methods
    std::vector<int> missing_indices;
    for (int i = 0; i < 106; ++i)
    {
        if (pfld_landmarks[i].p.x == 0 && pfld_landmarks[i].p.y == 0 && pfld_landmarks[i].p.z == 0)
        {
            missing_indices.push_back(i);
        }
    }

    if (!missing_indices.empty())
    {
        auto interpolated = interpolateLandmarks(pfld_landmarks, missing_indices);
        for (size_t i = 0; i < interpolated.size() && i < missing_indices.size(); ++i)
        {
            pfld_landmarks[missing_indices[i]] = interpolated[i];
        }
    }

    // Apply curve-aware smoothing for PFLD format as well
    std::vector<std::pair<int, int>> pfld_curve_segments = {
        {0, 16},   // Jawline
        {17, 21},  // Right eyebrow  
        {22, 26},  // Left eyebrow
        {27, 35},  // Nose
        {36, 41},  // Right eye
        {42, 47},  // Left eye
        {48, 59},  // Outer mouth
        {60, 67}   // Inner mouth (partial - PFLD has more inner lip points)
    };
    
    auto smoothed_pfld = applyCurveSmoothing(pfld_landmarks, pfld_curve_segments);

    return smoothed_pfld;
}

std::vector<FaceLandmark> LandmarkConverter::extractKeyLandmarks(const std::vector<FaceLandmark>& landmarks, 
                                                                LandmarkFormat format)
{
    std::vector<FaceLandmark> key_landmarks;
    key_landmarks.reserve(5);

    switch (format)
    {
        case LandmarkFormat::PFLD_106:
        {
            // PFLD key landmark indices (approximate based on common facial landmark structures)
            if (landmarks.size() >= 106)
            {
                // These indices are approximations - would need exact PFLD format specification
                key_landmarks.push_back({0, landmarks[36].p}); // Left eye (approximate)
                key_landmarks.push_back({1, landmarks[45].p}); // Right eye (approximate)
                key_landmarks.push_back({2, landmarks[33].p}); // Nose tip (approximate)
                key_landmarks.push_back({3, landmarks[48].p}); // Left mouth corner (approximate)
                key_landmarks.push_back({4, landmarks[54].p}); // Right mouth corner (approximate)
            }
            break;
        }
        case LandmarkFormat::WFLW_98:
        {
            // WFLW key landmark indices based on WFLW format definition
            if (landmarks.size() >= 98)
            {
                // Based on WFLW format: https://wywu.github.io/projects/LAB/WFLW.html
                key_landmarks.push_back({0, landmarks[68].p}); // Left eye center (approximate)
                key_landmarks.push_back({1, landmarks[60].p}); // Right eye center (approximate) 
                key_landmarks.push_back({2, landmarks[54].p}); // Nose tip
                key_landmarks.push_back({3, landmarks[76].p}); // Left mouth corner
                key_landmarks.push_back({4, landmarks[82].p}); // Right mouth corner
            }
            break;
        }
        case LandmarkFormat::SCRFD_5:
        {
            // SCRFD already provides 5 key landmarks
            if (landmarks.size() >= 5)
            {
                for (int i = 0; i < 5; ++i)
                {
                    key_landmarks.push_back(landmarks[i]);
                }
            }
            break;
        }
    }

    return key_landmarks;
}

std::vector<int> LandmarkConverter::getRegionIndices(FacialRegion region, LandmarkFormat format)
{
    std::vector<int> indices;

    if (format == LandmarkFormat::WFLW_98)
    {
        // WFLW 98-point region definitions based on dataset specification
        switch (region)
        {
            case FacialRegion::JAWLINE:
                for (int i = 0; i <= 32; ++i) indices.push_back(i);
                break;
            case FacialRegion::RIGHT_EYEBROW:
                for (int i = 33; i <= 41; ++i) indices.push_back(i);
                break;
            case FacialRegion::LEFT_EYEBROW:
                for (int i = 42; i <= 50; ++i) indices.push_back(i);
                break;
            case FacialRegion::NOSE_BRIDGE:
                for (int i = 51; i <= 59; ++i) indices.push_back(i);
                break;
            case FacialRegion::RIGHT_EYE:
                for (int i = 60; i <= 67; ++i) indices.push_back(i);
                break;
            case FacialRegion::LEFT_EYE:
                for (int i = 68; i <= 75; ++i) indices.push_back(i);
                break;
            case FacialRegion::OUTER_MOUTH:
                for (int i = 76; i <= 87; ++i) indices.push_back(i);
                break;
            case FacialRegion::INNER_MOUTH:
                for (int i = 88; i <= 97; ++i) indices.push_back(i);
                break;
            default:
                break;
        }
    }
    else if (format == LandmarkFormat::PFLD_106)
    {
        // PFLD region definitions (approximations based on common 68+38 landmark structure)
        switch (region)
        {
            case FacialRegion::JAWLINE:
                for (int i = 0; i <= 16; ++i) indices.push_back(i);
                break;
            case FacialRegion::RIGHT_EYEBROW:
                for (int i = 17; i <= 21; ++i) indices.push_back(i);
                break;
            case FacialRegion::LEFT_EYEBROW:
                for (int i = 22; i <= 26; ++i) indices.push_back(i);
                break;
            case FacialRegion::NOSE_BRIDGE:
                for (int i = 27; i <= 35; ++i) indices.push_back(i);
                break;
            case FacialRegion::RIGHT_EYE:
                for (int i = 36; i <= 41; ++i) indices.push_back(i);
                break;
            case FacialRegion::LEFT_EYE:
                for (int i = 42; i <= 47; ++i) indices.push_back(i);
                break;
            case FacialRegion::OUTER_MOUTH:
                for (int i = 48; i <= 59; ++i) indices.push_back(i);
                break;
            case FacialRegion::INNER_MOUTH:
                for (int i = 60; i <= 67; ++i) indices.push_back(i);
                break;
            default:
                break;
        }
    }

    return indices;
}

bool LandmarkConverter::validateLandmarkFormat(const std::vector<FaceLandmark>& landmarks, 
                                              LandmarkFormat expected_format)
{
    int expected_count = getExpectedLandmarkCount(expected_format);
    return landmarks.size() == static_cast<size_t>(expected_count);
}

int LandmarkConverter::getExpectedLandmarkCount(LandmarkFormat format)
{
    switch (format)
    {
        case LandmarkFormat::PFLD_106: return 106;
        case LandmarkFormat::WFLW_98:  return 98;
        case LandmarkFormat::SCRFD_5:  return 5;
        default: return 0;
    }
}

const std::map<int, int>& LandmarkConverter::getPfldToWflwMapping()
{
    // Static mapping from WFLW index to PFLD index
    // This is a best-effort mapping based on common facial landmark correspondences
    // For production use, this should be refined based on exact format specifications
    static const std::map<int, int> mapping = {
        // Jawline (WFLW 0-32 -> PFLD 0-16 + extended jaw points)
        {0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8}, 
        {9, 9}, {10, 10}, {11, 11}, {12, 12}, {13, 13}, {14, 14}, {15, 15}, {16, 16},
        {17, 68}, {18, 69}, {19, 70}, {20, 71}, {21, 72}, {22, 73}, {23, 74}, {24, 75},
        {25, 76}, {26, 77}, {27, 78}, {28, 79}, {29, 80}, {30, 81}, {31, 82}, {32, 83},
        
        // Right eyebrow (WFLW 33-41 -> PFLD 17-21 + extended)
        {33, 17}, {34, 18}, {35, 19}, {36, 20}, {37, 21}, {38, 84}, {39, 85}, {40, 86}, {41, 87},
        
        // Left eyebrow (WFLW 42-50 -> PFLD 22-26 + extended)  
        {42, 22}, {43, 23}, {44, 24}, {45, 25}, {46, 26}, {47, 88}, {48, 89}, {49, 90}, {50, 91},
        
        // Nose (WFLW 51-59 -> PFLD 27-35)
        {51, 27}, {52, 28}, {53, 29}, {54, 30}, {55, 31}, {56, 32}, {57, 33}, {58, 34}, {59, 35},
        
        // Right eye (WFLW 60-67 -> PFLD 36-41 + extended)
        {60, 36}, {61, 37}, {62, 38}, {63, 39}, {64, 40}, {65, 41}, {66, 92}, {67, 93},
        
        // Left eye (WFLW 68-75 -> PFLD 42-47 + extended)
        {68, 42}, {69, 43}, {70, 44}, {71, 45}, {72, 46}, {73, 47}, {74, 94}, {75, 95},
        
        // Outer mouth (WFLW 76-87 -> PFLD 48-59)
        {76, 48}, {77, 49}, {78, 50}, {79, 51}, {80, 52}, {81, 53}, 
        {82, 54}, {83, 55}, {84, 56}, {85, 57}, {86, 58}, {87, 59},
        
        // Inner mouth (WFLW 88-97 -> PFLD 60-67 + extended)
        {88, 60}, {89, 61}, {90, 62}, {91, 63}, {92, 64}, {93, 65}, {94, 66}, {95, 67},
        {96, 96}, {97, 97}
    };
    
    return mapping;
}

const std::map<int, int>& LandmarkConverter::getWflwToPfldMapping()
{
    // Reverse mapping - this is computed once and cached
    static std::map<int, int> reverse_mapping;
    static bool initialized = false;
    
    if (!initialized)
    {
        const auto& forward_mapping = getPfldToWflwMapping();
        for (const auto& pair : forward_mapping)
        {
            reverse_mapping[pair.second] = pair.first;
        }
        initialized = true;
    }
    
    return reverse_mapping;
}

std::vector<FaceLandmark> LandmarkConverter::interpolateLandmarks(const std::vector<FaceLandmark>& landmarks,
                                                                 const std::vector<int>& missing_indices)
{
    std::vector<FaceLandmark> interpolated;
    interpolated.reserve(missing_indices.size());

    for (int missing_idx : missing_indices)
    {
        // Simple interpolation: find nearest valid landmarks and average their positions
        // This is a simplified approach - production code would use more sophisticated interpolation
        
        double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
        int count = 0;
        
        // Find nearby landmarks for interpolation
        for (int i = std::max(0, missing_idx - 5); i < std::min(static_cast<int>(landmarks.size()), missing_idx + 5); ++i)
        {
            if (i != missing_idx && 
                !(landmarks[i].p.x == 0 && landmarks[i].p.y == 0 && landmarks[i].p.z == 0))
            {
                sum_x += landmarks[i].p.x;
                sum_y += landmarks[i].p.y;
                sum_z += landmarks[i].p.z;
                count++;
            }
        }
        
        if (count > 0)
        {
            interpolated.emplace_back(FaceLandmark{
                missing_idx,
                math_utils::Point3D(sum_x / count, sum_y / count, sum_z / count)
            });
        }
        else
        {
            // Fallback: use center point or zero
            interpolated.emplace_back(FaceLandmark{
                missing_idx,
                math_utils::Point3D(0, 0, 0)
            });
        }
    }

    return interpolated;
}

// Enhanced geometric interpolation considering facial structure
math_utils::Point3D LandmarkConverter::computeGeometricInterpolation(
    const std::vector<FaceLandmark>& landmarks, 
    int target_index, 
    const std::vector<int>& available_indices)
{
    // Find nearest neighbors with curve-aware distance weighting
    std::vector<std::pair<double, int>> weighted_neighbors;
    
    for (int idx : available_indices)
    {
        if (idx >= 0 && idx < static_cast<int>(landmarks.size()))
        {
            // Consider both spatial distance and index proximity for facial geometry
            double spatial_weight = 1.0;
            double index_weight = 1.0 / (1.0 + std::abs(target_index - idx));
            
            // Apply facial region weighting - landmarks in same facial region are more relevant
            double region_weight = getFacialRegionWeight(target_index, idx);
            
            double combined_weight = spatial_weight * index_weight * region_weight;
            weighted_neighbors.emplace_back(combined_weight, idx);
        }
    }
    
    if (weighted_neighbors.empty())
    {
        return math_utils::Point3D(0, 0, 0);
    }
    
    // Sort by weight (highest first)
    std::sort(weighted_neighbors.begin(), weighted_neighbors.end(),
        [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            return a.first > b.first;
        });
    
    // Use top weighted neighbors for interpolation
    double total_weight = 0.0;
    math_utils::Point3D weighted_sum(0, 0, 0);
    
    size_t max_neighbors = std::min(size_t(4), weighted_neighbors.size()); // Use up to 4 neighbors
    for (size_t i = 0; i < max_neighbors; ++i)
    {
        double weight = weighted_neighbors[i].first;
        int neighbor_idx = weighted_neighbors[i].second;
        
        const auto& neighbor_point = landmarks[neighbor_idx].p;
        
        weighted_sum.x += neighbor_point.x * weight;
        weighted_sum.y += neighbor_point.y * weight;
        weighted_sum.z += neighbor_point.z * weight;
        total_weight += weight;
    }
    
    if (total_weight > 0.0)
    {
        return math_utils::Point3D(
            weighted_sum.x / total_weight,
            weighted_sum.y / total_weight,
            weighted_sum.z / total_weight
        );
    }
    
    return math_utils::Point3D(0, 0, 0);
}

// Compute facial region weight for enhanced interpolation
double LandmarkConverter::getFacialRegionWeight(int target_idx, int source_idx)
{
    // Define facial regions based on landmark indices
    auto get_region = [](int idx) -> int {
        if (idx >= 0 && idx <= 16) return 0;    // Jawline
        if (idx >= 17 && idx <= 21) return 1;   // Right eyebrow
        if (idx >= 22 && idx <= 26) return 2;   // Left eyebrow  
        if (idx >= 27 && idx <= 35) return 3;   // Nose
        if (idx >= 36 && idx <= 41) return 4;   // Right eye
        if (idx >= 42 && idx <= 47) return 5;   // Left eye
        if (idx >= 48 && idx <= 59) return 6;   // Outer mouth
        if (idx >= 60 && idx <= 67) return 7;   // Inner mouth
        return 8; // Other/Unknown
    };
    
    int target_region = get_region(target_idx);
    int source_region = get_region(source_idx);
    
    // Same region gets highest weight
    if (target_region == source_region) return 2.0;
    
    // Adjacent regions get medium weight
    if (std::abs(target_region - source_region) <= 1) return 1.5;
    
    // Distant regions get lower weight
    return 1.0;
}

// Apply curve-aware smoothing to improve landmark conversion accuracy
std::vector<FaceLandmark> LandmarkConverter::applyCurveSmoothing(
    const std::vector<FaceLandmark>& landmarks, 
    const std::vector<std::pair<int, int>>& curve_segments)
{
    std::vector<FaceLandmark> smoothed = landmarks;
    
    for (const auto& segment : curve_segments)
    {
        int start_idx = segment.first;
        int end_idx = segment.second;
        
        if (start_idx >= static_cast<int>(smoothed.size()) || 
            end_idx >= static_cast<int>(smoothed.size()) || 
            start_idx >= end_idx)
        {
            continue;
        }
        
        // Apply simple smoothing within the curve segment
        for (int i = start_idx + 1; i < end_idx; ++i)
        {
            const auto& prev = smoothed[i - 1].p;
            const auto& curr = smoothed[i].p;
            const auto& next = smoothed[i + 1].p;
            
            // Weighted average smoothing
            double weight = 0.7; // Current point weight
            double neighbor_weight = (1.0 - weight) / 2.0;
            
            math_utils::Point3D smoothed_point(
                curr.x * weight + (prev.x + next.x) * neighbor_weight,
                curr.y * weight + (prev.y + next.y) * neighbor_weight, 
                curr.z * weight + (prev.z + next.z) * neighbor_weight
            );
            
            smoothed[i].p = smoothed_point;
        }
    }
    
    return smoothed;
}

} // namespace linuxface
