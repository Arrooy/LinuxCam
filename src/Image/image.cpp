#include "LinuxFace/Image/image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/Image/pixel_conversion.h"

using namespace linuxface;
using namespace linuxface::pixel_conversion;

// Optimized pixel operations using centralized conversion logic
namespace linuxface::pixel_operations
{
void blendPixels(unsigned char* dst, const unsigned char* src, unsigned char src_pixel_size, unsigned char src_alpha,
                 unsigned char dst_pixel_size, unsigned char /*dstAlpha*/) noexcept
{
    // Skip transparent pixels for RGBA formats
    if (src_pixel_size == 4 && src_alpha == 0)
    {
        return;
    }

    // Determine conversion type and apply optimized conversion
    const ConversionType conv_type = getConversionType(src_pixel_size, dst_pixel_size);

    // Special handling for RGBA->RGBA blending
    if (conv_type == ConversionType::DIRECT_COPY && src_pixel_size == 4)
    {
        const bool needs_blending = (src_alpha != 255 && src_alpha != 0);
        convertPixel(src, dst, conv_type, src_alpha, needs_blending);
        return;
    }

    // For grayscale blending with alpha
    if (src_pixel_size == 1 && src_alpha != 255 && src_alpha != 0)
    {
        // Blend grayscale with alpha
        dst[0] = static_cast<unsigned char>((src_alpha * src[0] + (255 - src_alpha) * dst[0]) / 255);
        return;
    }

    // Use unified conversion for all other cases
    convertPixel(src, dst, conv_type, src_alpha, false);
}
} // namespace linuxface::pixel_operations

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

