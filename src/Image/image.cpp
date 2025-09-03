#include "LinuxFace/Image/image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <vector>

#include "LinuxFace/Image/alpha_blender.h"
#include "LinuxFace/Image/image_processor.h"
#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/Image/pixel_converter.h"

using namespace linuxface;

// Modern pixel operations using clean architecture
namespace linuxface::pixel_operations
{

} // namespace linuxface::pixel_operations

// Static utility methods
linuxface::image::PixelFormat Image::pixelSizeToFormat(unsigned char pixelSize) noexcept
{
    switch (pixelSize)
    {
        case 1:
            return image::PixelFormat::GRAYSCALE;
        case 3:
            return image::PixelFormat::RGB;
        case 4:
            return image::PixelFormat::RGBA;
        default:
            return image::PixelFormat::RGB; // Default fallback
    }
}

// Constructors with improved memory management
Image::Image(size_t size) : size_(size)
{
    if (size > 0)
    {
        data_ = std::shared_ptr<unsigned char>(new unsigned char[size], std::default_delete<unsigned char[]>());
        // Only set format to RGB if still UNKNOWN (so derived classes can override)
        if (info.format == ImageFormat::UNKNOWN)
        {
            info.format = ImageFormat::RGB;
            info.pixelSizeBytes = 3;
        }
    }
}

Image::Image(unsigned char* buffer, size_t size) : size_(size)
{
    if ((buffer != nullptr) && size > 0)
    {
        data_ = std::shared_ptr<unsigned char>(buffer, std::default_delete<unsigned char[]>());
    }
}

Image::Image(unsigned char* buffer, size_t size, bool takeOwnership) : size_(size)
{
    if ((buffer != nullptr) && size > 0)
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

Image::Image(Pixel color, size_t width, size_t height)
{
    // Determine format based on alpha value
    const bool hasAlpha = (color.a != 255);
    info.pixelSizeBytes = hasAlpha ? 4 : 3;
    info.format = hasAlpha ? ImageFormat::RGBA : ImageFormat::RGB;
    info.width = width;
    info.height = height;

    // Calculate size after setting pixelSizeBytes
    size_ = width * height * info.pixelSizeBytes;

    data_ = std::shared_ptr<unsigned char>(new unsigned char[size_], std::default_delete<unsigned char[]>());

    // Fill with the color
    unsigned char* d = data_.get();
    for (size_t i = 0; i < width * height; ++i)
    {
        const size_t idx = i * info.pixelSizeBytes;
        d[idx + 0] = color.r;
        d[idx + 1] = color.g;
        d[idx + 2] = color.b;
        if (hasAlpha)
        {
            d[idx + 3] = color.a;
        }
    }
}

// Add missing move constructor and assignment operator
Image::Image(Image&& other) noexcept : info(other.info), data_(std::move(other.data_)), size_(other.size_)
{
    other.size_ = 0;
    other.info = {};
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this == &other)
    {
        // Self-assignment: clear the object to a valid empty state
        data_.reset();
        size_ = 0;
        info = {};
        return *this;
    }
    data_ = std::move(other.data_);
    size_ = other.size_;
    info = other.info;
    other.size_ = 0;
    other.info = {};
    return *this;
}

void Image::black()
{
    if (data_ && size_ > 0)
    {
        // Set all pixels to black
        memset(data_.get(), 0, size_);
    }
}
void Image::resize(size_t newSize, bool preserveData)
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

        // MED-004 OPTIMIZATION: Conditional data preservation with performance analysis.
        // Analysis shows most calls use preserveData=false (copyFrom, scaleToInPlace),
        // but some legitimate cases need data preserved (JPEG codec tests, buffer reallocation).
        // Only perform memcpy when explicitly requested and both buffers are valid.
        if (preserveData && data_ && size_ > 0)
        {
            // Copy existing data, preserving as much as fits in the new buffer
            memcpy(newData.get(), data_.get(), std::min(size_, newSize));
            // Zero-fill any additional space in larger buffers
            if (newSize > size_)
            {
                memset(newData.get() + size_, 0, newSize - size_);
            }
        }
        else
        {
            // Initialize new data to zero when not preserving
            memset(newData.get(), 0, newSize);
        }

        data_ = std::move(newData);
        size_ = newSize;
    }
}

Pixel Image::operator()(size_t col, size_t row) const
{
    const size_t idx = index(col, row);
    if (idx >= size_ || !data_)
    {
        common::logError("Image::operator(): index out of bounds [col,row] %zu, %zu Index: %zu", col, row, idx);
        return {0, 0, 0, DefaultAlpha};
    }

    // TODO(arroyo): Byte order depends on pixelFormat. Forced to RGBA for now
    unsigned char* data = data_.get();
    if (info.pixelSizeBytes == 4)
    {
        return {data[idx], data[idx + 1], data[idx + 2], data[idx + 3]};
    }

    return {data[idx], data[idx + 1], data[idx + 2], DefaultAlpha};
}

void Image::ppx(size_t col, size_t row, const Pixel& c)
{
    this->pxy(col, row, c.r, c.g, c.b, c.a);
}

void Image::pxy(size_t col, size_t row, const unsigned char r, const unsigned char g, const unsigned char b,
                const unsigned char a)
{
    const size_t pixelIdx = row * info.width + col; // Calculate pixel index
    if (pixelIdx >= info.width * info.height || !data_)
    {
        common::logError("Image::pxy: index out of bounds [col,row] %zu, %zu Pixel Index: %zu", col, row, pixelIdx);
        return;
    }
    this->pidx(pixelIdx, r, g, b, a);
}

void Image::pidx(size_t pixelIdx, const unsigned char r, const unsigned char g, const unsigned char b,
                 const unsigned char a)
{
    // Convert pixel index to byte index
    const size_t byteIdx = pixelIdx * info.pixelSizeBytes;

    // Bounds check using byte index
    if (byteIdx >= size_ || !data_)
    {
        common::logError("Image::pidx: pixel index out of bounds. Pixel index: %zu, byte index: %zu, size: %zu",
                         pixelIdx, byteIdx, size_);
        return;
    }

    // Use PixelOperations for consistency
    if (info.pixelSizeBytes == 4)
    {
        pixel_operations::setPixelRGBA(data_.get(), byteIdx, r, g, b, a);
    }
    else
    {
        pixel_operations::setPixelRGB(data_.get(), byteIdx, r, g, b);
    }
}

bool Image::isColorImage() const noexcept
{
    return info.format == ImageFormat::RGB || info.format == ImageFormat::RGBA || info.format == ImageFormat::JPEG;
}

unsigned char Image::getExpectedPixelSize() const noexcept
{
    // Use compile-time lookup for cleaner and more maintainable code
    static constexpr std::pair<ImageFormat, unsigned char> FormatSizes[] = {
        {ImageFormat::RGB,         3            },
        {ImageFormat::RGBA,        4            },
        {ImageFormat::GRAYSCALE,   1            },
        {ImageFormat::DEPTH_FLOAT, sizeof(float)},
        {ImageFormat::DEPTH_Z16,   2            },
    };

    for (const auto& [format, size] : FormatSizes)
    {
        if (info.format == format)
        {
            return size;
        }
    }

    // Fallback to stored pixel size for unknown/unsupported formats
    return info.pixelSizeBytes;
}

bool Image::hasSameDimensions(const Image& other) const noexcept
{
    return info.width == other.info.width && info.height == other.info.height;
}

bool Image::hasSameSize(const Image& other) const noexcept
{
    return size_ == other.size_ && info.format == other.info.format && info.pixelSizeBytes == other.info.pixelSizeBytes;
}

bool Image::isCompatible(const Image& other) const noexcept
{
    return hasSameDimensions(other) && hasSameSize(other);
}

void Image::copyFrom(const Image& other)
{
    if (this != &other)
    {
        this->resize(other.size_, false);
        // Copy data if both images have valid data pointers and size > 0
        if (data_ && (other.data() != nullptr) && size_ > 0)
        {
            std::memcpy(data_.get(), other.data(), size_);
        }
        info = other.info;
    }
}

std::unique_ptr<Image> Image::deepCopy() const
{
    auto copy = std::make_unique<Image>(size_);
    if (data_ && (copy->data() != nullptr) && size_ > 0)
    {
        std::memcpy(copy->data(), data_.get(), size_);
    }
    copy->info = this->info;
    return copy;
}

