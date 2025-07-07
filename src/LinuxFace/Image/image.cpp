#include "LinuxFace/Image/image.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "LinuxFace/Image/image_utils.h"

using namespace linuxface;

// Optimized pixel operations
void PixelOperations::blendPixels(unsigned char* dst, const unsigned char* src, unsigned char pixelSize,
                                  unsigned char alpha) noexcept
{
    if (src[0] == 0 && src[1] == 0 && src[2] == 0)
    {
        // If the source pixel is black, do not blend
        return;
    }

    if (alpha == 255)
    {
        // Fast path for fully opaque pixels
        switch (pixelSize)
        {
            case 3:
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                break;
            case 4:
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
                break;
            default:
                std::memcpy(dst, src, pixelSize);
        }
    }
    else if (alpha > 0)
    {
        // Optimized alpha blending using integer arithmetic
        const int srcAlpha = alpha;
        const int invAlpha = 255 - alpha;
        const int c = std::min(pixelSize, static_cast<unsigned char>(3));
        for (unsigned char i = 0; i < c; ++i)
        {
            dst[i] = (src[i] * srcAlpha + dst[i] * invAlpha) / 255;
        }
        if (pixelSize == 4)
        {
            dst[3] = std::max(dst[3], alpha);
        }
    }
}

// Constructors with improved memory management
Image::Image(size_t size) : size_(size)
{
    if (size > 0)
    {
        data_ = std::shared_ptr<unsigned char>(new unsigned char[size], std::default_delete<unsigned char[]>());
        // Set default format to RGB
        info.format = ImageFormat::RGB;
        info.pixelSizeBytes = 3;
    }
}

Image::Image(unsigned char* buffer, size_t size) : size_(size)
{
    if (buffer && size > 0)
    {
        data_ = std::shared_ptr<unsigned char>(buffer, std::default_delete<unsigned char[]>());
    }
}

Image::Image(unsigned char* buffer, size_t size, bool takeOwnership) : size_(size)
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

// Add missing move constructor and assignment operator
Image::Image(Image&& other) noexcept : info(other.info), data_(std::move(other.data_)), size_(other.size_)
{
    other.size_ = 0;
    other.info = {};
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other)
    {
        data_ = std::move(other.data_);
        size_ = other.size_;
        info = other.info;

        other.size_ = 0;
        other.info = {};
    }
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
        // Initialize new data to zero
        memset(newData.get(), 0, newSize);
        // TODO: FIXME: Can we remove this memcpy? do we need old data in any situation?
        if (preserveData && data_ && newData)
        {
            // Copy existing data if present
            memcpy(newData.get(), data_.get(), std::min(size_, newSize));
        }

        data_ = std::move(newData);
        size_ = newSize;
    }
}

