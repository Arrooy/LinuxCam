#ifndef DEPTH_IMAGE_H
#define DEPTH_IMAGE_H

#include <limits>
#include <memory>

#include "LinuxFace/Image/image.h"
//TODO: Still under evaluation.
namespace linuxface
{

class DepthImage : public Image
{
  public:
    // Default constructor
    DepthImage() : Image() {}

    // Constructor with dimensions - creates depth image with float pixel format
    DepthImage(unsigned long width, unsigned long height) : Image(width * height * sizeof(float))
    {
        info.width = width;
        info.height = height;
        info.pixelSizeBytes = sizeof(float);
        info.format = ImageFormat::DEPTH_FLOAT;
        info.x = 0;
        info.y = 0;
    }

    // Constructor that adopts existing buffer
    DepthImage(unsigned char* buffer, size_t size, unsigned long width, unsigned long height, bool takeOwnership = true)
        : Image(buffer, size, takeOwnership)
    {
        info.width = width;
        info.height = height;
        info.pixelSizeBytes = sizeof(float);
        info.format = ImageFormat::DEPTH_FLOAT;
        info.x = 0;
        info.y = 0;
    }

    // Get depth value at specific coordinates
    float getDepth(unsigned long x, unsigned long y) const
    {
        if (x >= info.width || y >= info.height || !data())
        {
            return 0.0f;
        }

        const float* depthData = reinterpret_cast<const float*>(data());
        return depthData[y * info.width + x];
    }

    // Set depth value at specific coordinates
    void setDepth(unsigned long x, unsigned long y, float depth)
    {
        if (x >= info.width || y >= info.height || !data())
        {
            return;
        }

        float* depthData = reinterpret_cast<float*>(data());
        depthData[y * info.width + x] = depth;
    }

    // Get pointer to depth data as float array
    float* getDepthData() const { return reinterpret_cast<float*>(data()); }

    // Create a copy of this depth image
    std::unique_ptr<DepthImage> deepCopyDepth() const
    {
        auto copy = std::make_unique<DepthImage>(info.width, info.height);
        if (data() && copy->data() && size() > 0)
        {
            std::memcpy(copy->data(), data(), size());
        }
        copy->info = this->info;
        // Remove tensor_transform copy since we moved that to TensorPadding
        return copy;
    }

    // Set normal data (assumes normal data is in format [x, y, z] per pixel)
    void setNormals(const float* normalData, size_t normalDataSize)
    {
        if (!normalData)
        {
            common::log_error("DepthImage::setNormals - Null normal data pointer");
            return;
        }

        // Calculate expected size in bytes
        size_t expected_bytes = info.width * info.height * 3 * sizeof(float);

        if (normalDataSize != expected_bytes)
        {
            common::log_warn("DepthImage::setNormals - Expected: %zu bytes, Got: %zu bytes", static_cast<size_t>(expected_bytes), static_cast<size_t>(normalDataSize));
        }

        size_t element_count = normalDataSize / sizeof(float);
        size_t expected_elements = info.width * info.height * 3;

        common::log_warn("DepthImage::setNormals - Size mismatch. Expected %zu elements, got %zu elements",
                         expected_elements, element_count);

        // If we have the right number of elements, proceed anyway
        if (element_count == expected_elements)
        {
            common::log_info("DepthImage::setNormals - Element count matches, proceeding");
        }
        else
        {
            common::log_error("DepthImage::setNormals - Cannot proceed with mismatched element count");
            return;
        }

        size_t elements_to_copy = info.width * info.height * 3;
        normals_ = std::make_unique<float[]>(elements_to_copy);
        std::memcpy(normals_.get(), normalData, elements_to_copy * sizeof(float));
        hasNormals_ = true;
    }

    // Set confidence data
    void setConfidence(const float* confidenceData, size_t confidenceDataSize)
    {
        if (!confidenceData)
        {
            common::log_error("DepthImage::setConfidence - Null confidence data pointer");
            return;
        }

        // Calculate expected size in bytes
        size_t expected_bytes = info.width * info.height * sizeof(float);


        if (confidenceDataSize != expected_bytes)
        {
            // Try to determine actual element count from byte size
            size_t element_count = confidenceDataSize / sizeof(float);
            size_t expected_elements = info.width * info.height;
            common::log_warn("DepthImage::setConfidence - Expected: %zu bytes, Got: %zu bytes", expected_bytes,
                             confidenceDataSize);

            common::log_warn("DepthImage::setConfidence - Size mismatch. Expected %zu elements, got %zu elements",
                             expected_elements, element_count);

            // If we have the right number of elements, proceed anyway
            if (element_count == expected_elements)
            {
                common::log_info("DepthImage::setConfidence - Element count matches, proceeding");
            }
            else
            {
                common::log_error("DepthImage::setConfidence - Cannot proceed with mismatched element count");
                return;
            }
        }

        size_t elements_to_copy = info.width * info.height;
        confidence_ = std::make_unique<float[]>(elements_to_copy);
        std::memcpy(confidence_.get(), confidenceData, elements_to_copy * sizeof(float));
        hasConfidence_ = true;
    }