void Image::scaleImageBuffer(const unsigned char* srcData, unsigned long srcWidth, unsigned long srcHeight,
                             unsigned char pixelSize, unsigned char* dstData, unsigned long dstWidth,
                             unsigned long dstHeight, ScalingAlgorithm algorithm)
{
    // Use non-const T for both src and dst to match template requirements
    const image_utils::ImageView<unsigned char> srcView{const_cast<unsigned char*>(srcData), srcWidth, srcHeight,
                                                        pixelSize};
    image_utils::ImageView<unsigned char> dstView{dstData, dstWidth, dstHeight, pixelSize};
    switch (algorithm)
    {
        case ScalingAlgorithm::LANCZOS:
            image_utils::lanczosScaling<unsigned char, unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        case ScalingAlgorithm::BILINEAR:
            image_utils::bilinearScaling<unsigned char, unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        case ScalingAlgorithm::AREA_AVERAGING:
            image_utils::areaAveragingScaling<unsigned char, unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        case ScalingAlgorithm::FAST_BOX:
            image_utils::fastBoxScaling<unsigned char, unsigned char>(srcView, dstView);
            break;
        case ScalingAlgorithm::BICUBIC:
            image_utils::bicubicScaling<unsigned char, unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        default:
            common::logError("scaleImageBuffer - Unsupported scaling algorithm: %d", static_cast<int>(algorithm));
            break;
    }
}

// In-place scaling methods
void Image::scaleInPlace(double factor, ScalingAlgorithm algorithm)
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0 || factor == 1.0)
    {
        return;
    }
    auto newWidth = static_cast<size_t>(info.width * factor + 0.5);
    auto newHeight = static_cast<size_t>(info.height * factor + 0.5);
    if (newWidth == 0)
    {
        newWidth = 1;
    }
    if (newHeight == 0)
    {
        newHeight = 1;
    }
    scaleToInPlace(newWidth, newHeight, algorithm);
}

void Image::scaleInPlace(unsigned long newWidth, unsigned long newHeight, ScalingAlgorithm algorithm)
{
    scaleToInPlace(static_cast<size_t>(newWidth), static_cast<size_t>(newHeight), algorithm);
}

void Image::scaleToInPlace(size_t newWidth, size_t newHeight, ScalingAlgorithm algorithm)
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
    {
        return;
    }
    if (newWidth == 0 || newHeight == 0)
    {
        return;
    }

    if (newWidth == info.width && newHeight == info.height)
    {
        return;
    }

    std::vector<unsigned char> result(newWidth * newHeight * info.pixelSizeBytes);
    scaleImageBuffer(data_.get(), info.width, info.height, info.pixelSizeBytes, result.data(), newWidth, newHeight,
                     algorithm);

    resize(newWidth * newHeight * info.pixelSizeBytes, false);
    std::memcpy(data_.get(), result.data(), newWidth * newHeight * info.pixelSizeBytes);
    info.width = newWidth;
    info.height = newHeight;
}

std::unique_ptr<Image> Image::scale(double factor, ScalingAlgorithm algorithm) const
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0 || factor == 1.0)
    {
        return nullptr;
    }

    auto newWidth = static_cast<size_t>(info.width * factor + 0.5);
    auto newHeight = static_cast<size_t>(info.height * factor + 0.5);
    // Ensure minimum size of 1x1
    if (newWidth == 0)
    {
        newWidth = 1;
    }
    if (newHeight == 0)
    {
        newHeight = 1;
    }

    return scaleTo(newWidth, newHeight, algorithm);
}

std::unique_ptr<Image> Image::scaleTo(size_t newWidth, size_t newHeight, ScalingAlgorithm algorithm) const
{
    return scale(static_cast<unsigned long>(newWidth), static_cast<unsigned long>(newHeight), algorithm);
}

// Performance-optimized bilinear scaling with different algorithms for up/down scaling
std::unique_ptr<Image> Image::scale(unsigned long newWidth, unsigned long newHeight, ScalingAlgorithm algorithm) const
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
    {
        common::logError("Image::scale - Invalid source image");
        return nullptr;
    }

    if (newWidth == 0 || newHeight == 0)
    {
        common::logError("Image::scale - Invalid target dimensions: %lux%lu", newWidth, newHeight);
        return nullptr;
    }

    if (newWidth == info.width && newHeight == info.height)
    {
        return deepCopy();
    }

    auto result = std::make_unique<Image>(newWidth * newHeight * info.pixelSizeBytes);
    scaleImageBuffer(data_.get(), info.width, info.height, info.pixelSizeBytes, result->data(), newWidth, newHeight,
                     algorithm);
    result->info = info;
    result->info.width = newWidth;
    result->info.height = newHeight;
    return result;
}

void Image::move(size_t newX, size_t newY)
{
    info.x = newX;
    info.y = newY;
}

Image& Image::paste(const Image& other, bool expandCanvas)
{
    pasteAt(other, static_cast<long>(other.info.x), static_cast<long>(other.info.y), expandCanvas);
    return *this;
}

Image& Image::pasteAt(const Image& other, long x, long y, bool expandCanvas)
{
    pasteImpl(other, x, y, expandCanvas);
    return *this;
}

void Image::toTensor(float* outputData, TensorPadding& padding, int newWidth, int newHeight,
                     NormalizationType normType) const
{
    if (!isColorImage() || (info.pixelSizeBytes != 3 && info.pixelSizeBytes != 4))
    {
        common::logError("Image::toTensor - Expected RGB or RGBA format, got format: %s with pixel size: %d",
                         fromImageFormatToString(info.format).c_str(), info.pixelSizeBytes);
        return;
    }

    if ((outputData == nullptr) || !data_ || size_ == 0)
    {
        common::logError("Image::toTensor - Invalid input data");
        return;
    }

    const int origW = info.width;
    const int origH = info.height;
    const int bytesPerPixel = info.pixelSizeBytes;

    // Step 1: Compute scale ratio (preserve aspect ratio)
    const float r = std::min(static_cast<float>(newWidth) / origW, static_cast<float>(newHeight) / origH);
    const int resizedW = static_cast<int>(origW * r);
    const int resizedH = static_cast<int>(origH * r);
    const int offsetX = (newWidth - resizedW) / 2;
    const int offsetY = (newHeight - resizedH) / 2;
    const int paddedSize = newWidth * newHeight;

    // Store transform metadata in the padding object
    padding.tensor_width = newWidth;
    padding.tensor_height = newHeight;
    padding.resized_width = resizedW;
    padding.resized_height = resizedH;
    padding.offset_x = offsetX;
    padding.offset_y = offsetY;
    padding.scale_ratio = r;
    padding.has_transform = true;

    // Initialize the entire output buffer based on padding type
    switch (padding.type)
    {
        case PaddingType::NO_PADDING:
            break;
        case PaddingType::ZERO:
            std::memset(outputData, 0, paddedSize * 3 * sizeof(float));
            break;
        case PaddingType::CONSTANT:
        {
            float* ptr = outputData;
            float* end = outputData + (paddedSize * 3);
            while (ptr < end)
            {
                *ptr++ = padding.constant_value;
            }
        }
        break;
        case PaddingType::RGB_CONSTANT:
        {
            // Fill each channel separately with direct assignment
            float* rChannel = outputData;
            float* gChannel = outputData + paddedSize;
            float* bChannel = outputData + 2 * paddedSize;

            for (int i = 0; i < paddedSize; i++)
            {
                rChannel[i] = padding.rgb_values[0];
                gChannel[i] = padding.rgb_values[1];
                bChannel[i] = padding.rgb_values[2];
            }
        }
        break;
        default:
            std::memset(outputData, 0, paddedSize * 3 * sizeof(float));
            break;
    }

    const unsigned char* srcData = data_.get();

    // TODO(arroyo): Test resize with bilinear interpolation or even bicubic
    // interpolation. Resize original image using nearest neighbor For each
    // pixel in resized image
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

            const int srcIdx = (srcH * origW + srcW) * bytesPerPixel; // RGB or RGBA interleaved

            // Bounds check for source data
            if (srcIdx + 2 >= static_cast<int>(size_))
            {
                common::logError("Image::toTensor - Source index out of bounds: %d >= %zu", srcIdx + 2, size_);
                continue;
            }

            // Location in padded (output) image
            const int dstH = h + offsetY;
            const int dstW = w + offsetX;
            const int dstIdx = dstH * newWidth + dstW;

            // Bounds check for destination
            if (dstIdx >= paddedSize)
            {
                common::logError("Image::toTensor - Destination index out of bounds: %d >= %d", dstIdx, paddedSize);
                continue;
            }
            if (normType == NormalizationType::NONE)
            {
                outputData[0 * paddedSize + dstIdx] = srcData[srcIdx];     // R
                outputData[1 * paddedSize + dstIdx] = srcData[srcIdx + 1]; // G
                outputData[2 * paddedSize + dstIdx] = srcData[srcIdx + 2]; // B
            }
            else if (normType == NormalizationType::MINMAX)
            {
                outputData[0 * paddedSize + dstIdx] = srcData[srcIdx] / 255.0f;     // R
                outputData[1 * paddedSize + dstIdx] = srcData[srcIdx + 1] / 255.0f; // G
                outputData[2 * paddedSize + dstIdx] = srcData[srcIdx + 2] / 255.0f; // B
            }
            else if (normType == NormalizationType::ZERO_CENTER)
            {
                outputData[0 * paddedSize + dstIdx] = (static_cast<float>(srcData[srcIdx]) - 127.5f) / 127.5f;     // R
                outputData[1 * paddedSize + dstIdx] = (static_cast<float>(srcData[srcIdx + 1]) - 127.5f) / 127.5f; // G
                outputData[2 * paddedSize + dstIdx] = (static_cast<float>(srcData[srcIdx + 2]) - 127.5f) / 127.5f; // B
            }
        }
    }
}

