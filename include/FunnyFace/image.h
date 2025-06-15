#ifndef IMAGE_H
#define IMAGE_H

#include <math.h>
#include <onnxruntime_cxx_api.h>
#include <turbojpeg.h>

#include <memory>
#include <mutex>

#include "FunnyFace/common.h"
#include "FunnyFace/math_utils.h"

namespace funnyface
{


enum class ImageFormat
{
    UNKNOWN,
    JPEG,
    SGBRG8,    // Bayer format
    DEPTH_Z16, // Depth image format
    UYUV422,   // UYVY 4:2:2 format
    YUYV422,   // YUYV 4:2:2 format
    RAW,
};

inline std::string fromImageFormatToString(const ImageFormat& format)
{
    switch (format)
    {
        case ImageFormat::JPEG:
            return "JPEG";
        case ImageFormat::SGBRG8:
            return "SGBRG8";
        case ImageFormat::DEPTH_Z16:
            return "DEPTH_Z16";
        case ImageFormat::UYUV422:
            return "UYUV422";
        case ImageFormat::YUYV422:
            return "YUYV422";
        case ImageFormat::RAW:
            return "RAW";
        default:
            return "UNKNOWN";
    }
}

constexpr unsigned char DEFAULT_ALPHA = 255;

struct Pixel
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;

    Pixel(unsigned char r, unsigned char g, unsigned char b, unsigned char a = DEFAULT_ALPHA)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }
};

struct TJImageDescription
{
    unsigned long x{0u};
    unsigned long y{0u};
    unsigned long width;
    unsigned long height;
    unsigned char pixelSizeBytes;
    TJSAMP TJSampleFormat; // TJSAMP_444
    TJCS TJColorSpace;     // TJCS_RGB
    TJPF TJPixelFormat;    // TJPF_RGB
};

// Image class with proper resource management
class Image
{
  public:
    // Default constructor
    Image() : data_(nullptr), size_(0) {}
    ~Image() = default;

    // Constructor with size allocation
    explicit Image(size_t size) : size_(size)
    {
        if (size > 0)
        {
            data_ = std::shared_ptr<unsigned char>(new unsigned char[size], std::default_delete<unsigned char[]>());
        }
    }

    // Constructor that adopts existing buffer without copying
    Image(unsigned char* buffer, size_t size) : size_(size)
    {
        if (buffer && size > 0)
        {
            data_ = std::shared_ptr<unsigned char>(buffer, std::default_delete<unsigned char[]>());
        }
    }

    // Constructor for non-owning reference (for V4L2 buffers)
    Image(unsigned char* buffer, size_t size, bool takeOwnership) : size_(size)
    {
        if (buffer && size > 0)
        {
            if (takeOwnership)
            {
                data_ = std::shared_ptr<unsigned char>(buffer, std::default_delete<unsigned char[]>());
            }
            else
            {
                // Non-owning reference - use custom deleter that does nothing
                data_ = std::shared_ptr<unsigned char>(buffer, [](unsigned char*) {});
            }
        }
    }

    // Resize the image data
    void resize(size_t newSize)
    {
        if (newSize == 0)
        {
            data_.reset();
            size_ = 0;
            return;
        }

        if (size_ != newSize)
        {
            std::shared_ptr<unsigned char> newData(new unsigned char[newSize], std::default_delete<unsigned char[]>());
            // Initialize new data to zero
            memset(newData.get(), 0, newSize);

            // TODO: FIXME: Can we remove this memcpy? do we need old data in any situation?
            if (data_ && newData)
            {
                // Copy existing data if present
                memcpy(newData.get(), data_.get(), std::min(size_, newSize));
            }

            data_ = std::move(newData);
            size_ = newSize;
        }
    }

    // Access pixel at row, col (with bounds checking)
    Pixel operator()(unsigned long col, unsigned long row) const
    {
        unsigned long idx = index(col, row);
        if (idx >= size_ || !data_)
        {
            common::log_error("Image::operator(): index out of bounds [col,row] %ld, %ld Index: %ld", col, row, idx);
            return Pixel(0, 0, 0, DEFAULT_ALPHA); // Return a default pixel
        }

        // TODO: Byte order depends on pixelFormat. Forced to RGBA for now
        unsigned char* data = data_.get();
        if (info.pixelSizeBytes == 4)
        {
            return Pixel(data[idx], data[idx + 1], data[idx + 2], data[idx + 3]);
        }
        else
        {
            return Pixel(data[idx], data[idx + 1], data[idx + 2], DEFAULT_ALPHA);
        }
    }