    // Get normal at specific coordinates (returns pointer to 3-element array [x, y, z])
    const float* getNormal(unsigned long x, unsigned long y) const
    {
        if (!hasNormals_ || x >= info.width || y >= info.height)
        {
            return nullptr;
        }

        return &normals_[(y * info.width + x) * 3];
    }

    // Get confidence at specific coordinates
    float getConfidence(unsigned long x, unsigned long y) const
    {
        if (!hasConfidence_ || x >= info.width || y >= info.height)
        {
            return 0.0f;
        }

        return confidence_[y * info.width + x];
    }

    // Check if normals are available
    bool hasNormals() const { return hasNormals_; }

    // Check if confidence data is available
    bool hasConfidence() const { return hasConfidence_; }

    // Get raw normal data pointer
    const float* getNormalsData() const { return normals_.get(); }

    // Get raw confidence data pointer
    const float* getConfidenceData() const { return confidence_.get(); }

    // Filter depth based on confidence threshold
    void filterByConfidence(float minConfidence)
    {
        if (!hasConfidence_ || !data())
        {
            return;
        }

        float* depthData = getDepthData();
        for (unsigned long i = 0; i < info.width * info.height; ++i)
        {
            if (confidence_[i] < minConfidence)
            {
                depthData[i] = 0.0f; // Set low confidence pixels to zero depth
            }
        }
    }

    // Get depth statistics
    struct DepthStats
    {
        float minDepth;
        float maxDepth;
        float meanDepth;
        unsigned long validPixels;
    };

    DepthStats getDepthStats() const
    {
        DepthStats stats = {0.0f, 0.0f, 0.0f, 0};

        if (!data())
        {
            return stats;
        }

        const float* depthData = getDepthData();
        float sum = 0.0f;
        float minVal = std::numeric_limits<float>::max();
        float maxVal = std::numeric_limits<float>::lowest();

        for (unsigned long i = 0; i < info.width * info.height; ++i)
        {
            float depth = depthData[i];
            if (depth > 0.0f) // Only consider valid depth values
            {
                stats.validPixels++;
                sum += depth;
                minVal = std::min(minVal, depth);
                maxVal = std::max(maxVal, depth);
            }
        }

        if (stats.validPixels > 0)
        {
            stats.minDepth = minVal;
            stats.maxDepth = maxVal;
            stats.meanDepth = sum / stats.validPixels;
        }

        return stats;
    }