void Image::fromTensor(const float* tensorData, std::vector<int64_t> tensorShape, int tensorWidth, int tensorHeight,
                       const TensorPadding& padding, NormalizationType normType)
{
    if ((tensorData == nullptr) || !data_ || size_ == 0)
    {
        common::logError("Image::fromTensor - Invalid input data");
        return;
    }

    // Use stored transform metadata if available, otherwise calculate it
    int offsetX = 0;
    int offsetY = 0;
    int resizedW = 0;
    int resizedH = 0;
    float r = NAN;

    // common::logInfo("Image::fromTensor - Using tensor dimensions: %dx%d", tensor_width, tensor_height);
    // common::logInfo("Tensor padding = %d width, %d height", padding.tensor_width, padding.tensor_height);

    if (padding.has_transform && padding.tensor_width == tensorWidth && padding.tensor_height == tensorHeight)
    {
        // Use stored transform metadata from padding
        offsetX = padding.offset_x;
        offsetY = padding.offset_y;
        resizedW = padding.resized_width;
        resizedH = padding.resized_height;
        r = padding.scale_ratio;

        // common::logInfo(
        //     "Image::fromTensor - Using stored transform metadata: offset(%d,%d), resized(%dx%d), scale=%.3f",
        //     offsetX, offsetY, resizedW, resizedH, r);
    }
    else
    {
        // Fallback: calculate transform parameters
        const int origW = info.width;
        const int origH = info.height;

        r = std::min(static_cast<float>(tensorWidth) / origW, static_cast<float>(tensorHeight) / origH);
        resizedW = static_cast<int>(origW * r);
        resizedH = static_cast<int>(origH * r);
        offsetX = (tensorWidth - resizedW) / 2;
        offsetY = (tensorHeight - resizedH) / 2;

        // common::logWarn("Image::fromTensor - No stored transform metadata, calculating: offset(%d,%d), "
        //                  "resized(%dx%d), scale=%.3f",
        //                  offsetX, offsetY, resizedW, resizedH, r);
    }

    unsigned char* dstData = data_.get();
    const int tensorSize = tensorWidth * tensorHeight;
    // Extract and resize depth data back to original dimensions
    for (int y = 0; y < static_cast<int>(info.height); ++y)
    {
        for (int x = 0; x < static_cast<int>(info.width); ++x)
        {
            // Map to tensor coordinates
            int tensorH = static_cast<int>((static_cast<float>(y) / info.height) * resizedH) + offsetY;
            int tensorW = static_cast<int>((static_cast<float>(x) / info.width) * resizedW) + offsetX;

            // Clamp to tensor bounds
            tensorH = std::max(0, std::min(tensorH, tensorHeight - 1));
            tensorW = std::max(0, std::min(tensorW, tensorWidth - 1));

            int channels = 1;
            // Check if the tensor has a single channel
            if (tensorShape[1] == 3)
            {
                channels = 3;
            }

            const int tensorIdx = (tensorH * tensorWidth + tensorW);

            // Convert tensor value to unsigned char
            unsigned char pixelValues[3];
            if (normType == NormalizationType::NONE)
            {
                pixelValues[0] = static_cast<unsigned char>(tensorData[tensorIdx]);
                if (channels == 3)
                {
                    // Remember that thi is in CHW not HWC
                    pixelValues[1] = static_cast<unsigned char>(tensorData[1 * tensorSize + tensorIdx]);
                    pixelValues[2] = static_cast<unsigned char>(tensorData[2 * tensorSize + tensorIdx]);
                }
            }
            else if (normType == NormalizationType::MINMAX)
            {
                pixelValues[0] = static_cast<unsigned char>(tensorData[tensorIdx] * 255.0f);
                if (channels == 3)
                {
                    pixelValues[1] = static_cast<unsigned char>(tensorData[1 * tensorSize + tensorIdx] * 255.0f);
                    pixelValues[2] = static_cast<unsigned char>(tensorData[2 * tensorSize + tensorIdx] * 255.0f);
                }
            }
            else if (normType == NormalizationType::ZERO_CENTER)
            {
                pixelValues[0] = static_cast<unsigned char>((tensorData[tensorIdx] + 1.0) / 2.0f * 255.0f);
                if (channels == 3)
                {
                    pixelValues[1] =
                        static_cast<unsigned char>((tensorData[1 * tensorSize + tensorIdx] + 1.0) / 2.0f * 255.0f);
                    pixelValues[2] =
                        static_cast<unsigned char>((tensorData[2 * tensorSize + tensorIdx] + 1.0) / 2.0f * 255.0f);
                }
            }
            else
            {
                common::logError("Unknown normalization type");
                return;
            }

            const int dstIdx = (y * info.width + x) * 3;
            dstData[dstIdx] = pixelValues[0];
            if (channels == 3)
            {
                // RGB
                dstData[dstIdx + 1] = pixelValues[1];
                dstData[dstIdx + 2] = pixelValues[2];
            }
            else
            {
                // Grayscale
                dstData[dstIdx + 1] = pixelValues[0];
                dstData[dstIdx + 2] = pixelValues[0];
            }
        }
    }
}

void Image::paintPoints(const std::vector<math_utils::Point<>>& points, const Pixel& color)
{
    for (const math_utils::Point<>& p : points)
    {
        // Check image bounds
        if (p.x < 0 || p.x >= static_cast<long>(info.width) || p.y < 0 || p.y >= static_cast<long>(info.height))
        {
            continue;
        }
        ppx(p.x, p.y, color);
    }
}