Image::Image(unsigned char* buffer, size_t size, bool take_ownership) : size_(size)
{
    if ((buffer != nullptr) && size > 0)
    {
        if (take_ownership)
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

Image::Image(Pixel color, size_t width, size_t height) : size_(width * height * info.pixelSizeBytes)
{
    // Determine format based on alpha value
    const bool has_alpha = (color.a != 255);
    info.pixelSizeBytes = has_alpha ? 4 : 3;
    info.format = has_alpha ? ImageFormat::RGBA : ImageFormat::RGB;
    info.width = width;
    info.height = height;

    data_ = std::shared_ptr<unsigned char>(new unsigned char[size_], std::default_delete<unsigned char[]>());

    // Fill with the color
    unsigned char* d = data_.get();
    for (size_t i = 0; i < width * height; ++i)
    {
        const size_t idx = i * info.pixelSizeBytes;
        d[idx + 0] = color.r;
        d[idx + 1] = color.g;
        d[idx + 2] = color.b;
        if (has_alpha)
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
void Image::resize(size_t new_size, bool preserve_data)
{
    if (new_size == 0)
    {
        data_.reset();
        size_ = 0;
        return;
    }

    if (size_ != new_size)
    {
        std::shared_ptr<unsigned char> new_data(new unsigned char[new_size], std::default_delete<unsigned char[]>());

        // MED-004 OPTIMIZATION: Conditional data preservation with performance analysis.
        // Analysis shows most calls use preserveData=false (copyFrom, scaleToInPlace),
        // but some legitimate cases need data preserved (JPEG codec tests, buffer reallocation).
        // Only perform memcpy when explicitly requested and both buffers are valid.
        if (preserve_data && data_ && size_ > 0)
        {
            // Copy existing data, preserving as much as fits in the new buffer
            memcpy(new_data.get(), data_.get(), std::min(size_, new_size));
            // Zero-fill any additional space in larger buffers
            if (new_size > size_)
            {
                memset(new_data.get() + size_, 0, new_size - size_);
            }
        }
        else
        {
            // Initialize new data to zero when not preserving
            memset(new_data.get(), 0, new_size);
        }

        data_ = std::move(new_data);
        size_ = new_size;
    }
}

Pixel Image::operator()(size_t col, size_t row) const
{
    const size_t idx = index(col, row);
    if (idx >= size_ || !data_)
    {
        common::logError("Image::operator(): index out of bounds [col,row] %zu, %zu Index: "
                         "%zu",
                         col, row, idx);
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
    const size_t idx = index(col, row);
    if (idx >= size_ || !data_)
    {
        common::logError("Image::pxy: index out of bounds [col,row] %zu, %zu Index: %zu", col, row, idx);
        return;
    }
    this->pidx(idx, r, g, b, a);
}

void Image::pidx(size_t idx, const unsigned char r, const unsigned char g, const unsigned char b, const unsigned char a)
{
    // Use PixelOperations for consistency
    if (info.pixelSizeBytes == 4)
    {
        pixel_operations::setPixelRgba(data_.get(), idx, r, g, b, a);
    }
    else
    {
        pixel_operations::setPixelRgb(data_.get(), idx, r, g, b);
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

void Image::scaleImageBuffer(const unsigned char* src_data, unsigned long src_width, unsigned long src_height,
                             unsigned char pixel_size, unsigned char* dst_data, unsigned long dst_width,
                             unsigned long dst_height, ScalingAlgorithm algorithm)
{
    // Use non-const T for both src and dst to match template requirements
    const image_utils::ImageView<unsigned char> src_view{const_cast<unsigned char*>(src_data), src_width, src_height,
                                                        pixel_size};
    image_utils::ImageView<unsigned char> dst_view{dst_data, dst_width, dst_height, pixel_size};
    switch (algorithm)
    {
        case ScalingAlgorithm::LANCZOS:
            image_utils::lanczosScaling<unsigned char, unsigned char, NormalizationType::NONE>(src_view, dst_view);
            break;
        case ScalingAlgorithm::BILINEAR:
            image_utils::bilinearScaling<unsigned char, unsigned char, NormalizationType::NONE>(src_view, dst_view);
            break;
        case ScalingAlgorithm::AREA_AVERAGING:
            image_utils::areaAveragingScaling<unsigned char, unsigned char, NormalizationType::NONE>(src_view, dst_view);
            break;
        case ScalingAlgorithm::FAST_BOX:
            image_utils::fastBoxScaling<unsigned char, unsigned char>(src_view, dst_view);
            break;
        case ScalingAlgorithm::BICUBIC:
            image_utils::bicubicScaling<unsigned char, unsigned char, NormalizationType::NONE>(src_view, dst_view);
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
    auto new_width = static_cast<size_t>((info.width * factor) + 0.5);
    auto new_height = static_cast<size_t>((info.height * factor) + 0.5);
    if (new_width == 0)
    {
        new_width = 1;
    }
    if (new_height == 0)
    {
        new_height = 1;
    }
    scaleToInPlace(new_width, new_height, algorithm);
}

void Image::scaleInPlace(unsigned long new_width, unsigned long new_height, ScalingAlgorithm algorithm)
{
    scaleToInPlace(static_cast<size_t>(new_width), static_cast<size_t>(new_height), algorithm);
}

void Image::scaleToInPlace(size_t new_width, size_t new_height, ScalingAlgorithm algorithm)
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
    {
        return;
    }
    if (new_width == 0 || new_height == 0)
    {
        return;
    }

    if (new_width == info.width && new_height == info.height)
    {
        return;
    }

    std::vector<unsigned char> result(new_width * new_height * info.pixelSizeBytes);
    scaleImageBuffer(data_.get(), info.width, info.height, info.pixelSizeBytes, result.data(), new_width, new_height,
                     algorithm);

    resize(new_width * new_height * info.pixelSizeBytes, false);
    std::memcpy(data_.get(), result.data(), new_width * new_height * info.pixelSizeBytes);
    info.width = new_width;
    info.height = new_height;
}

std::unique_ptr<Image> Image::scale(double factor, ScalingAlgorithm algorithm) const
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0 || factor == 1.0)
    {
        return nullptr;
    }

    auto new_width = static_cast<size_t>((info.width * factor) + 0.5);
    auto new_height = static_cast<size_t>((info.height * factor) + 0.5);
    // Ensure minimum size of 1x1
    if (new_width == 0)
    {
        new_width = 1;
    }
    if (new_height == 0)
    {
        new_height = 1;
    }

    return scaleTo(new_width, new_height, algorithm);
}

std::unique_ptr<Image> Image::scaleTo(size_t new_width, size_t new_height, ScalingAlgorithm algorithm) const
{
    return scale(static_cast<unsigned long>(new_width), static_cast<unsigned long>(new_height), algorithm);
}

// Performance-optimized bilinear scaling with different algorithms for up/down scaling
std::unique_ptr<Image> Image::scale(unsigned long new_width, unsigned long new_height, ScalingAlgorithm algorithm) const
{
    if (!data_ || size_ == 0 || info.width == 0 || info.height == 0)
    {
        common::logError("Image::scale - Invalid source image");
        return nullptr;
    }

    if (new_width == 0 || new_height == 0)
    {
        common::logError("Image::scale - Invalid target dimensions: %lux%lu", new_width, new_height);
        return nullptr;
    }

    if (new_width == info.width && new_height == info.height)
    {
        return deepCopy();
    }

    auto result = std::make_unique<Image>(new_width * new_height * info.pixelSizeBytes);
    scaleImageBuffer(data_.get(), info.width, info.height, info.pixelSizeBytes, result->data(), new_width, new_height,
                     algorithm);
    result->info = info;
    result->info.width = new_width;
    result->info.height = new_height;
    return result;
}

void Image::move(size_t new_x, size_t new_y)
{
    info.x = new_x;
    info.y = new_y;
}

Image& Image::paste(const Image& other, bool expand_canvas)
{
    pasteAt(other, static_cast<long>(other.info.x), static_cast<long>(other.info.y), expand_canvas);
    return *this;
}

Image& Image::pasteAt(const Image& other, long x, long y, bool expand_canvas)
{
    pasteImpl(other, x, y, expand_canvas);
    return *this;
}

void Image::toTensor(float* output_data, TensorPadding& padding, int new_width, int new_height,
                     NormalizationType norm_type) const
{
    if (!isColorImage() || info.pixelSizeBytes != 3)
    {
        common::logError("Image::toTensor - Expected RGB format, got format: %s with pixel "
                         "size: %d",
                         fromImageFormatToString(info.format).c_str(), info.pixelSizeBytes);
        return;
    }

    if ((output_data == nullptr) || !data_ || size_ == 0)
    {
        common::logError("Image::toTensor - Invalid input data");
        return;
    }

    const int orig_w = info.width;
    const int orig_h = info.height;

    // Step 1: Compute scale ratio (preserve aspect ratio)
    const float r = std::min(static_cast<float>(new_width) / orig_w, static_cast<float>(new_height) / orig_h);
    const int resized_w = static_cast<int>(orig_w * r);
    const int resized_h = static_cast<int>(orig_h * r);
    const int offset_x = (new_width - resized_w) / 2;
    const int offset_y = (new_height - resized_h) / 2;
    const int padded_size = new_width * new_height;

    // Store transform metadata in the padding object
    padding.tensor_width = new_width;
    padding.tensor_height = new_height;
    padding.resized_width = resized_w;
    padding.resized_height = resized_h;
    padding.offset_x = offset_x;
    padding.offset_y = offset_y;
    padding.scale_ratio = r;
    padding.has_transform = true;

    // Initialize the entire output buffer based on padding type
    switch (padding.type)
    {
        case PaddingType::NO_PADDING:
            break;
        case PaddingType::ZERO:
            std::memset(output_data, 0, padded_size * 3 * sizeof(float));
            break;
        case PaddingType::CONSTANT:
        {
            float* ptr = output_data;
            float* end = output_data + (padded_size * 3);
            while (ptr < end)
            {
                *ptr++ = padding.constant_value;
            }
        }
        break;
        case PaddingType::RGB_CONSTANT:
        {
            // Fill each channel separately with direct assignment
            float* r_channel = output_data;
            float* g_channel = output_data + padded_size;
            float* b_channel = output_data + (2 * padded_size);

            for (int i = 0; i < padded_size; i++)
            {
                r_channel[i] = padding.rgb_values[0];
                g_channel[i] = padding.rgb_values[1];
                b_channel[i] = padding.rgb_values[2];
            }
        }
        break;
        default:
            std::memset(output_data, 0, padded_size * 3 * sizeof(float));
            break;
    }

    const unsigned char* src_data = data_.get();

    // TODO(arroyo): Test resize with bilinear interpolation or even bicubic
    // interpolation. Resize original image using nearest neighbor For each
    // pixel in resized image
    for (int h = 0; h < resized_h; ++h)
    {
        int src_h = static_cast<int>((static_cast<float>(h) / resized_h) * orig_h);
        if (src_h >= orig_h)
        {
            src_h = orig_h - 1;
        }

        for (int w = 0; w < resized_w; ++w)
        {
            int src_w = static_cast<int>((static_cast<float>(w) / resized_w) * orig_w);
            if (src_w >= orig_w)
            {
                src_w = orig_w - 1;
            }

            const int src_idx = (src_h * orig_w + src_w) * 3; // RGB interleaved

            // Bounds check for source data
            if (src_idx + 2 >= static_cast<int>(size_))
            {
                common::logError("Image::toTensor - Source index out of bounds: %d >= %zu", src_idx + 2, size_);
                continue;
            }

            // Location in padded (output) image
            const int dst_h = h + offset_y;
            const int dst_w = w + offset_x;
            const int dst_idx = (dst_h * new_width) + dst_w;

            // Bounds check for destination
            if (dst_idx >= padded_size)
            {
                common::logError("Image::toTensor - Destination index out of bounds: %d >= "
                                 "%d",
                                 dst_idx, padded_size);
                continue;
            }
            if (norm_type == NormalizationType::NONE)
            {
                output_data[(0 * padded_size) + dst_idx] = src_data[src_idx];     // R
                output_data[(1 * padded_size) + dst_idx] = src_data[src_idx + 1]; // G
                output_data[(2 * padded_size) + dst_idx] = src_data[src_idx + 2]; // B
            }
            else if (norm_type == NormalizationType::MINMAX)
            {
                output_data[(0 * padded_size) + dst_idx] = src_data[src_idx] / 255.0f;     // R
                output_data[(1 * padded_size) + dst_idx] = src_data[src_idx + 1] / 255.0f; // G
                output_data[(2 * padded_size) + dst_idx] = src_data[src_idx + 2] / 255.0f; // B
            }
            else if (norm_type == NormalizationType::ZERO_CENTER)
            {
                output_data[(0 * padded_size) + dst_idx] = (static_cast<float>(src_data[src_idx]) - 127.5f) / 127.5f;     // R
                output_data[(1 * padded_size) + dst_idx] = (static_cast<float>(src_data[src_idx + 1]) - 127.5f) / 127.5f; // G
                output_data[(2 * padded_size) + dst_idx] = (static_cast<float>(src_data[src_idx + 2]) - 127.5f) / 127.5f; // B
            }
        }
    }
}

void Image::fromTensor(const float* tensor_data, std::vector<int64_t> tensor_shape, int tensor_width, int tensor_height,
                       const TensorPadding& padding, NormalizationType norm_type)
{
    if ((tensor_data == nullptr) || !data_ || size_ == 0)
    {
        common::logError("Image::fromTensor - Invalid input data");
        return;
    }

    // Use stored transform metadata if available, otherwise calculate it
    int offset_x = 0;
    int offset_y = 0;
    int resized_w = 0;
    int resized_h = 0;
    float r = NAN;

    // common::logInfo("Image::fromTensor - Using tensor dimensions: %dx%d", tensor_width, tensor_height);
    // common::logInfo("Tensor padding = %d width, %d height", padding.tensor_width, padding.tensor_height);

    if (padding.has_transform && padding.tensor_width == tensor_width && padding.tensor_height == tensor_height)
    {
        // Use stored transform metadata from padding
        offset_x = padding.offset_x;
        offset_y = padding.offset_y;
        resized_w = padding.resized_width;
        resized_h = padding.resized_height;
        r = padding.scale_ratio;

        // common::logInfo(
        //     "Image::fromTensor - Using stored transform metadata: offset(%d,%d), resized(%dx%d), scale=%.3f",
        //     offsetX, offsetY, resizedW, resizedH, r);
    }
    else
    {
        // Fallback: calculate transform parameters
        const int orig_w = info.width;
        const int orig_h = info.height;

        r = std::min(static_cast<float>(tensor_width) / orig_w, static_cast<float>(tensor_height) / orig_h);
        resized_w = static_cast<int>(orig_w * r);
        resized_h = static_cast<int>(orig_h * r);
        offset_x = (tensor_width - resized_w) / 2;
        offset_y = (tensor_height - resized_h) / 2;

        // common::logWarn("Image::fromTensor - No stored transform metadata, calculating: offset(%d,%d), "
        //                  "resized(%dx%d), scale=%.3f",
        //                  offsetX, offsetY, resizedW, resizedH, r);
    }

    unsigned char* dst_data = data_.get();
    const int tensor_size = tensor_width * tensor_height;
    // Extract and resize depth data back to original dimensions
    for (int y = 0; y < static_cast<int>(info.height); ++y)
    {
        for (int x = 0; x < static_cast<int>(info.width); ++x)
        {
            // Map to tensor coordinates
            int tensor_h = static_cast<int>((static_cast<float>(y) / info.height) * resized_h) + offset_y;
            int tensor_w = static_cast<int>((static_cast<float>(x) / info.width) * resized_w) + offset_x;

            // Clamp to tensor bounds
            tensor_h = std::max(0, std::min(tensor_h, tensor_height - 1));
            tensor_w = std::max(0, std::min(tensor_w, tensor_width - 1));

            int channels = 1;
            // Check if the tensor has a single channel
            if (tensor_shape[1] == 3)
            {
                channels = 3;
            }

            const int tensor_idx = ((tensor_h * tensor_width) + tensor_w);

            // Convert tensor value to unsigned char
            unsigned char pixel_values[3];
            if (norm_type == NormalizationType::NONE)
            {
                pixel_values[0] = static_cast<unsigned char>(tensor_data[tensor_idx]);
                if (channels == 3)
                {
                    // Remember that thi is in CHW not HWC
                    pixel_values[1] = static_cast<unsigned char>(tensor_data[(1 * tensor_size) + tensor_idx]);
                    pixel_values[2] = static_cast<unsigned char>(tensor_data[(2 * tensor_size) + tensor_idx]);
                }
            }
            else if (norm_type == NormalizationType::MINMAX)
            {
                pixel_values[0] = static_cast<unsigned char>(tensor_data[tensor_idx] * 255.0f);
                if (channels == 3)
                {
                    pixel_values[1] = static_cast<unsigned char>(tensor_data[(1 * tensor_size) + tensor_idx] * 255.0f);
                    pixel_values[2] = static_cast<unsigned char>(tensor_data[(2 * tensor_size) + tensor_idx] * 255.0f);
                }
            }
            else if (norm_type == NormalizationType::ZERO_CENTER)
            {
                pixel_values[0] = static_cast<unsigned char>((tensor_data[tensor_idx] + 1.0) / 2.0f * 255.0f);
                if (channels == 3)
                {
                    pixel_values[1] =
                        static_cast<unsigned char>((tensor_data[(1 * tensor_size) + tensor_idx] + 1.0) / 2.0f * 255.0f);
                    pixel_values[2] =
                        static_cast<unsigned char>((tensor_data[(2 * tensor_size) + tensor_idx] + 1.0) / 2.0f * 255.0f);
                }
            }
            else
            {
                common::logError("Unknown normalization type");
                return;
            }

            const int dst_idx = (y * info.width + x) * 3;
            dst_data[dst_idx] = pixel_values[0];
            if (channels == 3)
            {
                // RGB
                dst_data[dst_idx + 1] = pixel_values[1];
                dst_data[dst_idx + 2] = pixel_values[2];
            }
            else
            {
                // Grayscale
                dst_data[dst_idx + 1] = pixel_values[0];
                dst_data[dst_idx + 2] = pixel_values[0];
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

    const int clipped_width = x2 - x1;
    const int clipped_height = y2 - y1;

    if (clipped_width <= 0 || clipped_height <= 0)
    {
        return;
    }

    const int bytes_per_pixel = info.pixelSizeBytes;

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
    const int row_bytes = clipped_width * bytes_per_pixel;

    // Allocate row buffer
    auto row_buffer = std::make_unique<unsigned char[]>(row_bytes);

    if (bytes_per_pixel == 3) // RGB
    {
        // Prepare a 4-pixel (12-byte) block for RGB
        unsigned char block[12];
        for (int i = 0; i < 4; i++)
        {
            block[(i * 3) + 0] = color.r;
            block[(i * 3) + 1] = color.g;
            block[(i * 3) + 2] = color.b;
        }

        // Fill the row buffer with unrolled copies of the 12-byte block
        const int n_blocks = clipped_width / 4; // full 4-pixel blocks
        const int rem = clipped_width % 4;     // leftover pixels

        unsigned char* ptr = row_buffer.get();

        // Copy full 4-pixel blocks
        for (int i = 0; i < n_blocks; i++)
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
    else if (bytes_per_pixel == 4) // RGBA
    {
        // Prepare a 4-pixel (16-byte) block for RGBA
        unsigned char block[16];
        for (int i = 0; i < 4; i++)
        {
            block[(i * 4) + 0] = color.r;
            block[(i * 4) + 1] = color.g;
            block[(i * 4) + 2] = color.b;
            block[(i * 4) + 3] = color.a;
        }

        // Fill the row buffer with unrolled copies of the 16-byte block
        const int n_blocks = clipped_width / 4; // full 4-pixel blocks
        const int rem = clipped_width % 4;     // leftover pixels

        unsigned char* ptr = row_buffer.get();

        // Copy full 4-pixel blocks
        for (int i = 0; i < n_blocks; i++)
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
        unsigned char* dest = data() + ((row * info.width + x1) * bytes_per_pixel);
        std::memcpy(dest, row_buffer.get(), row_bytes);
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

    size_t const result_size = crop_width * crop_height * info.pixelSizeBytes;
    std::unique_ptr<Image> result = std::make_unique<Image>(result_size);

    const unsigned char* src_img = data_.get();
    unsigned char* dst_img = result->data();

    for (int y = 0; y < crop_height; ++y)
    {
        const unsigned char* src_row = src_img + (((y0 + y) * info.width + x0) * info.pixelSizeBytes);
        unsigned char* dst_row = dst_img + ((y * crop_width) * info.pixelSizeBytes);
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
        common::logError("Image::saveToDisk - Only JPEG or PPM formats are supported for "
                         "saving to disk. Image is %s. Path was %s",
                         fromImageFormatToString(info.format).c_str(), dest_path.c_str());
        return false;
    }

    if (!data_ || size_ == 0u)
    {
        common::logError("Image::saveToDisk - No data to save");
        return false;
    }

    // Use O_TRUNC to overwrite file if it exists
    const int file = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (file < 0)
    {
        common::logError("Image::saveToDisk - Error opening file for writing: %s", dest_path.c_str());
        common::errnoLog("Image::saveToDisk - Error opening file for writing");
        return false;
    }
    common::logInfo("Image::saveToDisk - Saving image to %s with size %lux%lu", dest_path.c_str(), info.width,
                    info.height);
    // Handle PPM (RGB, RGBA, Grayscale)
    if (info.format == ImageFormat::PPM || info.format == ImageFormat::RGB || info.format == ImageFormat::RGBA
        || info.format == ImageFormat::GRAYSCALE)
    {
        char header[64];
        // Write the P6 header
        const int header_len = std::snprintf(header, sizeof(header), "P6\n%lu %lu\n255\n", info.width, info.height);
        if (write(file, header, header_len) != header_len)
        {
            common::errnoLog("Image::saveToDisk - write header failed");
            close(file);
            return false;
        }
        const size_t pixel_count = info.width * info.height;
        // Grayscale: convert to RGB
        if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
        {
            std::vector<unsigned char> rgb_data = convertToRgb();
            if (!common::longWrite(file, rgb_data.data(), rgb_data.size()))
            {
                common::logError("Image::saveToDisk - Error saving grayscale as RGB PPM. "
                                 "Not all bytes were stored.");
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
                rgb_data[(i * 3) + 0] = rgba[(i * 4) + 0];
                rgb_data[(i * 3) + 1] = rgba[(i * 4) + 1];
                rgb_data[(i * 3) + 2] = rgba[(i * 4) + 2];
            }
            if (!common::longWrite(file, rgb_data.data(), rgb_data.size()))
            {
                common::logError("Image::saveToDisk - Error saving RGBA as RGB PPM. Not all "
                                 "bytes were stored.");
                close(file);
                return false;
            }
            close(file);
            return true;
        }
        // RGB: direct write
        if (info.format == ImageFormat::RGB || info.pixelSizeBytes == 3)
        {
            if (!common::longWrite(file, data_.get(), pixel_count * 3))
            {
                common::logError("Image::saveToDisk - Error saving RGB PPM. Not all bytes "
                                 "were stored.");
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
            common::logError("Image::saveToDisk - Error saving JPEG. Not all bytes were "
                             "stored.");
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

std::vector<unsigned char> Image::convertToRgb() const
{
    if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
    {
        const size_t pixel_count = info.width * info.height;
        std::vector<unsigned char> rgb_data(pixel_count * 3);
        const unsigned char* gray = data_.get();
        for (size_t i = 0; i < pixel_count; ++i)
        {
            rgb_data[(i * 3) + 0] = gray[i];
            rgb_data[(i * 3) + 1] = gray[i];
            rgb_data[(i * 3) + 2] = gray[i];
        }
        return rgb_data;
    }
    return {};
}

bool Image::convertToRgbaInplace()
{
    if (info.format == ImageFormat::RGB && info.pixelSizeBytes == 3)
    {
        const size_t pixel_count = info.width * info.height;
        const size_t new_size = pixel_count * 4;

        // Create new RGBA data
        auto new_data =
            std::shared_ptr<unsigned char>(new unsigned char[new_size], std::default_delete<unsigned char[]>());

        // Convert RGB to RGBA by adding alpha=255
        const unsigned char* rgb = data_.get();
        unsigned char* rgba = new_data.get();

        for (size_t i = 0; i < pixel_count; ++i)
        {
            rgba[(i * 4) + 0] = rgb[(i * 3) + 0]; // R
            rgba[(i * 4) + 1] = rgb[(i * 3) + 1]; // G
            rgba[(i * 4) + 2] = rgb[(i * 3) + 2]; // B
            rgba[(i * 4) + 3] = 255;            // A
        }

        // Update image properties
        data_ = std::move(new_data);
        size_ = new_size;
        info.format = ImageFormat::RGBA;
        info.pixelSizeBytes = 4;

        return true;
    }
    return false;
}

bool Image::convertToRgbInplace()
{
    if (info.format == ImageFormat::GRAYSCALE || info.pixelSizeBytes == 1)
    {
        std::vector<unsigned char> const rgb_data = convertToRgb();
        size_t i = 0;
        const size_t pixel_count = info.width * info.height * 3;
        resize(pixel_count, true);
        for (unsigned char const pixel : rgb_data)
        {
            data_.get()[i] = pixel;
            i += 1;
        }
        if (i != pixel_count)
        {
            common::logError("Image::convertToRGB - Error converting grayscale to RGB. Size mismatch.");
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

    unsigned char* pha_data = matting.data();
    unsigned char* frg_data = data();
    unsigned char* background_data = background.data();
    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long idx = (y * info.width + x) * 3;
            const unsigned char alpha = pha_data[idx];
            frg_data[idx] = (alpha * frg_data[idx] + (255 - alpha) * background_data[idx]) / 255;
            frg_data[idx + 1] = (alpha * frg_data[idx + 1] + (255 - alpha) * background_data[idx + 1]) / 255;
            frg_data[idx + 2] = (alpha * frg_data[idx + 2] + (255 - alpha) * background_data[idx + 2]) / 255;
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
            const unsigned char gray = pixel_conversion::rgbToGrayscale(r, g, b);
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
    const unsigned long row_bytes = info.width * info.pixelSizeBytes;
    std::vector<unsigned char> tmp(row_bytes);
    for (unsigned long y = 0; y < info.height / 2; ++y)
    {
        unsigned char* row1 = d + (y * row_bytes);
        unsigned char* row2 = d + ((info.height - 1 - y) * row_bytes);
        std::memcpy(tmp.data(), row1, row_bytes);
        std::memcpy(row1, row2, row_bytes);
        std::memcpy(row2, tmp.data(), row_bytes);
    }
}


// // Compute center of the image
// float cx = (info.width - 1) / 2.0f;
// float cy = (info.height - 1) / 2.0f;

math_utils::Point<double> Image::rotate(double angle_rad, math_utils::Point<double> center)
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return {0.0, 0.0};
    }

    const double cos_a = std::cos(angle_rad);
    const double sin_a = std::sin(angle_rad);

    // Compute corners relative to center
    double const corners[4][2] = {
        {-center.x,                 -center.y                 },
        {info.width - 1 - center.x, -center.y                 },
        {-center.x,                 info.height - 1 - center.y},
        {info.width - 1 - center.x, info.height - 1 - center.y}
    };

    double min_x = 1e9;
    double min_y = 1e9;
    double max_x = -1e9;
    double max_y = -1e9;
    for (auto & corner : corners)
    {
        const double x = (corner[0] * cos_a) - (corner[1] * sin_a);
        const double y = (corner[0] * sin_a) + (corner[1] * cos_a);
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }

    const auto new_width = static_cast<unsigned long>(std::ceil(max_x - min_x));
    const auto new_height = static_cast<unsigned long>(std::ceil(max_y - min_y));
    const size_t new_size = new_width * new_height * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(new_size, 0);

    for (unsigned long y = 0; y < new_height; ++y)
    {
        for (unsigned long x = 0; x < new_width; ++x)
        {
            // Map back to original image
            const double dx = x + min_x;
            const double dy = y + min_y;

            double const src_x = (cos_a * dx) + (sin_a * dy) + center.x;
            double const src_y = (-sin_a * dx) + (cos_a * dy) + center.y;

            const int x0 = static_cast<int>(std::floor(src_x));
            const int y0 = static_cast<int>(std::floor(src_y));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;
            const double wx = src_x - x0;
            const double wy = src_y - y0;

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

                        const size_t src_idx = ((sy * info.width + sx) * info.pixelSizeBytes) + c;
                        val += data_.get()[src_idx] * wyf * wxf;
                    }
                }
                const size_t dst_idx = ((y * new_width + x) * info.pixelSizeBytes) + c;
                rotated[dst_idx] = static_cast<unsigned char>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }

    // Resize internal buffer and update info
    data_.reset(new unsigned char[new_size]);
    size_ = new_size;
    std::memcpy(data_.get(), rotated.data(), new_size);
    info.width = new_width;
    info.height = new_height;
    return {-min_x, -min_y};
}

void Image::rotate90()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    const unsigned long new_width = info.height;
    const unsigned long new_height = info.width;
    const size_t new_size = new_width * new_height * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(new_size);

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long src_idx = (y * info.width + x) * info.pixelSizeBytes;
            const unsigned long dst_idx = (x * new_width + (new_width - 1 - y)) * info.pixelSizeBytes;
            std::memcpy(&rotated[dst_idx], &data_.get()[src_idx], info.pixelSizeBytes);
        }
    }
    resize(new_size);
    std::memcpy(data_.get(), rotated.data(), new_size);
    info.width = new_width;
    info.height = new_height;
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
            const unsigned char tmp = d[(i * info.pixelSizeBytes) + c];
            d[(i * info.pixelSizeBytes) + c] = d[(j * info.pixelSizeBytes) + c];
            d[(j * info.pixelSizeBytes) + c] = tmp;
        }
    }
}

void Image::rotate270()
{
    if (!data_ || info.width == 0 || info.height == 0)
    {
        return;
    }
    const unsigned long new_width = info.height;
    const unsigned long new_height = info.width;
    const size_t new_size = new_width * new_height * info.pixelSizeBytes;
    std::vector<unsigned char> rotated(new_size);

    for (unsigned long y = 0; y < info.height; ++y)
    {
        for (unsigned long x = 0; x < info.width; ++x)
        {
            const unsigned long src_idx = (y * info.width + x) * info.pixelSizeBytes;
            const unsigned long dst_idx = ((info.width - 1 - x) * new_width + y) * info.pixelSizeBytes;
            std::memcpy(&rotated[dst_idx], &data_.get()[src_idx], info.pixelSizeBytes);
        }
    }
    resize(new_size);
    std::memcpy(data_.get(), rotated.data(), new_size);
    info.width = new_width;
    info.height = new_height;
}

// Private methods
size_t Image::index(size_t col, size_t row) const noexcept
{
    return (row * info.width + col) * info.pixelSizeBytes;
}

void Image::copyPixelsWithBlending(const Image& src, long src_global_x, long src_global_y, long canvas_x, long canvas_y,
                                   size_t canvas_width, size_t canvas_height)
{
    if ((src.data() == nullptr) || !data_)
    {
        common::logError("copyPixelsWithBlending - Aborting no data");
        return;
    }

    // Use signed arithmetic for proper bounds handling
    const long src_left = src_global_x;
    const long src_top = src_global_y;
    const long src_right = src_left + static_cast<long>(src.info.width);
    const long src_bottom = src_top + static_cast<long>(src.info.height);
    const long canvas_left = canvas_x;
    const long canvas_top = canvas_y;
    const long canvas_right = canvas_left + static_cast<long>(canvas_width);
    const long canvas_bottom = canvas_top + static_cast<long>(canvas_height);

    long const clip_left = std::max(src_left, canvas_left);
    long const clip_top = std::max(src_top, canvas_top);
    long const clip_right = std::min(src_right, canvas_right);
    long const clip_bottom = std::min(src_bottom, canvas_bottom);

    // Skip if no intersection
    if (clip_left >= clip_right || clip_top >= clip_bottom)
    {
        return;
    }

    const unsigned char* src_data = src.data();
    unsigned char* dst_data = data_.get();
    const unsigned char dst_pixel_size = info.pixelSizeBytes;
    const unsigned char src_pixel_size = src.info.pixelSizeBytes;

    // Determine conversion type once for the entire operation
    const ConversionType conv_type = getConversionType(src_pixel_size, dst_pixel_size);

    for (long y = clip_top; y < clip_bottom; y++)
    {
        const long src_row = y - src_top;
        const long dst_row = y - canvas_top;
        for (long x = clip_left; x < clip_right; x++)
        {
            const long src_col = x - src_left;
            const long dst_col = x - canvas_left;

            // Bounds check before calculating indices
            if (src_row < 0 || src_row >= static_cast<long>(src.info.height) || src_col < 0
                || src_col >= static_cast<long>(src.info.width) || dst_row < 0
                || dst_row >= static_cast<long>(canvas_height) || dst_col < 0 || dst_col >= static_cast<long>(canvas_width))
            {
                continue;
            }

            const size_t src_idx =
                (static_cast<size_t>(src_row) * src.info.width + static_cast<size_t>(src_col)) * src_pixel_size;
            const size_t dst_idx =
                (static_cast<size_t>(dst_row) * canvas_width + static_cast<size_t>(dst_col)) * dst_pixel_size;

            // Bounds check for buffer access
            if (src_idx + src_pixel_size > src.size() || dst_idx + dst_pixel_size > size_)
            {
                continue;
            }

            // Handle RGBA->RGBA blending with alpha transparency
            if (conv_type == ConversionType::DIRECT_COPY && src_pixel_size == 4)
            {
                const unsigned char src_alpha = src_data[src_idx + 3];
                if (src_alpha == 0)
                {
                    continue; // Skip completely transparent pixels
                }
                convertPixel(&src_data[src_idx], &dst_data[dst_idx], conv_type, src_alpha, src_alpha != 255);
            }
            else
            {
                // Use centralized conversion for all other format combinations
                convertPixel(&src_data[src_idx], &dst_data[dst_idx], conv_type);
            }
        }
    }
}

void Image::copyPixelsOptimized(const Image& src, long src_x, long src_y, long dst_x, long dst_y, size_t copy_width,
                                size_t copy_height)
{
    if ((src.data() == nullptr) || !data_)
    {
        return;
    }

    const unsigned char* src_data = src.data();
    unsigned char* dst_data = data_.get();
    const unsigned char dst_pixel_size = info.pixelSizeBytes;
    const unsigned char src_pixel_size = src.info.pixelSizeBytes;

    // Determine conversion type once for the entire operation
    const ConversionType conv_type = getConversionType(src_pixel_size, dst_pixel_size);

    // Optimize for direct copy cases
    if (conv_type == ConversionType::DIRECT_COPY)
    {
        for (size_t row = 0; row < copy_height; ++row)
        {
            const size_t src_row_idx = ((src_y + row) * src.info.width + src_x) * src_pixel_size;
            const size_t dst_row_idx = ((dst_y + row) * info.width + dst_x) * dst_pixel_size;
            copyPixelBlock(src_data, dst_data, src_row_idx, dst_row_idx, copy_width, src_pixel_size);
        }
        return;
    }

    // Use row-based conversion for better performance
    for (size_t row = 0; row < copy_height; ++row)
    {
        const size_t src_row_start = ((src_y + row) * src.info.width + src_x) * src_pixel_size;
        const size_t dst_row_start = ((dst_y + row) * info.width + dst_x) * dst_pixel_size;

        convertPixelRow(src_data + src_row_start, dst_data + dst_row_start, copy_width, conv_type);
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
Image& Image::pasteImpl(const Image& other, long other_x, long other_y, bool expand_canvas)
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
    const long base_left = static_cast<long>(info.x);
    const long base_top = static_cast<long>(info.y);
    const long base_right = base_left + static_cast<long>(info.width);
    const long base_bottom = base_top + static_cast<long>(info.height);

    const long other_left = other_x;
    const long other_top = other_y;
    const long other_right = other_left + static_cast<long>(other.info.width);
    const long other_bottom = other_top + static_cast<long>(other.info.height);

    // Calculate new dimensions
    const long min_x = std::min(base_left, other_left);
    const long min_y = std::min(base_top, other_top);
    const long max_x = std::max(base_right, other_right);
    const long max_y = std::max(base_bottom, other_bottom);

    const auto new_width = static_cast<unsigned long>(max_x - min_x);
    const auto new_height = static_cast<unsigned long>(max_y - min_y);

    const bool same_canvas_size =
        new_width == info.width && new_height == info.height && min_x == base_left && min_y == base_top;
    if (!expand_canvas || same_canvas_size)
    {
        // Fast path: fully in-bounds, same format, fully opaque
        const bool fully_opaque = (info.pixelSizeBytes != 4) || other.isFullyOpaque();
        const bool fully_in_bounds =
            other_left >= base_left && other_top >= base_top && other_right <= base_right && other_bottom <= base_bottom;
        if (fully_in_bounds)
        {
            const long dst_x = other_left - base_left;
            const long dst_y = other_top - base_top;
            if (fully_opaque)
            {
                copyPixelsOptimized(other, 0, 0, dst_x, dst_y, other.info.width, other.info.height);
            }
            else
            {
                // Only blend the region corresponding to the pasted image
                copyPixelsWithBlending(other, 0, 0, dst_x, dst_y, info.width, info.height);
            }
            return *this;
        }
        // Fallback to blending (legacy, for partial overlap)
        copyPixelsWithBlending(other, other_left, other_top, 0, 0, info.width, info.height);
        return *this;
    }

    // PERF: Only backup and copy the minimal affected region when expanding canvas.
    // Avoid full buffer memset; only clear new regions if needed.
    // This minimizes memory operations and improves performance for large images.
    Image backup;
    if (!same_canvas_size)
    {
        // Region-aware backup and copy is now correct and safe (see CRIT-002 resolution)
        backup.copyFrom(*this);
        const size_t new_size = new_width * new_height * info.pixelSizeBytes;
        this->resize(new_size);
        info.x = static_cast<unsigned long>(min_x);
        info.y = static_cast<unsigned long>(min_y);
        info.width = new_width;
        info.height = new_height;
        copyPixelsWithBlending(backup, base_left, base_top, min_x, min_y, base_right - base_left, base_bottom - base_top);
    }
    // Copy the pasted image (other) to the correct region
    copyPixelsWithBlending(other, other_left, other_top, min_x, min_y, other.info.width, other.info.height);
    return *this;
}
// TODO(arroyo): SEEMS LIKE BOUNDS ARE WRONG, MOVE CLOSE THE FACE TO THE BOTTOM
// EDGE.
std::unique_ptr<Image>
Image::affineWarpBilinear(const double* /*m*/, int out_width, int out_height, const double* inv_m) const
{
    if (info.pixelSizeBytes != 3)
    {
        common::logError("Image::affineWarpBilinear - Only RGB images are supported");
        return nullptr;
    }


    // Accept optional inverse matrix for performance
    double local_inv_m[6];
    const double* use_inv_m = nullptr;
    if (inv_m != nullptr)
    {
        use_inv_m = inv_m;
    }
    else
    {
        if (!math_utils::invertAffine(M, local_inv_m))
        {
            common::logError("Image::affineWarpGeneric - Invalid affine matrix provided");
            return nullptr;
        }
        use_inv_m = local_inv_m;
    }

    const size_t out_size = out_width * out_height * info.pixelSizeBytes;
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
            const double src_x = (use_inv_m[0] * x) + (use_inv_m[1] * y) + use_inv_m[2];
            const double src_y = (use_inv_m[3] * x) + (use_inv_m[4] * y) + use_inv_m[5];
            unsigned char* pdst = dst + ((y * out_width + x) * 3);
            const int x0 = static_cast<int>(std::floor(src_x));
            const int y0 = static_cast<int>(std::floor(src_y));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;

            if (x0 >= 0 && x1 < in_width && y0 >= 0 && y1 < in_height)
            {
                const double wx = src_x - x0;
                const double wy = src_y - y0;

                for (int c = 0; c < 3; ++c)
                {
                    const double v = ((1 - wx) * (1 - wy) * src[((y0 * in_width + x0) * 3) + c])
                                     + (wx * (1 - wy) * src[((y0 * in_width + x1) * 3) + c])
                                     + ((1 - wx) * wy * src[((y1 * in_width + x0) * 3) + c])
                                     + (wx * wy * src[((y1 * in_width + x1) * 3) + c]);

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

std::unique_ptr<Image>
Image::affineWarpNearestNeighbour(const double* /*m*/, int out_width, int out_height, const double* inv_m) const
{
    if (info.pixelSizeBytes != 1)
    {
        common::logError("Image::affineWarpNearestNeighbour - Only single-channel images are supported");
        return nullptr;
    }

    // Matrix inversion optimization use optional invM parameter
    double local_inv_m[6];
    const double* use_inv_m = nullptr;
    if (inv_m != nullptr)
    {
        use_inv_m = inv_m;
    }
    else
    {
        if (!math_utils::invertAffine(M, local_inv_m))
        {
            common::logError("Image::affineWarpNearestNeighbour - Invalid affine matrix provided");
            return nullptr;
        }
        use_inv_m = local_inv_m;
    }

    const size_t out_size = out_width * out_height;
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
            const double src_x = (use_inv_m[0] * x) + (use_inv_m[1] * y) + use_inv_m[2];
            const double src_y = (use_inv_m[3] * x) + (use_inv_m[4] * y) + use_inv_m[5];
            unsigned char* pdst = dst + (y * out_width + x);

            // Nearest neighbor
            const int ix = static_cast<int>(std::round(src_x));
            const int iy = static_cast<int>(std::round(src_y));

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
        common::logError("Image::alphaBlend - Image sizes or pixel formats do not match");
        return;
    }
    unsigned char* dst_data = this->data();
    const unsigned char* src_data = src.data();
    const unsigned char* mask_data = mask.data();
    const int npixels = info.width * info.height;
    for (int i = 0; i < npixels; ++i)
    {
        // Blend each pixel using the mask
        pixel_operations::blendPixels(dst_data + (i * 3), src_data + (i * 3), 3, mask_data[i], 3, 255);
    }
}