    void ppx(unsigned long col, unsigned long row, const Pixel& c) { this->pxy(col, row, c.r, c.g, c.b, c.a); }
    void pxy(unsigned long col, unsigned long row, const unsigned char r, const unsigned char g, const unsigned char b,
             const unsigned char a = DEFAULT_ALPHA)
    {
        unsigned long idx = index(col, row);
        if (idx >= size_ || !data_)
        {
            common::log_error("Image::px: index out of bounds [col,row] %ld, %ld Index: %ld", col, row, idx);
            return;
        }
        this->pidx(idx, r, g, b, a);
    }

    void pidx(unsigned long idx, const unsigned char r, const unsigned char g, const unsigned char b,
              const unsigned char a = DEFAULT_ALPHA)
    {
        // TODO: Byte order depends on pixelFormat. Forced to RGBA for now
        unsigned char* data = data_.get();
        data[idx] = r;
        data[idx + 1] = g;
        data[idx + 2] = b;
        if (info.pixelSizeBytes == 4)
        {
            data[idx + 3] = a;
        }
    }

    // Size accessor
    size_t size() const { return size_; }

    // Data accessor
    unsigned char* data() const { return data_ != nullptr ? data_.get() : nullptr; }

    TJImageDescription info;

    void copyFrom(const Image& other)
    {
        if (this != &other)
        {
            this->resize(other.size_);

            // Copy data if both images have valid data pointers and size > 0
            if (data_ && other.data() && size_ > 0)
            {
                std::memcpy(data_.get(), other.data(), size_);
            }
            size_ = other.size_;
            info = other.info;
        }
    }

    std::unique_ptr<Image> deepCopy() const
    {
        auto copy = std::make_unique<Image>(size_);
        if (data_ && copy->data() && size_ > 0)
        {
            std::memcpy(copy->data(), data_.get(), size_);
        }
        copy->info = this->info;
        return copy;
    }

    // Scale image by percentage while maintaining aspect ratio
    std::unique_ptr<Image> scaleByPercentage(double percentage)
    {
        if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
        {
            common::log_error("Image::scaleByPercentage - Invalid source image");
            return nullptr;
        }

        if (percentage <= 0.0)
        {
            common::log_error("Image::scaleByPercentage - Invalid percentage: %f", percentage);
            return nullptr;
        }

        // Calculate new dimensions maintaining aspect ratio
        unsigned long newWidth = static_cast<unsigned long>(info.width * percentage + 0.5);
        unsigned long newHeight = static_cast<unsigned long>(info.height * percentage + 0.5);

        // Ensure minimum size of 1x1
        if (newWidth == 0)
        {
            newWidth = 1;
        }
        if (newHeight == 0)
        {
            newHeight = 1;
        }

        // Use existing scale method
        return scale(newWidth, newHeight);
    }