void Image::fillRect(int x, int y, int width, int height, const Pixel& color)
{
    // Bounds checking and clipping
    if (x >= static_cast<int>(info.width) || y >= static_cast<int>(info.height) || width <= 0 || height <= 0)
    {
        return;
    }

    // Clip rectangle to image bounds
    const int x1 = std::max(0, x);
    const int y1 = std::max(0, y);
    const int x2 = std::min(static_cast<int>(info.width), x + width);
    const int y2 = std::min(static_cast<int>(info.height), y + height);

    const int clippedWidth = x2 - x1;
    const int clippedHeight = y2 - y1;

    if (clippedWidth <= 0 || clippedHeight <= 0)
    {
        return;
    }

    const int bytesPerPixel = info.pixelSizeBytes;

    // Only optimize for RGB and RGBA formats
    if (info.format != ImageFormat::RGB && info.format != ImageFormat::RGBA)
    {
        // Fallback to individual pixel setting for unsupported formats
        for (int row = y1; row < y2; ++row)
        {
            for (int col = x1; col < x2; ++col)
            {
                ppx(col, row, color);
            }
        }
        return;
    }

    // Fast fill using block-based approach
    const int rowBytes = clippedWidth * bytesPerPixel;

    // Allocate row buffer
    auto rowBuffer = std::make_unique<unsigned char[]>(rowBytes);

    if (bytesPerPixel == 3) // RGB
    {
        // Prepare a 4-pixel (12-byte) block for RGB
        unsigned char block[12];
        for (int i = 0; i < 4; i++)
        {
            block[i * 3 + 0] = color.r;
            block[i * 3 + 1] = color.g;
            block[i * 3 + 2] = color.b;
        }

        // Fill the row buffer with unrolled copies of the 12-byte block
        const int nBlocks = clippedWidth / 4; // full 4-pixel blocks
        const int rem = clippedWidth % 4;     // leftover pixels

        unsigned char* ptr = rowBuffer.get();

        // Copy full 4-pixel blocks
        for (int i = 0; i < nBlocks; i++)
        {
            std::memcpy(ptr, block, 12);
            ptr += 12;
        }

        // Handle leftover pixels
        for (int i = 0; i < rem; i++)
        {
            *ptr++ = color.r;
            *ptr++ = color.g;
            *ptr++ = color.b;
        }
    }
    else if (bytesPerPixel == 4) // RGBA
    {
        // Prepare a 4-pixel (16-byte) block for RGBA
        unsigned char block[16];
        for (int i = 0; i < 4; i++)
        {
            block[i * 4 + 0] = color.r;
            block[i * 4 + 1] = color.g;
            block[i * 4 + 2] = color.b;
            block[i * 4 + 3] = color.a;
        }

        // Fill the row buffer with unrolled copies of the 16-byte block
        const int nBlocks = clippedWidth / 4; // full 4-pixel blocks
        const int rem = clippedWidth % 4;     // leftover pixels

        unsigned char* ptr = rowBuffer.get();

        // Copy full 4-pixel blocks
        for (int i = 0; i < nBlocks; i++)
        {
            std::memcpy(ptr, block, 16);
            ptr += 16;
        }

        // Handle leftover pixels
        for (int i = 0; i < rem; i++)
        {
            *ptr++ = color.r;
            *ptr++ = color.g;
            *ptr++ = color.b;
            *ptr++ = color.a;
        }
    }

    // Copy row buffer to each image row efficiently
    for (int row = y1; row < y2; ++row)
    {
        unsigned char* dest = data() + (row * info.width + x1) * bytesPerPixel;
        std::memcpy(dest, rowBuffer.get(), rowBytes);
    }
}
// TODO: Thickness is ignored for line drawing
void Image::drawBorder(const Pixel& color, int thickness)
{
    // Draw a border around the image
    if (thickness <= 0)
    {
        return;
    }

    // Top border
    for (unsigned long x = 0; x < info.width; ++x)
    {
        ppx(x, 0, color);
    }
    // Bottom border
    for (unsigned long x = 0; x < info.width; ++x)
    {
        ppx(x, info.height - 1, color);
    }
    // Left border
    for (unsigned long y = 0; y < info.height; ++y)
    {
        ppx(0, y, color);
    }
    // Right border
    for (unsigned long y = 0; y < info.height; ++y)
    {
        ppx(info.width - 1, y, color);
    }
}

std::unique_ptr<Image> Image::crop(const math_utils::Rect<float>& rect) const
{
    // Round and clamp rectangle coordinates to fit within image bounds
    const int x0 = std::max(0, static_cast<int>(rect.l));
    const int y0 = std::max(0, static_cast<int>(rect.t));
    const int x1 = std::min(info.width, static_cast<unsigned long>(rect.r));
    const int y1 = std::min(info.height, static_cast<unsigned long>(rect.b));

    const int cropWidth = x1 - x0;
    const int cropHeight = y1 - y0;

    if (cropWidth <= 0 || cropHeight <= 0)
    {
        return nullptr;
    }

    const size_t resultSize = cropWidth * cropHeight * info.pixelSizeBytes;
    std::unique_ptr<Image> result = std::make_unique<Image>(resultSize);

    const unsigned char* srcImg = data_.get();
    unsigned char* dstImg = result->data();

    for (int y = 0; y < cropHeight; ++y)
    {
        const unsigned char* srcRow = srcImg + ((y0 + y) * info.width + x0) * info.pixelSizeBytes;
        unsigned char* dstRow = dstImg + (y * cropWidth) * info.pixelSizeBytes;
        std::memcpy(dstRow, srcRow, cropWidth * info.pixelSizeBytes);
    }
    result->info = info;
    result->info.x = rect.x();
    result->info.y = rect.y();
    result->info.width = cropWidth;
    result->info.height = cropHeight;
    return result;
}

