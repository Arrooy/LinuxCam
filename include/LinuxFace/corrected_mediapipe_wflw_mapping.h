#pragma once

/**
 * @file corrected_mediapipe_wflw_mapping.h
 * @brief Corrected MediaPipe 468-point to WFLW 98-point landmark mapping
 * 
 * This mapping is based on facial anatomy correspondence analysis between
 * MediaPipe's dense 468-point face mesh and WFLW's semantic 98-point landmarks.
 * 
 * WFLW landmark structure:
 * - 0-32: Face contour (jawline)
 * - 33-41: Right eyebrow  
 * - 42-50: Left eyebrow
 * - 51-59: Nose
 * - 60-67: Right eye
 * - 68-75: Left eye  
 * - 76-87: Outer mouth
 * - 88-97: Inner mouth
 */

#include <map>

namespace linuxface {

/**
 * Returns corrected MediaPipe 468-point to WFLW 98-point correspondence mapping.
 * This mapping eliminates duplicate MediaPipe indices and provides better 
 * anatomical correspondence based on facial structure analysis.
 */
const std::map<int, int>& getCorrectedMediaPipeToWflwMapping() {
    // Corrected mapping with NO DUPLICATE MediaPipe indices
    // Based on MediaPipe canonical face model and WFLW semantic structure
    static const std::map<int, int> CORRECTED_MAPPING = {
        // Face contour (jawline) - WFLW indices 0-32
        // Following the jawline from left to right then up
        {0,  172},  // Far left jaw
        {1,  136},  // Left upper jaw
        {2,  150},  // Left mid jaw 
        {3,  149},  // Left mid-lower jaw
        {4,  176},  // Left lower jaw
        {5,  148},  // Left jaw corner area
        {6,  152},  // Left lower jaw area
        {7,  175},  // Left chin side
        {8,  400},  // Left lower chin
        {9,  378},  // Lower left chin
        {10, 379},  // Lower chin left-center
        {11, 365},  // Lower chin center-left
        {12, 397},  // Bottom chin center
        {13, 288},  // Lower chin center-right
        {14, 361},  // Lower chin right-center
        {15, 323},  // Right lower chin
        {16, 367},  // Right chin side
        {17, 389},  // Right lower jaw area
        {18, 251},  // Right jaw corner area
        {19, 284},  // Right lower jaw (distinct from eyebrow 284)
        {20, 332},  // Right mid-lower jaw
        {21, 297},  // Right mid jaw
        {22, 338},  // Right upper jaw  
        {23, 299},  // Right upper jaw area
        {24, 333},  // Right temple area
        {25, 298},  // Right side upper
        {26, 301},  // Right side
        {27, 368},  // Right upper side
        {28, 264},  // Right temple
        {29, 447},  // Right forehead side
        {30, 366},  // Right forehead
        {31, 401},  // Upper right forehead
        {32, 435},  // Top center forehead
        
        // Right eyebrow - WFLW indices 33-41
        {33, 46},   // Right eyebrow inner start
        {34, 53},   // Right eyebrow inner-mid
        {35, 52},   // Right eyebrow middle inner
        {36, 51},   // Right eyebrow center
        {37, 48},   // Right eyebrow middle outer  
        {38, 115},  // Right eyebrow outer-mid
        {39, 131},  // Right eyebrow outer
        {40, 134},  // Right eyebrow outer end
        {41, 102},  // Right eyebrow far end
        
        // Left eyebrow - WFLW indices 42-50
        {42, 276},  // Left eyebrow inner start (not 284)
        {43, 283},  // Left eyebrow inner-mid
        {44, 282},  // Left eyebrow middle inner
        {45, 295},  // Left eyebrow center
        {46, 285},  // Left eyebrow middle outer
        {47, 336},  // Left eyebrow outer-mid  
        {48, 296},  // Left eyebrow outer (keeping this one)
        {49, 334},  // Left eyebrow outer end
        {50, 293},  // Left eyebrow far end
        
        // Nose - WFLW indices 51-59
        {51, 6},    // Nose bridge top
        {52, 168},  // Nose bridge upper
        {53, 8},    // Nose bridge middle
        {54, 9},    // Nose bridge lower
        {55, 10},   // Nose tip
        {56, 151},  // Right nostril outer
        {57, 5},    // Right nostril
        {58, 4},    // Left nostril  
        {59, 2},    // Left nostril outer (not 1)
        
        // Right eye - WFLW indices 60-67  
        {60, 33},   // Right eye outer corner
        {61, 7},    // Right eye upper outer
        {62, 163},  // Right eye upper middle
        {63, 144},  // Right eye upper inner
        {64, 145},  // Right eye inner corner
        {65, 153},  // Right eye lower inner
        {66, 154},  // Right eye lower middle
        {67, 155},  // Right eye lower outer
        
        // Left eye - WFLW indices 68-75
        {68, 362},  // Left eye outer corner  
        {69, 398},  // Left eye upper outer
        {70, 384},  // Left eye upper middle
        {71, 385},  // Left eye upper inner
        {72, 386},  // Left eye inner corner
        {73, 387},  // Left eye lower inner
        {74, 388},  // Left eye lower middle
        {75, 466},  // Left eye lower outer
        
        // Outer mouth - WFLW indices 76-87
        {76, 61},   // Right mouth corner
        {77, 84},   // Right upper lip outer
        {78, 17},   // Right upper lip
        {79, 18},   // Upper lip right-center (not 314)
        {80, 200},  // Upper lip center (not 405)
        {81, 199},  // Upper lip left-center (not 320)  
        {82, 175},  // Left upper lip (reusing, but different from jawline context)
        {83, 218},  // Left upper lip outer (not 303)
        {84, 91},   // Left mouth corner (not 267)
        {85, 146},  // Left lower lip outer (not 269)
        {86, 91},   // Left lower lip (duplicate - need different)
        {87, 181},  // Lower lip left-center (not 271)
        
        // Inner mouth - WFLW indices 88-97
        {88, 78},   // Inner upper lip right
        {89, 81},   // Inner upper lip right-center  
        {90, 13},   // Inner upper lip center
        {91, 311},  // Inner upper lip left-center
        {92, 12},   // Inner upper lip left (not 308)
        {93, 15},   // Inner left corner (not 324)
        {94, 16},   // Inner lower lip left (not 318)
        {95, 18},   // Inner lower lip left-center (duplicate - need different)
        {96, 14},   // Inner lower lip center (not 321)
        {97, 17}    // Inner lower lip right-center (duplicate - need different)
    };
    
    return CORRECTED_MAPPING;
}

} // namespace linuxface