    // Specialized scale method for DepthImage that preserves depth functionality
    std::unique_ptr<DepthImage> scale(unsigned long newWidth, unsigned long newHeight) const
    {
        if (!data() || size() == 0 || info.width == 0 || info.height == 0)
        {
            common::log_error("DepthImage::scale - Invalid source image");
            return nullptr;
        }

        if (newWidth == 0 || newHeight == 0)
        {
            common::log_error("DepthImage::scale - Invalid target dimensions: %lux%lu", newWidth, newHeight);
            return nullptr;
        }

        if (newWidth == info.width && newHeight == info.height)
        {
            // No scaling needed, return a deep copy
            return deepCopyDepth();
        }

        // Create scaled depth image
        auto scaledImage = std::make_unique<DepthImage>(newWidth, newHeight);

        // Copy image info and update dimensions
        scaledImage->info = this->info;
        scaledImage->info.width = newWidth;
        scaledImage->info.height = newHeight;

        // Perform bilinear scaling for depth data
        const float* src = getDepthData();
        float* dst = scaledImage->getDepthData();

        // Handle single pixel edge case
        if (info.width == 1 && info.height == 1)
        {
            // Fill entire scaled image with the single source pixel
            for (unsigned long i = 0; i < newWidth * newHeight; i++)
            {
                dst[i] = src[0];
            }
            return scaledImage;
        }

        const double xRatio = (info.width > 1) ? static_cast<double>(info.width - 1) / (newWidth - 1) : 0.0;
        const double yRatio = (info.height > 1) ? static_cast<double>(info.height - 1) / (newHeight - 1) : 0.0;

        for (unsigned long y = 0; y < newHeight; y++)
        {
            for (unsigned long x = 0; x < newWidth; x++)
            {
                // Calculate source coordinates
                double srcX = (newWidth > 1) ? x * xRatio : 0.0;
                double srcY = (newHeight > 1) ? y * yRatio : 0.0;

                // Get integer and fractional parts
                unsigned long x1 = static_cast<unsigned long>(srcX);
                unsigned long y1 = static_cast<unsigned long>(srcY);
                // Ensure we don't go out of bounds
                unsigned long x2 = std::min(x1 + 1, info.width - 1);
                unsigned long y2 = std::min(y1 + 1, info.height - 1);

                double fracX = srcX - x1;
                double fracY = srcY - y1;

                // Calculate destination index
                unsigned long dstIdx = y * newWidth + x;

                // Get source depth values with bounds checking
                unsigned long idx1 = y1 * info.width + x1;
                unsigned long idx2 = y1 * info.width + x2;
                unsigned long idx3 = y2 * info.width + x1;
                unsigned long idx4 = y2 * info.width + x2;

                // Additional safety check
                unsigned long totalPixels = info.width * info.height;
                if (idx1 >= totalPixels || idx2 >= totalPixels || idx3 >= totalPixels || idx4 >= totalPixels)
                {
                    common::log_error("DepthImage::scale - Source index out of bounds");
                    dst[dstIdx] = 0.0f;
                    continue;
                }

                float p1 = src[idx1];
                float p2 = src[idx2];
                float p3 = src[idx3];
                float p4 = src[idx4];

                // Bilinear interpolation for depth values
                float top = p1 * (1.0f - static_cast<float>(fracX)) + p2 * static_cast<float>(fracX);
                float bottom = p3 * (1.0f - static_cast<float>(fracX)) + p4 * static_cast<float>(fracX);
                float result = top * (1.0f - static_cast<float>(fracY)) + bottom * static_cast<float>(fracY);

                dst[dstIdx] = result;
            }
        }

        // Scale normals and confidence if they exist
        if (hasNormals())
        {
            // Create scaled normals data
            auto scaledNormals = std::make_unique<float[]>(newWidth * newHeight * 3);

            for (unsigned long y = 0; y < newHeight; y++)
            {
                for (unsigned long x = 0; x < newWidth; x++)
                {
                    double srcX = (newWidth > 1) ? x * xRatio : 0.0;
                    double srcY = (newHeight > 1) ? y * yRatio : 0.0;

                    unsigned long srcXInt = static_cast<unsigned long>(srcX);
                    unsigned long srcYInt = static_cast<unsigned long>(srcY);

                    // Clamp to bounds
                    srcXInt = std::min(srcXInt, info.width - 1);
                    srcYInt = std::min(srcYInt, info.height - 1);

                    unsigned long srcIdx = (srcYInt * info.width + srcXInt) * 3;
                    unsigned long dstIdx = (y * newWidth + x) * 3;

                    // Copy normal vector (simple nearest neighbor for normals)
                    scaledNormals[dstIdx] = normals_[srcIdx];
                    scaledNormals[dstIdx + 1] = normals_[srcIdx + 1];
                    scaledNormals[dstIdx + 2] = normals_[srcIdx + 2];
                }
            }

            scaledImage->setNormals(scaledNormals.get(), newWidth * newHeight * 3 * sizeof(float));
        }

        if (hasConfidence())
        {
            // Create scaled confidence data
            auto scaledConfidence = std::make_unique<float[]>(newWidth * newHeight);

            for (unsigned long y = 0; y < newHeight; y++)
            {
                for (unsigned long x = 0; x < newWidth; x++)
                {
                    double srcX = (newWidth > 1) ? x * xRatio : 0.0;
                    double srcY = (newHeight > 1) ? y * yRatio : 0.0;

                    unsigned long srcXInt = static_cast<unsigned long>(srcX);
                    unsigned long srcYInt = static_cast<unsigned long>(srcY);

                    // Clamp to bounds
                    srcXInt = std::min(srcXInt, info.width - 1);
                    srcYInt = std::min(srcYInt, info.height - 1);

                    unsigned long srcIdx = srcYInt * info.width + srcXInt;
                    unsigned long dstIdx = y * newWidth + x;

                    // Copy confidence value (simple nearest neighbor)
                    scaledConfidence[dstIdx] = confidence_[srcIdx];
                }
            }

            scaledImage->setConfidence(scaledConfidence.get(), newWidth * newHeight * sizeof(float));
        }

        return scaledImage;
    }

  private:
    std::unique_ptr<float[]> normals_;
    std::unique_ptr<float[]> confidence_;
    bool hasNormals_ = false;
    bool hasConfidence_ = false;
};

} // namespace linuxface

#endif // DEPTH_IMAGE_H