    // Fast bilinear scaling - creates a new scaled image
    std::unique_ptr<Image> scale(unsigned long newWidth, unsigned long newHeight) const
    {
        if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
        {
            common::log_error("Image::scale - Invalid source image");
            return nullptr;
        }

        if (newWidth == 0 || newHeight == 0)
        {
            common::log_error("Image::scale - Invalid target dimensions: %lux%lu", newWidth, newHeight);
            return nullptr;
        }

        if (newWidth == info.width && newHeight == info.height)
        {
            // No scaling needed, return a deep copy
            return deepCopy();
        }

        // Create scaled image
        size_t scaledSize = newWidth * newHeight * info.pixelSizeBytes;
        auto scaledImage = std::make_unique<Image>(scaledSize);

        // Copy image info and update dimensions
        scaledImage->info = this->info;
        scaledImage->info.width = newWidth;
        scaledImage->info.height = newHeight;

        // Perform bilinear scaling
        const unsigned char* src = data_.get();
        unsigned char* dst = scaledImage->data();

        // Handle single pixel edge case
        if (info.width == 1 && info.height == 1)
        {
            // Fill entire scaled image with the single source pixel
            for (unsigned long i = 0; i < newWidth * newHeight; i++)
            {
                for (unsigned char ch = 0; ch < info.pixelSizeBytes; ch++)
                {
                    dst[i * info.pixelSizeBytes + ch] = src[ch];
                }
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
                unsigned long dstIdx = (y * newWidth + x) * info.pixelSizeBytes;

                // Interpolate each channel (works for pixelSizeBytes = 1, 3, 4, etc.)
                for (unsigned char ch = 0; ch < info.pixelSizeBytes; ch++)
                {
                    // Get source pixel values with bounds checking
                    unsigned long idx1 = (y1 * info.width + x1) * info.pixelSizeBytes + ch;
                    unsigned long idx2 = (y1 * info.width + x2) * info.pixelSizeBytes + ch;
                    unsigned long idx3 = (y2 * info.width + x1) * info.pixelSizeBytes + ch;
                    unsigned long idx4 = (y2 * info.width + x2) * info.pixelSizeBytes + ch;

                    // Additional safety check (should not be needed with proper x2/y2 calculation)
                    if (idx1 >= size_ || idx2 >= size_ || idx3 >= size_ || idx4 >= size_)
                    {
                        common::log_error(
                            "Image::scale - Source index out of bounds: idx1=%lu idx2=%lu idx3=%lu idx4=%lu size=%zu",
                            idx1, idx2, idx3, idx4, size_);
                        return nullptr;
                    }

                    double p1 = src[idx1];
                    double p2 = src[idx2];
                    double p3 = src[idx3];
                    double p4 = src[idx4];

                    // Bilinear interpolation
                    double top = p1 * (1.0 - fracX) + p2 * fracX;
                    double bottom = p3 * (1.0 - fracX) + p4 * fracX;
                    double result = top * (1.0 - fracY) + bottom * fracY;

                    dst[dstIdx + ch] = static_cast<unsigned char>(result + 0.5);
                }
            }
        }

        return scaledImage;
    }

    void move(unsigned long new_x, unsigned long new_y)
    {
        info.x = new_x;
        info.y = new_y;
    }

    std::pair<unsigned long, unsigned long> getPosition() const { return {info.x, info.y}; }

    // Paste another image using its stored coordinates
    Image& paste(const Image& other, bool expandCanvas = false)
    {
        pasteImpl(other, static_cast<long>(other.info.x), static_cast<long>(other.info.y), expandCanvas);
        return *this;
    }

    // Paste another image at specific coordinates
    Image& pasteAt(const Image& other, long x, long y, bool expandCanvas = false)
    {
        if (x < 0 || y < 0)
        {
            common::log_error("Image::pasteAt - Negative coordinates not supported: %ld, %ld", x, y);
            return *this;
        }
        pasteImpl(other, x, y, expandCanvas);
        return *this;
    }
    // converts the image to a tensor
    // The resize is smart, adding padding if necessary. Allways maintaining original aspect ratio.
    void toTensor(float* outputData, float pad, int new_width, int new_height) const
    {
        const int origW = info.width;
        const int origH = info.height;

        // Step 1: Compute scale ratio (preserve aspect ratio)
        float r = std::min(static_cast<float>(new_width) / origW, static_cast<float>(new_height) / origH);

        const int resizedW = static_cast<int>(origW * r);
        const int resizedH = static_cast<int>(origH * r);

        const int offsetX = (new_width - resizedW) / 2;
        const int offsetY = (new_height - resizedH) / 2;

        const int paddedSize = new_width * new_height;

        // TODO: Fill hole outputData with zero.
        // memset(outputData, 0, paddedSize * sizeof(float));
        const unsigned char* srcData = data_.get();

        // Resize original image using nearest neighbor
        // For each pixel in resized image
        // TODO: Test resize with bilinear interpolation or even bicubic interpolation.
        for (int h = 0; h < resizedH; ++h)
        {
            int srcH = static_cast<int>((static_cast<float>(h) / resizedH) * origH);
            if (srcH >= origH)
            {
                srcH = origH - 1;
            }

            for (int w = 0; w < resizedW; ++w)
            {
                int srcW = static_cast<int>((static_cast<float>(w) / resizedW) * origW);
                if (srcW >= origW)
                {
                    srcW = origW - 1;
                }

                int srcIdx = (srcH * origW + srcW) * 3; // RGB interleaved

                // Location in padded (output) image
                int dstH = h + offsetY;
                int dstW = w + offsetX;
                int dstIdx = dstH * new_width + dstW;

                outputData[0 * paddedSize + dstIdx] = srcData[srcIdx] / 255.0f;     // R
                outputData[1 * paddedSize + dstIdx] = srcData[srcIdx + 1] / 255.0f; // G
                outputData[2 * paddedSize + dstIdx] = srcData[srcIdx + 2] / 255.0f; // B
            }
        }
    }