Pixel Image::operator()(size_t col, size_t row) const
{
    size_t idx = index(col, row);
    if (idx >= size_ || !data_)
    {
        common::log_error("Image::operator(): index out of bounds [col,row] %zu, %zu Index: %zu", col, row, idx);
        return Pixel(0, 0, 0, DEFAULT_ALPHA);
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

void Image::ppx(size_t col, size_t row, const Pixel& c)
{
    this->pxy(col, row, c.r, c.g, c.b, c.a);
}

void Image::pxy(size_t col, size_t row, const unsigned char r, const unsigned char g, const unsigned char b,
                const unsigned char a)
{
    size_t idx = index(col, row);
    if (idx >= size_ || !data_)
    {
        common::log_error("Image::pxy: index out of bounds [col,row] %zu, %zu Index: %zu", col, row, idx);
        return;
    }
    this->pidx(idx, r, g, b, a);
}

void Image::pidx(size_t idx, const unsigned char r, const unsigned char g, const unsigned char b, const unsigned char a)
{
    // Use PixelOperations for consistency
    if (info.pixelSizeBytes == 4)
    {
        PixelOperations::setPixelRGBA(data_.get(), idx, r, g, b, a);
    }
    else
    {
        PixelOperations::setPixelRGB(data_.get(), idx, r, g, b);
    }
}

bool Image::isColorImage() const noexcept
{
    return info.format == ImageFormat::RGB || info.format == ImageFormat::RGBA || info.format == ImageFormat::JPEG;
}

unsigned char Image::getExpectedPixelSize() const noexcept
{
    // TODO: Refactor this
    switch (info.format)
    {
        case ImageFormat::RGB:
            return 3;
        case ImageFormat::RGBA:
            return 4;
        case ImageFormat::GRAYSCALE:
            return 1;
        case ImageFormat::DEPTH_FLOAT:
            return sizeof(float);
        case ImageFormat::DEPTH_Z16:
            return 2;
        default:
            return info.pixelSizeBytes;
    }
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
        if (data_ && other.data() && size_ > 0)
        {
            std::memcpy(data_.get(), other.data(), size_);
        }
        info = other.info;
    }
}

std::unique_ptr<Image> Image::deepCopy() const
{
    auto copy = std::make_unique<Image>(size_);
    if (data_ && copy->data() && size_ > 0)
    {
        std::memcpy(copy->data(), data_.get(), size_);
    }
    copy->info = this->info;
    return copy;
}

void Image::scaleImageBuffer(const unsigned char* srcData, unsigned long srcWidth, unsigned long srcHeight,
                      unsigned char pixelSize, unsigned char* dstData, unsigned long dstWidth, unsigned long dstHeight,
                      ScalingAlgorithm algorithm) const
{
    // Use non-const T for both src and dst to match template requirements
    image_utils::ImageView<unsigned char> srcView{const_cast<unsigned char*>(srcData), srcWidth, srcHeight, pixelSize};
    image_utils::ImageView<unsigned char> dstView{dstData, dstWidth, dstHeight, pixelSize};
    switch (algorithm)
    {
        case ScalingAlgorithm::LANCZOS:
            image_utils::lanczosScaling<unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        case ScalingAlgorithm::BILINEAR:
            image_utils::bilinearScaling<unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        case ScalingAlgorithm::AREA_AVERAGING:
            image_utils::areaAveragingScaling<unsigned char, NormalizationType::NONE>(srcView, dstView);
            break;
        case ScalingAlgorithm::FAST_BOX:
            image_utils::fastBoxScaling<unsigned char>(srcView, dstView);
            break;
        case ScalingAlgorithm::BICUBIC:
        default:
            common::log_error("scaleImageBuffer - Unsupported scaling algorithm: %d", static_cast<int>(algorithm));
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
    size_t newWidth = static_cast<size_t>(info.width * factor + 0.5);
    size_t newHeight = static_cast<size_t>(info.height * factor + 0.5);
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

    size_t newWidth = static_cast<size_t>(info.width * factor + 0.5);
    size_t newHeight = static_cast<size_t>(info.height * factor + 0.5);
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
        return deepCopy();
    }

    auto result = std::make_unique<Image>(newWidth * newHeight * info.pixelSizeBytes);
    scaleImageBuffer(data_.get(), info.width, info.height, info.pixelSizeBytes,
                    result->data(), newWidth, newHeight, algorithm);
    result->info = info;
    result->info.width = newWidth;
    result->info.height = newHeight;
    return result;
}

void Image::move(size_t new_x, size_t new_y)
{
    info.x = new_x;
    info.y = new_y;
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

void Image::toTensor(float* outputData, TensorPadding& padding, int new_width, int new_height,
                     NormalizationType normType) const
{
    if (!isColorImage() || info.pixelSizeBytes != 3)
    {
        common::log_error("Image::toTensor - Expected RGB format, got format: %s with pixel size: %d",
                          fromImageFormatToString(info.format).c_str(), info.pixelSizeBytes);
        return;
    }

    if (!outputData || !data_ || size_ == 0)
    {
        common::log_error("Image::toTensor - Invalid input data");
        return;
    }

    const int origW = info.width;
    const int origH = info.height;

    // Step 1: Compute scale ratio (preserve aspect ratio)
    float r = std::min(static_cast<float>(new_width) / origW, static_cast<float>(new_height) / origH);

    const int resizedW = static_cast<int>(origW * r);
    const int resizedH = static_cast<int>(origH * r);

    const int offsetX = (new_width - resizedW) / 2;
    const int offsetY = (new_height - resizedH) / 2;

    const int paddedSize = new_width * new_height;

    // Store transform metadata in the padding object
    padding.tensor_width = new_width;
    padding.tensor_height = new_height;
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
            float* r_channel = outputData;
            float* g_channel = outputData + paddedSize;
            float* b_channel = outputData + 2 * paddedSize;

            for (int i = 0; i < paddedSize; i++)
            {
                r_channel[i] = padding.rgb_values[0];
                g_channel[i] = padding.rgb_values[1];
                b_channel[i] = padding.rgb_values[2];
            }
        }
        break;
        default:
            std::memset(outputData, 0, paddedSize * 3 * sizeof(float));
            break;
    }

    const unsigned char* srcData = data_.get();

    // TODO: Test resize with bilinear interpolation or even bicubic interpolation.
    // Resize original image using nearest neighbor
    // For each pixel in resized image
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

            // Bounds check for source data
            if (srcIdx + 2 >= static_cast<int>(size_))
            {
                common::log_error("Image::toTensor - Source index out of bounds: %d >= %zu", srcIdx + 2, size_);
                continue;
            }

            // Location in padded (output) image
            int dstH = h + offsetY;
            int dstW = w + offsetX;
            int dstIdx = dstH * new_width + dstW;

            // Bounds check for destination
            if (dstIdx >= paddedSize)
            {
                common::log_error("Image::toTensor - Destination index out of bounds: %d >= %d", dstIdx, paddedSize);
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

void Image::fromTensor(const float* tensorData, std::vector<int64_t> tensorShape, int tensor_width, int tensor_height,
                       const TensorPadding& padding, NormalizationType normType)
{
    if (!tensorData || !data_ || size_ == 0)
    {
        common::log_error("Image::fromTensor - Invalid input data");
        return;
    }

    // Use stored transform metadata if available, otherwise calculate it
    int offsetX, offsetY, resizedW, resizedH;
    float r;

    // common::log_info("Image::fromTensor - Using tensor dimensions: %dx%d", tensor_width, tensor_height);
    // common::log_info("Tensor padding = %d width, %d height", padding.tensor_width, padding.tensor_height);

    if (padding.has_transform && padding.tensor_width == tensor_width && padding.tensor_height == tensor_height)
    {
        // Use stored transform metadata from padding
        offsetX = padding.offset_x;
        offsetY = padding.offset_y;
        resizedW = padding.resized_width;
        resizedH = padding.resized_height;
        r = padding.scale_ratio;

        // common::log_info(
        //     "Image::fromTensor - Using stored transform metadata: offset(%d,%d), resized(%dx%d), scale=%.3f",
        //     offsetX, offsetY, resizedW, resizedH, r);
    }
    else
    {
        // Fallback: calculate transform parameters
        const int origW = info.width;
        const int origH = info.height;

        r = std::min(static_cast<float>(tensor_width) / origW, static_cast<float>(tensor_height) / origH);
        resizedW = static_cast<int>(origW * r);
        resizedH = static_cast<int>(origH * r);
        offsetX = (tensor_width - resizedW) / 2;
        offsetY = (tensor_height - resizedH) / 2;

        // common::log_warn("Image::fromTensor - No stored transform metadata, calculating: offset(%d,%d), "
        //                  "resized(%dx%d), scale=%.3f",
        //                  offsetX, offsetY, resizedW, resizedH, r);
    }

    unsigned char* dstData = data_.get();
    int tensor_size = tensor_width * tensor_height;
    // Extract and resize depth data back to original dimensions
    for (int y = 0; y < static_cast<int>(info.height); ++y)
    {
        for (int x = 0; x < static_cast<int>(info.width); ++x)
        {
            // Map to tensor coordinates
            int tensorH = static_cast<int>((static_cast<float>(y) / info.height) * resizedH) + offsetY;
            int tensorW = static_cast<int>((static_cast<float>(x) / info.width) * resizedW) + offsetX;

            // Clamp to tensor bounds
            tensorH = std::max(0, std::min(tensorH, tensor_height - 1));
            tensorW = std::max(0, std::min(tensorW, tensor_width - 1));

            int channels = 1;
            // Check if the tensor has a single channel
            if (tensorShape[1] == 3)
            {
                channels = 3;
            }

            int tensorIdx = (tensorH * tensor_width + tensorW);

            // Convert tensor value to unsigned char
            unsigned char pixelValues[3];
            if (normType == NormalizationType::NONE)
            {
                pixelValues[0] = static_cast<unsigned char>(tensorData[tensorIdx]);
                if (channels == 3)
                {
                    // Remember that thi is in CHW not HWC
                    pixelValues[1] = static_cast<unsigned char>(tensorData[1 * tensor_size + tensorIdx]);
                    pixelValues[2] = static_cast<unsigned char>(tensorData[2 * tensor_size + tensorIdx]);
                }
            }
            else if (normType == NormalizationType::MINMAX)
            {
                pixelValues[0] = static_cast<unsigned char>(tensorData[tensorIdx] * 255.0f);
                if (channels == 3)
                {
                    pixelValues[1] = static_cast<unsigned char>(tensorData[1 * tensor_size + tensorIdx] * 255.0f);
                    pixelValues[2] = static_cast<unsigned char>(tensorData[2 * tensor_size + tensorIdx] * 255.0f);
                }
            }
            else if (normType == NormalizationType::ZERO_CENTER)
            {
                pixelValues[0] = static_cast<unsigned char>((tensorData[tensorIdx] + 1.0) / 2.0f * 255.0f);
                if (channels == 3)
                {
                    pixelValues[1] =
                        static_cast<unsigned char>((tensorData[1 * tensor_size + tensorIdx] + 1.0) / 2.0f * 255.0f);
                    pixelValues[2] =
                        static_cast<unsigned char>((tensorData[2 * tensor_size + tensorIdx] + 1.0) / 2.0f * 255.0f);
                }
            }
            else
            {
                common::log_error("Unknown normalization type");
                return;
            }

            int dstIdx = (y * info.width + x) * 3;
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

void Image::paintPoints(const std::vector<math_utils::Point>& points, const Pixel& color)
{
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

    const int crop_width = x1 - x0;
    const int crop_height = y1 - y0;

    if (crop_width <= 0 || crop_height <= 0)
    {
        return nullptr;
    }

    size_t result_size = crop_width * crop_height * info.pixelSizeBytes;
    std::unique_ptr<Image> result = std::make_unique<Image>(result_size);

    const unsigned char* srcImg = data_.get();
    unsigned char* dstImg = result->data();

    for (int y = 0; y < crop_height; ++y)
    {
        const unsigned char* src_row = srcImg + ((y0 + y) * info.width + x0) * info.pixelSizeBytes;
        unsigned char* dst_row = dstImg + (y * crop_width) * info.pixelSizeBytes;
        std::memcpy(dst_row, src_row, crop_width * info.pixelSizeBytes);
    }
    result->info = info;
    result->info.x = rect.x();
    result->info.y = rect.y();
    result->info.width = crop_width;
    result->info.height = crop_height;
    return result;
}

bool Image::saveToDisk(const std::string& dest_path) const
{
    // Only support saving as JPEG or PPM (RGB/Grayscale/RGBA)
    if (info.format != ImageFormat::JPEG && info.format != ImageFormat::PPM && info.format != ImageFormat::GRAYSCALE
        && info.format != ImageFormat::RGB && info.format != ImageFormat::RGBA)
    {
        common::log_error(
            "Image::saveToDisk - Only JPEG or PPM formats are supported for saving to disk. Image is %s. Path was %s",
            fromImageFormatToString(info.format).c_str(), dest_path.c_str());
        return false;
    }

    if (!data_ || size_ == 0u)
    {
        common::log_error("Image::saveToDisk - No data to save");
        return false;
    }

    int file = open(dest_path.c_str(), O_WRONLY | O_CREAT, 0660);
    if (file < 0)
    {
        common::log_error("Image::saveToDisk - Error opening file for writing: %s", dest_path.c_str());
        common::errno_log("Image::saveToDisk - Error opening file for writing");
        return false;
    }
    common::log_info("Image::saveToDisk - Saving image to %s", dest_path.c_str());
    // Handle PPM (RGB, RGBA, Grayscale)
    if (info.format == ImageFormat::PPM || info.format == ImageFormat::RGB || info.format == ImageFormat::RGBA
        || info.format == ImageFormat::GRAYSCALE)
    {
        char header[64];
        // Write the P6 header
        int header_len = std::snprintf(header, sizeof(header), "P6\n%lu %lu\n255\n", info.width, info.height);
        if (write(file, header, header_len) != header_len)
        {
            common::errno_log("Image::saveToDisk - write header failed");
            close(file);
            return false;
        }
        size_t pixel_count = info.width * info.height;
        // Grayscale: convert to RGB
        if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
        {
            std::vector<unsigned char> rgb_data = convertToRGB();
            if (!common::long_write(file, rgb_data.data(), rgb_data.size()))
            {
                common::log_error("Image::saveToDisk - Error saving grayscale as RGB PPM. Not all bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
        // RGBA: convert to RGB
        if (info.format == ImageFormat::RGBA || info.pixelSizeBytes == 4)
        {
            std::vector<unsigned char> rgb_data(pixel_count * 3);
            const unsigned char* rgba = data_.get();
            for (size_t i = 0; i < pixel_count; ++i)
            {
                rgb_data[i * 3 + 0] = rgba[i * 4 + 0];
                rgb_data[i * 3 + 1] = rgba[i * 4 + 1];
                rgb_data[i * 3 + 2] = rgba[i * 4 + 2];
            }
            if (!common::long_write(file, rgb_data.data(), rgb_data.size()))
            {
                common::log_error("Image::saveToDisk - Error saving RGBA as RGB PPM. Not all bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
        // RGB: direct write
        if (info.format == ImageFormat::RGB || info.pixelSizeBytes == 3)
        {
            if (!common::long_write(file, data_.get(), pixel_count * 3))
            {
                common::log_error("Image::saveToDisk - Error saving RGB PPM. Not all bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
    }

    // JPEG: direct write (assume data_ is already JPEG encoded)
    if (info.format == ImageFormat::JPEG)
    {
        if (!common::long_write(file, data_.get(), size_))
        {
            common::log_error("Image::saveToDisk - Error saving JPEG. Not all bytes were stored.");
            close(file);
            return false;
        }
        close(file);
        return true;
    }

    // If we reach here, format is not supported for saving
    common::log_error("Image::saveToDisk - Unsupported format for saving: %s",
                      fromImageFormatToString(info.format).c_str());
    close(file);
    return false;
}

std::vector<unsigned char> Image::convertToRGB() const
{
    if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
    {
        size_t pixel_count = info.width * info.height;
        std::vector<unsigned char> rgb_data(pixel_count * 3);
        const unsigned char* gray = data_.get();
        for (size_t i = 0; i < pixel_count; ++i)
        {
            rgb_data[i * 3 + 0] = gray[i];
            rgb_data[i * 3 + 1] = gray[i];
            rgb_data[i * 3 + 2] = gray[i];
        }
        return rgb_data;
    }
    return {};
}

bool Image::convertToRGBInplace()
{
    if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
    {
        std::vector<unsigned char> rgb_data = convertToRGB();
        size_t i = 0;
        size_t pixel_count = info.width * info.height * 3;
        resize(pixel_count, true);
        for (unsigned char pixel : rgb_data)
        {
            data_.get()[i] = pixel;
            i += 1;
        }
        if (i != pixel_count)
        {
            common::log_error("Image::convertToRGB - Error converting grayscale to RGB. Size mismatch.");
            return false;
        }
        info.format = ImageFormat::RGB;
        info.pixelSizeBytes = 3;

        return true;
    }
    return false;
}

void Image::changeBackgroundImage(const Image& matting, const Image& background)
{
    if (matting.info.width != background.info.width || matting.info.height != background.info.height)
    {
        common::log_error("Background image and matting image have different dimensions");
        common::log_info("Dimensions are matting %d x %d", matting.info.width, matting.info.height);
        common::log_info("Dimensions are background %d x %d", background.info.width, background.info.height);
        return;
    }

    if (matting.info.pixelSizeBytes != background.info.pixelSizeBytes)
    {
        common::log_error("Background image and matting image have different pixel sizes");
        return;
    }

    if (matting.data() == nullptr || background.data() == nullptr)
    {
        common::log_error("Background image or matting image have no data");
        return;
    }

    unsigned char* phaData = matting.data();
    unsigned char* frgData = data();
    unsigned char* backgroundData = background.data();
    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            unsigned long idx = (y * info.width + x) * 3;
            unsigned char alpha = phaData[idx];
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
            unsigned long idx = (y * info.width + x) * info.pixelSizeBytes;
            unsigned char r = data_.get()[idx];
            unsigned char g = data_.get()[idx + 1];
            unsigned char b = data_.get()[idx + 2];
            unsigned char gray = static_cast<unsigned char>(0.299f * r + 0.587f * g + 0.114f * b);
            data_.get()[idx] = gray;
            data_.get()[idx + 1] = gray;
            data_.get()[idx + 2] = gray;
            if (info.pixelSizeBytes == 4)
            {
                data_.get()[idx + 3] = DEFAULT_ALPHA;
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
            unsigned long idx1 = (y * info.width + x) * info.pixelSizeBytes;
            unsigned long idx2 = (y * info.width + (info.width - 1 - x)) * info.pixelSizeBytes;
            for (unsigned char c = 0; c < info.pixelSizeBytes; ++c)
            {
                unsigned char tmp = d[idx1 + c];
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
    unsigned long rowBytes = info.width * info.pixelSizeBytes;
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

void Image::rotate(float angleDegrees)
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }

    float angleRad = angleDegrees * static_cast<float>(M_PI) / 180.0f;
    float cosA = std::cos(angleRad);
    float sinA = std::sin(angleRad);

    // Compute center of the image
    float cx = (info.width - 1) / 2.0f;
    float cy = (info.height - 1) / 2.0f;

    // Compute bounding box for rotated image
    float corners[4][2] = {
        {0 - cx,                                  0 - cy                                  },
        {static_cast<float>(info.width - 1) - cx, 0 - cy                                  },
        {0 - cx,                                  static_cast<float>(info.height - 1) - cy},
        {static_cast<float>(info.width - 1) - cx, static_cast<float>(info.height - 1) - cy}
    };
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < 4; ++i)
    {
        float x = corners[i][0] * cosA - corners[i][1] * sinA;
        float y = corners[i][0] * sinA + corners[i][1] * cosA;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    unsigned long newWidth = static_cast<unsigned long>(std::ceil(maxX - minX + 1));
    unsigned long newHeight = static_cast<unsigned long>(std::ceil(maxY - minY + 1));
    size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(newSize, 0);

    // For each pixel in the destination image, map back to the source
    for (unsigned long y = 0; y < newHeight; ++y)
    {
        for (unsigned long x = 0; x < newWidth; ++x)
        {
            // Map (x, y) in dest to (srcX, srcY) in source
            float dx = x + minX;
            float dy = y + minY;
            float srcX = cosA * dx + sinA * dy + cx;
            float srcY = -sinA * dx + cosA * dy + cy;

            // Bilinear interpolation
            int x0 = static_cast<int>(std::floor(srcX));
            int y0 = static_cast<int>(std::floor(srcY));
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            float wx = srcX - x0;
            float wy = srcY - y0;

            for (unsigned char c = 0; c < info.pixelSizeBytes; ++c)
            {
                float v = 0.0f;
                for (int dyIdx = 0; dyIdx <= 1; ++dyIdx)
                {
                    int sy = (dyIdx == 0) ? y0 : y1;
                    if (sy < 0 || sy >= static_cast<int>(info.height))
                    {
                        continue;
                    }
                    float wyf = (dyIdx == 0) ? (1.0f - wy) : wy;
                    for (int dxIdx = 0; dxIdx <= 1; ++dxIdx)
                    {
                        int sx = (dxIdx == 0) ? x0 : x1;
                        if (sx < 0 || sx >= static_cast<int>(info.width))
                        {
                            continue;
                        }
                        float wxf = (dxIdx == 0) ? (1.0f - wx) : wx;
                        unsigned long srcIdx = (sy * info.width + sx) * info.pixelSizeBytes + c;
                        v += data_.get()[srcIdx] * wyf * wxf;
                    }
                }
                unsigned long dstIdx = (y * newWidth + x) * info.pixelSizeBytes + c;
                rotated[dstIdx] = static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f));
            }
        }
    }
    resize(newSize);
    std::memcpy(data_.get(), rotated.data(), newSize);
    info.width = newWidth;
    info.height = newHeight;
}

void Image::rotate90()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    unsigned long newWidth = info.height;
    unsigned long newHeight = info.width;
    size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(newSize);

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            unsigned long srcIdx = (y * info.width + x) * info.pixelSizeBytes;
            unsigned long dstIdx = (x * newWidth + (newWidth - 1 - y)) * info.pixelSizeBytes;
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
    unsigned long total = info.width * info.height;
    for (unsigned long i = 0; i < total / 2; ++i)
    {
        unsigned long j = total - 1 - i;
        for (unsigned char c = 0; c < info.pixelSizeBytes; ++c)
        {
            unsigned char tmp = d[i * info.pixelSizeBytes + c];
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
    unsigned long newWidth = info.height;
    unsigned long newHeight = info.width;
    size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(newSize);

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            unsigned long srcIdx = (y * info.width + x) * info.pixelSizeBytes;
            unsigned long dstIdx = ((info.width - 1 - x) * newWidth + y) * info.pixelSizeBytes;
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

void Image::copyPixelsWithBlending(const Image& src, long srcGlobalX, long srcGlobalY, long canvasX, long canvasY,
                                   size_t canvasWidth, size_t canvasHeight)
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
                PixelOperations::blendPixels(dstData + dstIdx, srcData + srcIdx, pixelSize, srcAlpha);
            }
            else if (pixelSize == 3) // RGB
            {
                PixelOperations::setPixelRGB(dstData, dstIdx, srcData[srcIdx], srcData[srcIdx + 1],
                                             srcData[srcIdx + 2]);
            }
            else if (pixelSize == 1) // Grayscale
            {
                dstData[dstIdx] = srcData[srcIdx];
            }
            else
            {
                common::log_error("Image::paste - Unsupported pixel format");
                // Fallback to memcpy for other formats
                std::memcpy(dstData + dstIdx, srcData + srcIdx, pixelSize);
            }
        }
    }
}

void Image::copyPixelsOptimized(const Image& src, long srcX, long srcY, long dstX, long dstY, size_t copyWidth,
                                size_t copyHeight)
{
    if (!src.data() || !data_)
    {
        return;
    }

    const unsigned char* srcData = src.data();
    unsigned char* dstData = data_.get();
    const unsigned char dstPixelSize = info.pixelSizeBytes;
    const unsigned char srcPixelSize = src.info.pixelSizeBytes;

    for (size_t row = 0; row < copyHeight; ++row)
    {
        size_t srcRowIdx = ((srcY + row) * src.info.width + srcX) * dstPixelSize;
        size_t dstRowIdx = ((dstY + row) * info.width + dstX) * dstPixelSize;
        if (info.pixelSizeBytes == src.info.pixelSizeBytes)
        {
            // If pixel sizes match, we can copy directly
            std::memcpy(dstData + dstRowIdx, srcData + srcRowIdx, copyWidth * dstPixelSize);
        }
        else if (dstPixelSize == 3 && srcPixelSize == 1)
        {
            // Convert Grayscale to RGB
            for (size_t row = 0; row < copyHeight; ++row)
            {
                size_t srcRowStart = (srcY + row) * src.info.width + srcX;
                size_t dstRowStart = ((dstY + row) * info.width + dstX) * dstPixelSize;

                for (size_t col = 0; col < copyWidth; ++col)
                {
                    unsigned char gray = srcData[srcRowStart + col];
                    dstData[dstRowStart + col * 3 + 0] = gray; // R
                    dstData[dstRowStart + col * 3 + 1] = gray; // G
                    dstData[dstRowStart + col * 3 + 2] = gray; // B
                }
            }
        }
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

// TODO: FIXME: This has a high execution cost!
Image& Image::pasteImpl(const Image& other, long otherX, long otherY, bool expandCanvas)
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
        // Fast path: fully in-bounds, same format, fully opaque
        bool fullyOpaque = (info.pixelSizeBytes != 4) || other.isFullyOpaque();
        bool fullyInBounds =
            otherLeft >= baseLeft && otherTop >= baseTop && otherRight <= baseRight && otherBottom <= baseBottom;
        if (fullyOpaque && fullyInBounds)
        {
            // Compute offsets
            long dstX = otherLeft - baseLeft;
            long dstY = otherTop - baseTop;
            copyPixelsOptimized(other, 0, 0, dstX, dstY, other.info.width, other.info.height);
            return *this;
        }
        // Fallback to blending
        copyPixelsWithBlending(other, otherLeft, otherTop, 0, 0, info.width, info.height);
        return *this;
    }

    // Create backup of current image before resizing
    Image backup; // TODO: FIXME: THIS IS WRONG: MAYBE .
    backup.copyFrom(*this);

    // Resize this image using existing method
    size_t newSize = newWidth * newHeight * info.pixelSizeBytes;
    this->resize(newSize);

    // Update dimensions and position
    info.x = static_cast<unsigned long>(minX);
    info.y = static_cast<unsigned long>(minY);
    info.width = newWidth;
    info.height = newHeight;

    // // Clear the new buffer
    if (data_)
    {
        std::memset(data_.get(), 0, size_);
    }

    // Copy original image to new position
    copyPixelsWithBlending(backup, baseLeft, baseTop, minX, minY, newWidth, newHeight);

    // Copy other image
    copyPixelsWithBlending(other, otherLeft, otherTop, minX, minY, newWidth, newHeight);

    return *this;
}

std::unique_ptr<Image> Image::affineWarpBilinear(const double* M, int out_width, int out_height) const
{
    if (info.pixelSizeBytes != 3)
    {
        common::log_error("Image::affineWarpBilinear - Only RGB images are supported");
        return nullptr;
    }

    // TODO: we are creating the matrix and inverting it all the time. Why not create the inverted matrix directly?
    double invM[6];
    if (!math_utils::invert_affine(M, invM))
    {
        common::log_error("Image::affineWarpGeneric - Invalid affine matrix provided");
        return nullptr;
    }

    size_t out_size = out_width * out_height * info.pixelSizeBytes;
    auto out_img = std::make_unique<Image>(out_size);
    out_img->info = info;
    out_img->info.width = out_width;
    out_img->info.height = out_height;
    out_img->info.pixelSizeBytes = info.pixelSizeBytes;
    out_img->info.format = ImageFormat::RGB;
    const int in_width = info.width;
    const int in_height = info.height;

    unsigned char* dst = out_img->data();
    const unsigned char* src = data_.get();

    for (int y = 0; y < out_height; ++y)
    {
        for (int x = 0; x < out_width; ++x)
        {
            double src_x = invM[0] * x + invM[1] * y + invM[2];
            double src_y = invM[3] * x + invM[4] * y + invM[5];
            unsigned char* pdst = dst + (y * out_width + x) * 3;
            int x0 = static_cast<int>(std::floor(src_x));
            int y0 = static_cast<int>(std::floor(src_y));
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            if (x0 >= 0 && x1 < in_width && y0 >= 0 && y1 < in_height)
            {
                double wx = src_x - x0;
                double wy = src_y - y0;

                for (int c = 0; c < 3; ++c)
                {
                    double v = (1 - wx) * (1 - wy) * src[(y0 * in_width + x0) * 3 + c]
                               + wx * (1 - wy) * src[(y0 * in_width + x1) * 3 + c]
                               + (1 - wx) * wy * src[(y1 * in_width + x0) * 3 + c]
                               + wx * wy * src[(y1 * in_width + x1) * 3 + c];

                    pdst[c] = static_cast<unsigned char>(std::round(std::clamp(v, 0.0, 255.0)));
                }
            }
            else
            {
                // If out of bounds, set black
                std::memset(pdst, 0, 3);
            }
        }
    }

    return out_img;
}

std::unique_ptr<Image> Image::affineWarpNearestNeighbour(const double* M, int out_width, int out_height) const
{
    if (info.pixelSizeBytes != 1)
    {
        common::log_error("Image::affineWarpNearestNeighbour - Only single-channel images are supported");
        return nullptr;
    }

    // TODO: we are creating the matrix and inverting it all the time. Why not create the inverted matrix directly?
    double invM[6];
    if (!math_utils::invert_affine(M, invM))
    {
        common::log_error("Image::affineWarpGeneric - Invalid affine matrix provided");
        return nullptr;
    }

    size_t out_size = out_width * out_height;
    auto out_img = std::make_unique<Image>(out_size);
    out_img->info = info;
    out_img->info.width = out_width;
    out_img->info.height = out_height;
    out_img->info.pixelSizeBytes = 1;
    out_img->info.format = ImageFormat::GRAYSCALE;
    const int in_width = info.width;
    const int in_height = info.height;

    unsigned char* dst = out_img->data();
    const unsigned char* src = data_.get();

    for (int y = 0; y < out_height; ++y)
    {
        for (int x = 0; x < out_width; ++x)
        {
            double src_x = invM[0] * x + invM[1] * y + invM[2];
            double src_y = invM[3] * x + invM[4] * y + invM[5];
            unsigned char* pdst = dst + (y * out_width + x);


            // Nearest neighbor
            int ix = static_cast<int>(std::round(src_x));
            int iy = static_cast<int>(std::round(src_y));

            if (ix >= 0 && ix < in_width && iy >= 0 && iy < in_height)
            {
                const unsigned char* psrc = src + (iy * in_width + ix);
                pdst[0] = *psrc; // Copy the pixel value
            }
            else
            {
                pdst[0] = 0; // Out of bounds, set to black
            }
        }
    }

    return out_img;
}

void Image::alphaBlend(const Image& src, const Image& mask)
{
    // Assumes all images are the same size and mask is single-channel
    if (info.width != src.info.width || info.height != src.info.height || info.width != mask.info.width
        || info.height != mask.info.height || info.pixelSizeBytes != 3 || src.info.pixelSizeBytes != 3
        || mask.info.pixelSizeBytes != 1)
    {
        // Invalid input, do nothing
        common::log_error("Image::alphaBlend - Image sizes or pixel formats do not match");
        return;
    }
    unsigned char* dst_data = this->data();
    const unsigned char* src_data = src.data();
    const unsigned char* mask_data = mask.data();
    const int npixels = info.width * info.height;
    for (int i = 0; i < npixels; ++i)
    {
        // Blend each pixel using the mask
        PixelOperations::blendPixels(dst_data + i * 3, src_data + i * 3, 3, mask_data[i]);
    }
}