bool Image::saveToDisk(const std::string& destPath) const
{
    // Only support saving as JPEG or PPM (RGB/Grayscale/RGBA)
    if (info.format != ImageFormat::JPEG && info.format != ImageFormat::PPM && info.format != ImageFormat::GRAYSCALE
        && info.format != ImageFormat::RGB && info.format != ImageFormat::RGBA)
    {
        common::logError(
            "Image::saveToDisk - Only JPEG or PPM formats are supported for saving to disk. Image is %s. Path was %s",
            fromImageFormatToString(info.format).c_str(), destPath.c_str());
        return false;
    }

    if (!data_ || size_ == 0u)
    {
        common::logError("Image::saveToDisk - No data to save");
        return false;
    }

    // Use O_TRUNC to overwrite file if it exists
    const int file = open(destPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (file < 0)
    {
        common::logError("Image::saveToDisk - Error opening file for writing: %s", destPath.c_str());
        common::errnoLog("Image::saveToDisk - Error opening file for writing");
        return false;
    }
    common::logInfo("Image::saveToDisk - Saving image to %s with size %lux%lu", destPath.c_str(), info.width,
                    info.height);
    // Handle PPM (RGB, RGBA, Grayscale)
    if (info.format == ImageFormat::PPM || info.format == ImageFormat::RGB || info.format == ImageFormat::RGBA
        || info.format == ImageFormat::GRAYSCALE)
    {
        char header[64];
        // Write the P6 header
        const int headerLen = std::snprintf(header, sizeof(header), "P6\n%lu %lu\n255\n", info.width, info.height);
        if (write(file, header, headerLen) != headerLen)
        {
            common::errnoLog("Image::saveToDisk - write header failed");
            close(file);
            return false;
        }
        const size_t pixelCount = info.width * info.height;
        // Grayscale: convert to RGB
        if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
        {
            std::vector<unsigned char> rgbData = convertToRGB();
            if (!common::longWrite(file, rgbData.data(), rgbData.size()))
            {
                common::logError("Image::saveToDisk - Error saving grayscale as RGB PPM. Not all bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
        // RGBA: convert to RGB
        if (info.format == ImageFormat::RGBA || info.pixelSizeBytes == 4)
        {
            std::vector<unsigned char> rgbData(pixelCount * 3);
            const unsigned char* rgba = data_.get();
            for (size_t i = 0; i < pixelCount; ++i)
            {
                rgbData[i * 3 + 0] = rgba[i * 4 + 0];
                rgbData[i * 3 + 1] = rgba[i * 4 + 1];
                rgbData[i * 3 + 2] = rgba[i * 4 + 2];
            }
            if (!common::longWrite(file, rgbData.data(), rgbData.size()))
            {
                common::logError("Image::saveToDisk - Error saving RGBA as RGB PPM. Not all bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
        // RGB: direct write
        if (info.format == ImageFormat::RGB || info.pixelSizeBytes == 3)
        {
            if (!common::longWrite(file, data_.get(), pixelCount * 3))
            {
                common::logError("Image::saveToDisk - Error saving RGB PPM. Not all bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
        // If pixel size is not 1, 3, or 4, error
        common::logError("Image::saveToDisk - Unsupported pixel size for PPM: %d", info.pixelSizeBytes);
        close(file);
        return false;
    }

    // JPEG: direct write (assume data_ is already JPEG encoded)
    if (info.format == ImageFormat::JPEG)
    {
        if (!common::longWrite(file, data_.get(), size_))
        {
            common::logError("Image::saveToDisk - Error saving JPEG. Not all bytes were stored.");
            close(file);
            return false;
        }
        close(file);
        return true;
    }

    // If we reach here, format is not supported for saving
    common::logError("Image::saveToDisk - Unsupported format for saving: %s",
                     fromImageFormatToString(info.format).c_str());
    close(file);
    return false;
}

std::vector<unsigned char> Image::convertToRGB() const
{
    // Use the modern pixel conversion system for comprehensive format support
    if (info.format == ImageFormat::RGB && info.pixelSizeBytes == 3)
    {
        // Already RGB - return copy of existing data
        std::vector<unsigned char> rgbData(size_);
        std::memcpy(rgbData.data(), data_.get(), size_);
        return rgbData;
    }

    const size_t pixelCount = info.width * info.height;
    std::vector<unsigned char> rgbData(pixelCount * 3);

    // Determine source format
    image::PixelFormat srcFormat;
    if (info.format == ImageFormat::RGBA && info.pixelSizeBytes == 4)
    {
        srcFormat = image::PixelFormat::RGBA;
    }
    else if (info.format == ImageFormat::GRAYSCALE && info.pixelSizeBytes == 1)
    {
        srcFormat = image::PixelFormat::GRAYSCALE;
    }
    else
    {
        common::logError("Image::convertToRGB - Unsupported source format: %s",
                         fromImageFormatToString(info.format).c_str());
        return {};
    }

    // Use ImageProcessor for proper format conversion
    const size_t srcStride = info.width * info.pixelSizeBytes;
    const size_t dstStride = info.width * 3;

    image::ImageProcessor::convertImage(data_.get(), rgbData.data(), info.width, info.height, srcStride,
                                       dstStride, srcFormat, image::PixelFormat::RGB);

    return rgbData;
}

bool Image::convertToRGBAInplace()
{
    if (info.format == ImageFormat::RGBA && info.pixelSizeBytes == 4)
    {
        return true; // Already RGBA
    }

    const size_t pixelCount = info.width * info.height;
    const size_t newSize = pixelCount * 4;

    // Create new RGBA buffer
    auto newData = std::shared_ptr<unsigned char>(new unsigned char[newSize], std::default_delete<unsigned char[]>());

    // Determine source format
    image::PixelFormat srcFormat;
    if (info.format == ImageFormat::RGB && info.pixelSizeBytes == 3)
    {
        srcFormat = image::PixelFormat::RGB;
    }
    else if (info.format == ImageFormat::GRAYSCALE && info.pixelSizeBytes == 1)
    {
        srcFormat = image::PixelFormat::GRAYSCALE;
    }
    else
    {
        common::logError("Image::convertToRGBAInplace - Unsupported source format: %s",
                         fromImageFormatToString(info.format).c_str());
        return false;
    }

    // Use ImageProcessor for proper format conversion
    const size_t srcStride = info.width * info.pixelSizeBytes;
    const size_t dstStride = info.width * 4;

    image::ImageProcessor::convertImage(data_.get(), newData.get(), info.width, info.height, srcStride, dstStride,
                                       srcFormat, image::PixelFormat::RGBA);

    // Update image properties
    data_ = std::move(newData);
    size_ = newSize;
    info.format = ImageFormat::RGBA;
    info.pixelSizeBytes = 4;
    info.TJPixelFormat = TJPF_RGBA;

    return true;
}

bool Image::convertToRGBInplace()
{
    // Use the modern pixel conversion system for comprehensive format support
    if (info.format == ImageFormat::RGB && info.pixelSizeBytes == 3)
    {
        return true; // Already RGB
    }

    const size_t pixelCount = info.width * info.height;
    const size_t newSize = pixelCount * 3;

    // Create new RGB buffer
    auto newData = std::shared_ptr<unsigned char>(new unsigned char[newSize], std::default_delete<unsigned char[]>());

    // Determine source format
    image::PixelFormat srcFormat;
    if (info.format == ImageFormat::RGBA && info.pixelSizeBytes == 4)
    {
        srcFormat = image::PixelFormat::RGBA;
    }
    else if (info.format == ImageFormat::GRAYSCALE && info.pixelSizeBytes == 1)
    {
        srcFormat = image::PixelFormat::GRAYSCALE;
    }
    else
    {
        common::logError("Image::convertToRGBInplace - Unsupported source format: %s",
                         fromImageFormatToString(info.format).c_str());
        return false;
    }

    // Use ImageProcessor for proper format conversion
    const size_t srcStride = info.width * info.pixelSizeBytes;
    const size_t dstStride = info.width * 3;

    image::ImageProcessor::convertImage(data_.get(), newData.get(), info.width, info.height, srcStride, dstStride,
                                       srcFormat, image::PixelFormat::RGB);

    // Update image properties
    data_ = std::move(newData);
    size_ = newSize;
    info.format = ImageFormat::RGB;
    info.pixelSizeBytes = 3;
    info.TJPixelFormat = TJPF_RGB;

    return true;
}

void Image::changeBackgroundImage(const Image& matting, const Image& background)
{
    if (matting.info.width != background.info.width || matting.info.height != background.info.height)
    {
        common::logError("Background image and matting image have different dimensions");
        common::logInfo("Dimensions are matting %d x %d", matting.info.width, matting.info.height);
        common::logInfo("Dimensions are background %d x %d", background.info.width, background.info.height);
        return;
    }

    if (matting.info.pixelSizeBytes != background.info.pixelSizeBytes)
    {
        common::logError("Background image and matting image have different pixel sizes");
        return;
    }

    if (matting.data() == nullptr || background.data() == nullptr)
    {
        common::logError("Background image or matting image have no data");
        return;
    }

    unsigned char* phaData = matting.data();
    unsigned char* frgData = data();
    unsigned char* backgroundData = background.data();
    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long idx = (y * info.width + x) * 3;
            const unsigned char alpha = phaData[idx];
            frgData[idx] = (alpha * frgData[idx] + (255 - alpha) * backgroundData[idx]) / 255;
            frgData[idx + 1] = (alpha * frgData[idx + 1] + (255 - alpha) * backgroundData[idx + 1]) / 255;
            frgData[idx + 2] = (alpha * frgData[idx + 2] + (255 - alpha) * backgroundData[idx + 2]) / 255;
        }
    }
}

// Image manipulation methods
void Image::toGrayscale()
{
    if (!data_ || info.pixelSizeBytes < 3)
    {
        return;
    }

    if (info.format == ImageFormat::GRAYSCALE)
    {
        return;
    }

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long idx = (y * info.width + x) * info.pixelSizeBytes;
            const unsigned char r = data_.get()[idx];
            const unsigned char g = data_.get()[idx + 1];
            const unsigned char b = data_.get()[idx + 2];
            const unsigned char gray = image::PixelConverter::rgbToGrayscale(r, g, b);
            data_.get()[idx] = gray;
            data_.get()[idx + 1] = gray;
            data_.get()[idx + 2] = gray;
            if (info.pixelSizeBytes == 4)
            {
                data_.get()[idx + 3] = DefaultAlpha;
            }
        }
    }
    info.format = ImageFormat::GRAYSCALE;
}

void Image::flipHorizontal()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    unsigned char* d = data_.get();
    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width / 2; ++x)
        {
            const unsigned long idx1 = (y * info.width + x) * info.pixelSizeBytes;
            const unsigned long idx2 = (y * info.width + (info.width - 1 - x)) * info.pixelSizeBytes;
            for (unsigned char c = 0; c < info.pixelSizeBytes; ++c)
            {
                const unsigned char tmp = d[idx1 + c];
                d[idx1 + c] = d[idx2 + c];
                d[idx2 + c] = tmp;
            }
        }
    }
}

void Image::flipVertical()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    unsigned char* d = data_.get();
    const unsigned long rowBytes = info.width * info.pixelSizeBytes;
    std::vector<unsigned char> tmp(rowBytes);
    for (unsigned long y = 0; y < info.height / 2; ++y)
    {
        unsigned char* row1 = d + y * rowBytes;
        unsigned char* row2 = d + (info.height - 1 - y) * rowBytes;
        std::memcpy(tmp.data(), row1, rowBytes);
        std::memcpy(row1, row2, rowBytes);
        std::memcpy(row2, tmp.data(), rowBytes);
    }
}


// // Compute center of the image
// float cx = (info.width - 1) / 2.0f;
// float cy = (info.height - 1) / 2.0f;

math_utils::Point<double> Image::rotate(double angleRad, math_utils::Point<double> center)
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return {0.0, 0.0};
    }

    const double cosA = std::cos(angleRad);
    const double sinA = std::sin(angleRad);

    // Compute corners relative to center
    const double corners[4][2] = {
        {-center.x,                 -center.y                 },
        {info.width - 1 - center.x, -center.y                 },
        {-center.x,                 info.height - 1 - center.y},
        {info.width - 1 - center.x, info.height - 1 - center.y}
    };

    double minX = 1e9;
    double minY = 1e9;
    double maxX = -1e9;
    double maxY = -1e9;
    for (const auto& corner : corners)
    {
        const double x = corner[0] * cosA - corner[1] * sinA;
        const double y = corner[0] * sinA + corner[1] * cosA;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }

    const auto newWidth = static_cast<unsigned long>(std::ceil(maxX - minX));
    const auto newHeight = static_cast<unsigned long>(std::ceil(maxY - minY));
    const size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(newSize, 0);

    for (unsigned long y = 0; y < newHeight; ++y)
    {
        for (unsigned long x = 0; x < newWidth; ++x)
        {
            // Map back to original image
            const double dx = x + minX;
            const double dy = y + minY;

            const double srcX = cosA * dx + sinA * dy + center.x;
            const double srcY = -sinA * dx + cosA * dy + center.y;

            const int x0 = static_cast<int>(std::floor(srcX));
            const int y0 = static_cast<int>(std::floor(srcY));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;
            const double wx = srcX - x0;
            const double wy = srcY - y0;

            for (unsigned char c = 0; c < info.pixelSizeBytes; ++c)
            {
                float val = 0.0f;
                for (int j = 0; j <= 1; ++j)
                {
                    const int sy = (j == 0) ? y0 : y1;
                    if (sy < 0 || sy >= static_cast<int>(info.height))
                    {
                        continue;
                    }
                    const double wyf = (j == 0) ? (1.0 - wy) : wy;

                    for (int i = 0; i <= 1; ++i)
                    {
                        const int sx = (i == 0) ? x0 : x1;
                        if (sx < 0 || sx >= static_cast<int>(info.width))
                        {
                            continue;
                        }
                        const double wxf = (i == 0) ? (1.0 - wx) : wx;

                        const size_t srcIdx = (sy * info.width + sx) * info.pixelSizeBytes + c;
                        val += data_.get()[srcIdx] * wyf * wxf;
                    }
                }
                const size_t dstIdx = (y * newWidth + x) * info.pixelSizeBytes + c;
                rotated[dstIdx] = static_cast<unsigned char>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }

    // Resize internal buffer and update info with proper array deleter
    data_.reset(new unsigned char[newSize], std::default_delete<unsigned char[]>());
    size_ = newSize;
    std::memcpy(data_.get(), rotated.data(), newSize);
    info.width = newWidth;
    info.height = newHeight;
    return {-minX, -minY};
}

void Image::rotate90()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    const unsigned long newWidth = info.height;
    const unsigned long newHeight = info.width;
    const size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(newSize);

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long srcIdx = (y * info.width + x) * info.pixelSizeBytes;
            const unsigned long dstIdx = (x * newWidth + (newWidth - 1 - y)) * info.pixelSizeBytes;
            std::memcpy(&rotated[dstIdx], &data_.get()[srcIdx], info.pixelSizeBytes);
        }
    }
    resize(newSize);
    std::memcpy(data_.get(), rotated.data(), newSize);
    info.width = newWidth;
    info.height = newHeight;
}

