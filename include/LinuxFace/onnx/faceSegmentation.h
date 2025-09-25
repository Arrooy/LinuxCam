#ifndef FACE_SEGMENTATION_H
#define FACE_SEGMENTATION_H

#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{
/**
 * Face segmentation class labels for BiSeNet face parsing model
 */
enum class FaceSegmentationClass : unsigned char
{
    BACKGROUND = 0,  // Background
    SKIN = 1,        // Skin
    L_BROW = 2,      // Left eyebrow
    R_BROW = 3,      // Right eyebrow
    L_EYE = 4,       // Left eye
    R_EYE = 5,       // Right eye
    EYE_G = 6,       // Eye glasses
    L_EAR = 7,       // Left ear
    R_EAR = 8,       // Right ear
    EAR_R = 9,       // Ear ring
    NOSE = 10,       // Nose
    MOUTH = 11,      // Mouth
    U_LIP = 12,      // Upper lip
    L_LIP = 13,      // Lower lip
    NECK = 14,       // Neck
    NECK_L = 15,     // Neck lace
    CLOTH = 16,      // Clothing
    HAIR = 17,       // Hair
    HAT = 18,        // Hat
    NUM_CLASSES = 19 // Total number of classes
};

/**
 * Face segmentation detector using face_parsing_18.onnx model
 * Based on BiSeNet architecture for semantic segmentation of facial regions
 */
class FaceSegmentationDetector : public OnnxDetector
{
  public:
    explicit FaceSegmentationDetector(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath) {};
    ~FaceSegmentationDetector() = default;

    Ort::Value transform(const std::unique_ptr<Image>& image) override;

    /**
     * Detect face segmentation regions
     * @param image Input image containing a face
     * @param labelMask Output label mask with integer values 0-18 representing different facial regions
     * @return True if segmentation was successful, false otherwise
     */
    bool detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& labelMask);

    // TODO: comment.
    bool detect(std::unique_ptr<Image>& image, Face& face);

    /**
     * Apply colored visualization overlay to show segmentation results
     * @param faceImage Face image to apply visualization to (modified in-place)
     * @param labelMask Segmentation label mask with class IDs
     * @param centerX X coordinate of face center
     * @param centerY Y coordinate of face center
     * @param roiWidth Width of ROI to visualize around center
     * @param roiHeight Height of ROI to visualize around center
     */
    static void applySegmentationVisualization(Image& faceImage, const Image& labelMask, float centerX, float centerY);

    static void applySegmentationVisualization(Image& faceImage, const Face& face);

    /**
     * Create a binary grayscale mask from segmentation labels
     * @param labelMask Segmentation label mask with class IDs
     * @param faceClasses List of class IDs to include in the face mask (set to 255)
     * @return Binary grayscale mask where selected classes are white
     */
    static std::unique_ptr<Image>
    createFaceShapeMask(const Image& labelMask, const std::vector<FaceSegmentationClass>& faceClasses);


  private:
    static constexpr const int InputWidth = 512;
    static constexpr const int InputHeight = 512;
    static constexpr const int NumClasses = static_cast<int>(FaceSegmentationClass::NUM_CLASSES);

    // ImageNet normalization values for RGB (as used in BiSeNet)
    static constexpr const float MeanVals[3] = {0.485f * 255.0f, 0.456f * 255.0f, 0.406f * 255.0f};
    static constexpr const float ScaleVals[3] = {1.0f / (0.229f * 255.0f), 1.0f / (0.224f * 255.0f),
                                                 1.0f / (0.225f * 255.0f)};

    TensorPadding padding_;

    /**
     * Generate segmentation mask from model output
     */
    void generateMask(std::vector<Ort::Value>& outputTensors, const std::unique_ptr<Image>& originalImage,
                      std::unique_ptr<Image>& labelMask);
};

} // namespace linuxface

#endif // FACE_SEGMENTATION_H