    void paintPoints(const std::vector<math_utils::Point>& points, Pixel color)
    {
        // Pant each point
        for (const math_utils::Point& p : points)
        {
            // Check image bounds
            if (p.x < 0 || p.x >= static_cast<long>(info.width) || p.y < 0 || p.y >= static_cast<long>(info.height))
            {
                continue;
            }
            ppx(p.x, p.y, color);
        }
    }

  private:
    inline unsigned long index(unsigned long col, unsigned long row) const
    {
        return (row * info.width + col) * info.pixelSizeBytes;
    }

    // Helper method to copy pixels from source to destination with alpha blending
    void copyPixelsWithBlending(const Image& src, long srcGlobalX, long srcGlobalY, long canvasX, long canvasY,
                                unsigned long canvasWidth, unsigned long canvasHeight)
    {
        if (!src.data() || !data_)
        {
            common::log_error("copyPixelsWithBlending - Aborting no data");
            return;
        }

        // Calculate intersection with canvas
        long srcLeft = srcGlobalX;
        long srcTop = srcGlobalY;
        long srcRight = srcLeft + static_cast<long>(src.info.width);
        long srcBottom = srcTop + static_cast<long>(src.info.height);

        long clipLeft = std::max(srcLeft, canvasX);
        long clipTop = std::max(srcTop, canvasY);
        long clipRight = std::min(srcRight, canvasX + static_cast<long>(canvasWidth));
        long clipBottom = std::min(srcBottom, canvasY + static_cast<long>(canvasHeight));

        // Skip if no intersection
        if (clipLeft >= clipRight || clipTop >= clipBottom)
        {
            common::log_error("copyPixelsWithBlending - Aborting no intersection");
            return;
        }

        const unsigned char* srcData = src.data();
        unsigned char* dstData = data_.get();
        const unsigned char pixelSize = info.pixelSizeBytes;

        // Copy pixels row by row
        for (long y = clipTop; y < clipBottom; y++)
        {
            // Calculate row start indices for optimization
            long srcRowStart = (y - srcTop) * src.info.width;
            long dstRowStart = (y - canvasY) * canvasWidth;

            for (long x = clipLeft; x < clipRight; x++)
            {
                // Calculate column offsets
                unsigned long srcCol = static_cast<unsigned long>(x - srcLeft);
                unsigned long dstCol = static_cast<unsigned long>(x - canvasX);

                // Calculate final indices
                unsigned long srcIdx = (srcRowStart + srcCol) * pixelSize;
                unsigned long dstIdx = (dstRowStart + dstCol) * pixelSize;

                // Bounds checking
                if (srcIdx + pixelSize > src.size() || dstIdx + pixelSize > size_)
                {
                    common::log_error("Image::copyPixelsWithBlending - Index out of bounds");
                    continue;
                }


                // Handle different pixel formats
                if (pixelSize == 4) // RGBA
                {
                    unsigned char srcAlpha = srcData[srcIdx + 3];
                    if (srcAlpha == 255)
                    {
                        // Fully opaque - direct assignment is faster than memcpy for 4 bytes
                        dstData[dstIdx] = srcData[srcIdx];
                        dstData[dstIdx + 1] = srcData[srcIdx + 1];
                        dstData[dstIdx + 2] = srcData[srcIdx + 2];
                        dstData[dstIdx + 3] = srcData[srcIdx + 3];
                    }
                    else if (srcAlpha > 0)
                    {
                        // Alpha blend
                        float alpha = srcAlpha / 255.0f;
                        float invAlpha = 1.0f - alpha;

                        dstData[dstIdx] =
                            static_cast<unsigned char>(srcData[srcIdx] * alpha + dstData[dstIdx] * invAlpha);
                        dstData[dstIdx + 1] =
                            static_cast<unsigned char>(srcData[srcIdx + 1] * alpha + dstData[dstIdx + 1] * invAlpha);
                        dstData[dstIdx + 2] =
                            static_cast<unsigned char>(srcData[srcIdx + 2] * alpha + dstData[dstIdx + 2] * invAlpha);
                        dstData[dstIdx + 3] = std::max(dstData[dstIdx + 3], srcAlpha);
                    }
                    // srcAlpha == 0: skip (transparent pixel)
                }
                else if (pixelSize == 3) // RGB
                {
                    // Direct assignment for 3 bytes
                    dstData[dstIdx] = srcData[srcIdx];
                    dstData[dstIdx + 1] = srcData[srcIdx + 1];
                    dstData[dstIdx + 2] = srcData[srcIdx + 2];
                }
                else if (pixelSize == 1) // Grayscale
                {
                    dstData[dstIdx] = srcData[srcIdx];
                }
                else
                {
                    // Fallback to memcpy for other formats
                    std::memcpy(dstData + dstIdx, srcData + srcIdx, pixelSize);
                }
            }
        }
    }