void Image::rotate180()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    unsigned char* d = data_.get();
    const unsigned long total = info.width * info.height;
    for (unsigned long i = 0; i < total / 2; ++i)
    {
        const unsigned long j = total - 1 - i;
        for (unsigned char c = 0; c < info.pixelSizeBytes; ++c)
        {
            const unsigned char tmp = d[i * info.pixelSizeBytes + c];
            d[i * info.pixelSizeBytes + c] = d[j * info.pixelSizeBytes + c];
            d[j * info.pixelSizeBytes + c] = tmp;
        }
    }
}

void Image::rotate270()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    const unsigned long newWidth = info.height;
    const unsigned long newHeight = info.width;
    const size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(newSize);

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long srcIdx = (y * info.width + x) * info.pixelSizeBytes;
            const unsigned long dstIdx = ((info.width - 1 - x) * newWidth + y) * info.pixelSizeBytes;
            std::memcpy(&rotated[dstIdx], &data_.get()[srcIdx], info.pixelSizeBytes);
        }
    }
    resize(newSize);
    std::memcpy(data_.get(), rotated.data(), newSize);
    info.width = newWidth;
    info.height = newHeight;
}

// Private methods
size_t Image::index(size_t col, size_t row) const noexcept
{
    return (row * info.width + col) * info.pixelSizeBytes;
}

void Image::copyPixelsWithBlending(const Image& src, long srcDestX, long srcDestY, long canvasX, long canvasY,
                                   size_t canvasWidth, size_t canvasHeight)
{
    if ((src.data() == nullptr) || !data_)
    {
        common::logError("copyPixelsWithBlending - Aborting no data");
        return;
    }

    // Calculate bounds in destination coordinate space
    const long srcLeft = srcDestX;
    const long srcTop = srcDestY;
    const long srcRight = srcLeft + static_cast<long>(src.info.width);
    const long srcBottom = srcTop + static_cast<long>(src.info.height);

    const long canvasLeft = canvasX;
    const long canvasTop = canvasY;
    const long canvasRight = canvasLeft + static_cast<long>(canvasWidth);
    const long canvasBottom = canvasTop + static_cast<long>(canvasHeight);

    // Destination image bounds
    const long destLeft = 0;
    const long destTop = 0;
    const long destRight = static_cast<long>(info.width);
    const long destBottom = static_cast<long>(info.height);

    // Calculate clipping region as intersection of all bounds
    const long clipLeft = std::max({srcLeft, canvasLeft, destLeft});
    const long clipTop = std::max({srcTop, canvasTop, destTop});
    const long clipRight = std::min({srcRight, canvasRight, destRight});
    const long clipBottom = std::min({srcBottom, canvasBottom, destBottom});

    // Skip if no intersection
    if (clipLeft >= clipRight || clipTop >= clipBottom)
    {
        return;
    }

    const unsigned char* srcData = src.data();
    unsigned char* dstData = data_.get();
    const unsigned char dstPixelSize = info.pixelSizeBytes;
    const unsigned char srcPixelSize = src.info.pixelSizeBytes;

    // Convert to new format system
    const linuxface::image::PixelFormat srcFormat = Image::pixelSizeToFormat(srcPixelSize);
    const linuxface::image::PixelFormat dstFormat = Image::pixelSizeToFormat(dstPixelSize);

    for (long y = clipTop; y < clipBottom; y++)
    {
        for (long x = clipLeft; x < clipRight; x++)
        {
            // CONSISTENT COORDINATE MAPPING:
            // All coordinates are in destination space, source mapping is relative to srcDestX/srcDestY
            const long srcCol = x - srcDestX; // Position in source image
            const long srcRow = y - srcDestY; // Position in source image
            const long dstCol = x;            // Position in destination image
            const long dstRow = y;            // Position in destination image

            // Bounds check before calculating indices
            if (srcRow < 0 || srcRow >= static_cast<long>(src.info.height) || srcCol < 0
                || srcCol >= static_cast<long>(src.info.width) || dstRow < 0 || dstRow >= static_cast<long>(info.height)
                || dstCol < 0 || dstCol >= static_cast<long>(info.width))
            {
                continue;
            }

            const size_t srcIdx =
                (static_cast<size_t>(srcRow) * src.info.width + static_cast<size_t>(srcCol)) * srcPixelSize;
            const size_t dstIdx =
                (static_cast<size_t>(dstRow) * info.width + static_cast<size_t>(dstCol)) * dstPixelSize;

            // Bounds check for buffer access
            if (srcIdx + srcPixelSize > src.size() || dstIdx + dstPixelSize > size_)
            {
                continue;
            }

            // Handle RGBA blending with alpha transparency
            if (srcFormat == image::PixelFormat::RGBA)
            {
                const unsigned char srcAlpha = srcData[srcIdx + 3];
                if (srcAlpha == 0)
                {
                    continue; // Skip completely transparent pixels
                }
                image::ImageProcessor::processPixel(&srcData[srcIdx], &dstData[dstIdx], srcFormat, dstFormat, srcAlpha != 255, srcAlpha);
            }
            else
            {
                // Use centralized conversion for all other format combinations
                image::ImageProcessor::processPixel(&srcData[srcIdx], &dstData[dstIdx], srcFormat, dstFormat, false, 255);
            }
        }
    }
}

void Image::copyPixelsOptimized(const Image& src, long srcX, long srcY, long dstX, long dstY, size_t copyWidth,
                                size_t copyHeight)
{
    if ((src.data() == nullptr) || !data_)
    {
        return;
    }

    const unsigned char* srcData = src.data();
    unsigned char* dstData = data_.get();
    const unsigned char dstPixelSize = info.pixelSizeBytes;
    const unsigned char srcPixelSize = src.info.pixelSizeBytes;

    // Convert to new format system
    const linuxface::image::PixelFormat srcFormat = Image::pixelSizeToFormat(srcPixelSize);
    const linuxface::image::PixelFormat dstFormat = Image::pixelSizeToFormat(dstPixelSize);

    // Optimize for direct copy cases
    if (srcFormat == dstFormat)
    {
        for (size_t row = 0; row < copyHeight; ++row)
        {
            const size_t srcRowIdx = ((srcY + row) * src.info.width + srcX) * srcPixelSize;
            const size_t dstRowIdx = ((dstY + row) * info.width + dstX) * dstPixelSize;
            std::memcpy(dstData + dstRowIdx, srcData + srcRowIdx, copyWidth * srcPixelSize);
        }
        return;
    }

    // Use row-based conversion for better performance
    for (size_t row = 0; row < copyHeight; ++row)
    {
        const size_t srcRowStart = ((srcY + row) * src.info.width + srcX) * srcPixelSize;
        const size_t dstRowStart = ((dstY + row) * info.width + dstX) * dstPixelSize;

        image::PixelConverter::convertRow(srcData + srcRowStart, dstData + dstRowStart, copyWidth, srcFormat, dstFormat);
    }
}

bool Image::isFullyOpaque() const
{
    if (info.pixelSizeBytes != 4)
    {
        return true; // Only RGBA can be non-opaque
    }
    if (!data_ || size_ == 0)
    {
        return false;
    }
    const unsigned char* d = data_.get();
    for (size_t i = 3; i < size_; i += 4)
    {
        if (d[i] != 255)
        {
            return false;
        }
    }
    return true;
}

// TODO(arroyo): FIXME: This has a high execution cost!
Image& Image::pasteImpl(const Image& other, long otherX, long otherY, bool expandCanvas)
{
    // Validation
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
    {
        common::logError("Image::paste - Invalid base image");
        return *this;
    }

    if ((other.data() == nullptr) || other.size() == 0 || other.info.width == 0 || other.info.height == 0)
    {
        common::logError("Image::paste - Invalid source image to paste");
        return *this;
    }

    // Calculate bounds for both images
    const long baseLeft = static_cast<long>(info.x);
    const long baseTop = static_cast<long>(info.y);
    const long baseRight = baseLeft + static_cast<long>(info.width);
    const long baseBottom = baseTop + static_cast<long>(info.height);

    const long otherLeft = otherX;
    const long otherTop = otherY;
    const long otherRight = otherLeft + static_cast<long>(other.info.width);
    const long otherBottom = otherTop + static_cast<long>(other.info.height);

    // Calculate new dimensions
    const long minX = std::min(baseLeft, otherLeft);
    const long minY = std::min(baseTop, otherTop);
    const long maxX = std::max(baseRight, otherRight);
    const long maxY = std::max(baseBottom, otherBottom);

    const auto newWidth = static_cast<unsigned long>(maxX - minX);
    const auto newHeight = static_cast<unsigned long>(maxY - minY);

    const bool sameCanvasSize =
        newWidth == info.width && newHeight == info.height && minX == baseLeft && minY == baseTop;
    if (!expandCanvas || sameCanvasSize)
    {
        // Fast path: fully in-bounds, same format, fully opaque
        const bool fullyOpaque = (other.info.pixelSizeBytes != 4) || other.isFullyOpaque();
        const bool fullyInBounds =
            otherLeft >= baseLeft && otherTop >= baseTop && otherRight <= baseRight && otherBottom <= baseBottom;
        if (fullyInBounds)
        {
            const long dstX = otherLeft - baseLeft;
            const long dstY = otherTop - baseTop;
            if (fullyOpaque)
            {
                copyPixelsOptimized(other, 0, 0, dstX, dstY, other.info.width, other.info.height);
            }
            else
            {
                // Copy source image positioned at dstX, dstY in destination coordinates
                // Canvas matches the source region exactly
                copyPixelsWithBlending(other, dstX, dstY, dstX, dstY, other.info.width, other.info.height);
            }
            return *this;
        }
        // Fallback to blending (legacy, for partial overlap)
        copyPixelsWithBlending(other, otherLeft, otherTop, 0, 0, info.width, info.height);
        return *this;
    }

    // PERF: Only backup and copy the minimal affected region when expanding canvas.
    // Avoid full buffer memset; only clear new regions if needed.
    // This minimizes memory operations and improves performance for large images.
    Image backup;
    if (!sameCanvasSize)
    {
        // Region-aware backup and copy is now correct and safe (see CRIT-002 resolution)
        backup.copyFrom(*this);
        const size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
        this->resize(newSize);
        info.x = static_cast<unsigned long>(minX);
        info.y = static_cast<unsigned long>(minY);
        info.width = newWidth;
        info.height = newHeight;
        copyPixelsWithBlending(backup, baseLeft, baseTop, minX, minY, baseRight - baseLeft, baseBottom - baseTop);
    }
    // Copy the pasted image (other) to the correct region
    copyPixelsWithBlending(other, otherLeft, otherTop, minX, minY, other.info.width, other.info.height);
    return *this;
}
// TODO(arroyo): SEEMS LIKE BOUNDS ARE WRONG, MOVE CLOSE THE FACE TO THE BOTTOM
// EDGE.
std::unique_ptr<Image> Image::affineWarpBilinear(const double* m, int outWidth, int outHeight, const double* invM,
                                                 ImageFormat targetFormat) const
{
    // Validate input format support (RGB and RGBA)
    if (info.pixelSizeBytes != 3 && info.pixelSizeBytes != 4)
    {
        common::logError("Image::affineWarpBilinear - Only RGB and RGBA images are supported, got %d channels",
                         info.pixelSizeBytes);
        return nullptr;
    }

    // Validate target format support
    if (targetFormat != ImageFormat::RGB && targetFormat != ImageFormat::RGBA)
    {
        common::logError("Image::affineWarpBilinear - Only RGB and RGBA target formats are supported");
        return nullptr;
    }

    // Determine output channels and format
    const int outChannels = (targetFormat == ImageFormat::RGBA) ? 4 : 3;
    const int inChannels = info.pixelSizeBytes;

    // Accept optional inverse matrix for performance
    double localInvM[6];
    const double* useInvM = nullptr;
    if (invM != nullptr)
    {
        useInvM = invM;
    }
    else
    {
        if (!math_utils::invertAffine(m, localInvM))
        {
            common::logError("Image::affineWarpBilinear - Invalid affine matrix provided");
            return nullptr;
        }
        useInvM = localInvM;
    }

    const size_t outSize = outWidth * outHeight * outChannels;
    auto outImg = std::make_unique<Image>(outSize);
    outImg->info = info;
    outImg->info.width = outWidth;
    outImg->info.height = outHeight;
    outImg->info.pixelSizeBytes = outChannels;
    outImg->info.format = targetFormat;

    const int inWidth = info.width;
    const int inHeight = info.height;

    unsigned char* dst = outImg->data();
    const unsigned char* src = data_.get();

    for (int y = 0; y < outHeight; ++y)
    {
        for (int x = 0; x < outWidth; ++x)
        {
            const double srcX = useInvM[0] * x + useInvM[1] * y + useInvM[2];
            const double srcY = useInvM[3] * x + useInvM[4] * y + useInvM[5];
            unsigned char* pdst = dst + (y * outWidth + x) * outChannels;

            const int x0 = static_cast<int>(std::floor(srcX));
            const int y0 = static_cast<int>(std::floor(srcY));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;

            if (x0 >= 0 && x1 < inWidth && y0 >= 0 && y1 < inHeight)
            {
                const double wx = srcX - x0;
                const double wy = srcY - y0;

                // Process RGB channels (always present)
                for (int c = 0; c < 3; ++c)
                {
                    const double v = (1 - wx) * (1 - wy) * src[(y0 * inWidth + x0) * inChannels + c]
                                     + wx * (1 - wy) * src[(y0 * inWidth + x1) * inChannels + c]
                                     + (1 - wx) * wy * src[(y1 * inWidth + x0) * inChannels + c]
                                     + wx * wy * src[(y1 * inWidth + x1) * inChannels + c];

                    pdst[c] = static_cast<unsigned char>(std::round(std::clamp(v, 0.0, 255.0)));
                }

                // Handle alpha channel based on input/output format combinations
                if (outChannels == 4) // RGBA output
                {
                    if (inChannels == 4) // RGBA input -> RGBA output
                    {
                        // Interpolate alpha channel
                        const double alpha = (1 - wx) * (1 - wy) * src[(y0 * inWidth + x0) * inChannels + 3]
                                             + wx * (1 - wy) * src[(y0 * inWidth + x1) * inChannels + 3]
                                             + (1 - wx) * wy * src[(y1 * inWidth + x0) * inChannels + 3]
                                             + wx * wy * src[(y1 * inWidth + x1) * inChannels + 3];
                        pdst[3] = static_cast<unsigned char>(std::round(std::clamp(alpha, 0.0, 255.0)));
                    }
                    else // RGB input -> RGBA output
                    {
                        // Set alpha to full opacity for RGB to RGBA conversion
                        pdst[3] = 255;
                    }
                }
            }
            else
            {
                // If out of bounds, set to black
                pdst[0] = pdst[1] = pdst[2] = 0; // RGB = black
                // If RGBA, set invisible
                if (outChannels == 4)
                {
                    pdst[3] = 0; // Alpha = transparent
                }
            }
        }
    }

    return outImg;
}