    // Core paste implementation - all paste methods delegate to this
    Image& pasteImpl(const Image& other, long otherX, long otherY, bool expandCanvas)
    {
        // Validation
        if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
        {
            common::log_error("Image::paste - Invalid base image");
            return *this;
        }

        if (!other.data() || other.size() == 0 || other.info.width == 0 || other.info.height == 0)
        {
            common::log_error("Image::paste - Invalid source image to paste");
            return *this;
        }

        if (info.pixelSizeBytes != other.info.pixelSizeBytes)
        {
            common::log_error("Image::paste - Pixel format mismatch: %d vs %d", info.pixelSizeBytes,
                              other.info.pixelSizeBytes);
            return *this;
        }

        // Calculate bounds for both images
        long baseLeft = static_cast<long>(info.x);
        long baseTop = static_cast<long>(info.y);
        long baseRight = baseLeft + static_cast<long>(info.width);
        long baseBottom = baseTop + static_cast<long>(info.height);

        long otherLeft = otherX;
        long otherTop = otherY;
        long otherRight = otherLeft + static_cast<long>(other.info.width);
        long otherBottom = otherTop + static_cast<long>(other.info.height);


        // Calculate new dimensions
        long minX = std::min(baseLeft, otherLeft);
        long minY = std::min(baseTop, otherTop);
        long maxX = std::max(baseRight, otherRight);
        long maxY = std::max(baseBottom, otherBottom);

        unsigned long newWidth = static_cast<unsigned long>(maxX - minX);
        unsigned long newHeight = static_cast<unsigned long>(maxY - minY);

        bool sameCanvasSize = newWidth == info.width && newHeight == info.height && minX == baseLeft && minY == baseTop;
        if (!expandCanvas || sameCanvasSize)
        {
            // No actual expansion needed, just paste normally
            copyPixelsWithBlending(other, otherLeft, otherTop, 0, 0, info.width, info.height);
            return *this;
        }

        // Create backup of current image before resizing
        Image backup; // TODO: FIXME: THIS IS WRONG: MAYBE .
        backup.copyFrom(*this);

        common::log_info("Image::paste - Resizing canvas from %lux%lu to %lux%lu", info.width, info.height, newWidth,
                         newHeight);
        // Resize this image using existing method
        size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
        this->resize(newSize);

        // Update dimensions and position
        info.x = static_cast<unsigned long>(minX);
        info.y = static_cast<unsigned long>(minY);
        info.width = newWidth;
        info.height = newHeight;

        // // Clear the new buffer
        // if (data_)
        // {
        //     std::memset(data_.get(), 0, size_);
        // }

        // Copy original image to new position
        common::log_info("Image::paste - Copying backup image to new position");
        copyPixelsWithBlending(backup, baseLeft, baseTop, minX, minY, newWidth, newHeight);

        // Copy other image
        common::log_info("Image::paste - Copying other image to new position");
        copyPixelsWithBlending(other, otherLeft, otherTop, minX, minY, newWidth, newHeight);

        return *this;
    }

    std::shared_ptr<unsigned char> data_;
    size_t size_;
};

} // namespace funnyface

#endif // IMAGE_H