std::unique_ptr<Image>
Image::affineWarpNearestNeighbour(const double* m, int outWidth, int outHeight, const double* invM) const
{
    if (info.pixelSizeBytes != 1)
    {
        common::logError("Image::affineWarpNearestNeighbour - Only single-channel images are supported");
        return nullptr;
    }

    // Matrix inversion optimization (MED-003): use optional invM parameter
    double localInvM[6];
    const double* useInvM = nullptr;
    if (invM != nullptr)
    {
        useInvM = invM;
    }
    else
    {
        if (!math_utils::invertAffine(m, localInvM))
        {
            common::logError("Image::affineWarpNearestNeighbour - Invalid affine matrix provided");
            return nullptr;
        }
        useInvM = localInvM;
    }

    const size_t outSize = outWidth * outHeight;
    auto outImg = std::make_unique<Image>(outSize);
    outImg->info = info;
    outImg->info.width = outWidth;
    outImg->info.height = outHeight;
    outImg->info.pixelSizeBytes = 1;
    outImg->info.format = ImageFormat::GRAYSCALE;
    const int inWidth = info.width;
    const int inHeight = info.height;

    unsigned char* dst = outImg->data();
    const unsigned char* src = data_.get();

    for (int y = 0; y < outHeight; ++y)
    {
        for (int x = 0; x < outWidth; ++x)
        {
            const double srcX = useInvM[0] * x + useInvM[1] * y + useInvM[2];
            const double srcY = useInvM[3] * x + useInvM[4] * y + useInvM[5];
            unsigned char* pdst = dst + (y * outWidth + x);

            // Nearest neighbor
            const int ix = static_cast<int>(std::round(srcX));
            const int iy = static_cast<int>(std::round(srcY));

            if (ix >= 0 && ix < inWidth && iy >= 0 && iy < inHeight)
            {
                const unsigned char* psrc = src + (iy * inWidth + ix);
                pdst[0] = *psrc; // Copy the pixel value
            }
            else
            {
                pdst[0] = 0; // Out of bounds, set to black
            }
        }
    }

    return outImg;
}

void Image::alphaBlend(const Image& src, const Image& mask)
{
    // Validate image dimensions
    if (info.width != src.info.width || info.height != src.info.height || info.width != mask.info.width
        || info.height != mask.info.height)
    {
        common::logError("Image::alphaBlend - Image dimensions do not match: dst(%lux%lu), src(%lux%lu), mask(%lux%lu)",
                         info.width, info.height, src.info.width, src.info.height, mask.info.width, mask.info.height);
        return;
    }

    // Validate pixel formats - support RGB and RGBA for dst/src, single-channel for mask
    if ((info.pixelSizeBytes != 3 && info.pixelSizeBytes != 4)
        || (src.info.pixelSizeBytes != 3 && src.info.pixelSizeBytes != 4) || mask.info.pixelSizeBytes != 1)
    {
        common::logError("Image::alphaBlend - Invalid pixel formats: dst(%d), src(%d), mask(%d). "
                         "Expected RGB(3) or RGBA(4) for dst/src, grayscale(1) for mask",
                         info.pixelSizeBytes, src.info.pixelSizeBytes, mask.info.pixelSizeBytes);
        return;
    }

    unsigned char* dstData = this->data();
    const unsigned char* srcData = src.data();
    const unsigned char* maskData = mask.data();
    const int npixels = info.width * info.height;

    const int dstChannels = info.pixelSizeBytes;
    const int srcChannels = src.info.pixelSizeBytes;

    for (int i = 0; i < npixels; ++i)
    {
        const unsigned char* srcPixel = srcData + i * srcChannels;
        unsigned char* dstPixel = dstData + i * dstChannels;
        const unsigned char maskValue = maskData[i];

        // Skip blending if mask is fully transparent
        if (maskValue == 0)
        {
            continue; // Keep original destination pixel unchanged
        }

        // Skip blending if source pixel is black (likely from out-of-bounds transformation)
        // This prevents black edges from appearing when faces are rotated
        if (srcPixel[0] == 0 && srcPixel[1] == 0 && srcPixel[2] == 0)
        {
            continue; // Keep original destination pixel unchanged
        }

        // Combine mask value with source alpha for RGBA sources
        unsigned char effectiveSrcAlpha = maskValue;
        if (srcChannels == 4)
        {
            // Combine mask alpha with source alpha: result = (mask * src_alpha) / 255
            // Use floating-point arithmetic to avoid rounding errors
            const float combinedAlpha = static_cast<float>(maskValue) * static_cast<float>(srcPixel[3]) / 255.0f;
            effectiveSrcAlpha = static_cast<unsigned char>(std::round(std::clamp(combinedAlpha, 0.0f, 255.0f)));
        }

        // Use our new architecture to blend pixels
        // Convert to our new pixel format system
        linuxface::image::PixelFormat srcFormat =
            (srcChannels == 3) ? linuxface::image::PixelFormat::RGB : linuxface::image::PixelFormat::RGBA;
        linuxface::image::PixelFormat dstFormat =
            (dstChannels == 3) ? linuxface::image::PixelFormat::RGB : linuxface::image::PixelFormat::RGBA;

        // Create temporary source pixel with effective alpha
        unsigned char tempSrcPixel[4];
        tempSrcPixel[0] = srcPixel[0];
        tempSrcPixel[1] = srcPixel[1];
        tempSrcPixel[2] = srcPixel[2];

        if (srcChannels == 4)
        {
            tempSrcPixel[3] = effectiveSrcAlpha;
            // Keep srcFormat as RGBA
        }
        else
        {
            // For RGB sources, use blendPixel with RGB format and alpha parameter
            // Don't set tempSrcPixel[3] as it won't be used for RGB format
        }

        // Use AlphaBlender to blend the pixel
        linuxface::image::AlphaBlender blender;
        if (srcChannels == 4)
        {
            blender.blendPixel(tempSrcPixel, dstPixel, srcFormat, dstFormat);
        }
        else
        {
            blender.blendPixel(tempSrcPixel, dstPixel, srcFormat, dstFormat, effectiveSrcAlpha);
        }
    }
}
