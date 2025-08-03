#ifndef IMAGE_H
#define IMAGE_H

#include <math.h>
#include <memory>
#include <mutex>
#include <onnxruntime_cxx_api.h>
#include <turbojpeg.h>


#include "LinuxFace/common.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/Image/tensor_padding.h"

namespace linuxface
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
    RGB,         // RGB format
    RGBA,        // RGBA format
    GRAYSCALE,   // Single channel grayscale
    DEPTH_FLOAT, // Floating point depth data
    PNG,
    BMP, // Bitmap format
    PPM, // Portable Pixmap
};

enum class ImageLayout
{
    HWC, // Height-Width-Channel
    CHW  // Channel-Height-Width
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
        case ImageFormat::RGB:
            return "RGB";
        case ImageFormat::RGBA:
            return "RGBA";
        case ImageFormat::GRAYSCALE:
            return "GRAYSCALE";
        case ImageFormat::DEPTH_FLOAT:
            return "DEPTH_FLOAT";
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

struct ImageMetadata
{
    unsigned long x{0u};
    unsigned long y{0u};
    unsigned long width{0u};
    unsigned long height{0u};
    unsigned char pixelSizeBytes{0u};
    TJSAMP TJSampleFormat;                // TJSAMP_444
    TJCS TJColorSpace;                    // TJCS_RGB
    TJPF TJPixelFormat;                   // TJPF_RGB
    ImageFormat format{ImageFormat::RGB}; // Default to RGB
    bool is_valid{false};
    std::string filename;
    unsigned int textureId{0};
    int layer{0}; // Layer for rendering, default is 0
};


enum class NormalizationType
{
    NONE,
    MINMAX,
    ZERO_CENTER
};

enum class ScalingAlgorithm
{
    //              Quality Speed Anti-aliasing / Downscale Sharpness Consistency / Artifact-free Overall Score
    LANCZOS,        // 9	3	9	8	7.25
    BILINEAR,       // 5	8	5	6	6.0
    AREA_AVERAGING, // 7	7	8	8	7.5
    FAST_BOX,       // 2	10	2	4	4.5
    BICUBIC,        // 8	6	7	7	7.0
};



// Separate pixel operations for better performance
namespace PixelOperations {
    // Fast pixel access without bounds checking for performance-critical loops
    inline void setPixelRGB(unsigned char* data, size_t idx, unsigned char r, unsigned char g, unsigned char b) noexcept {
        data[idx] = r;
        data[idx + 1] = g;
        data[idx + 2] = b;
    }

    inline void setPixelRGBA(unsigned char* data, size_t idx, unsigned char r, unsigned char g, unsigned char b, unsigned char a) noexcept {
        data[idx] = r;
        data[idx + 1] = g;
        data[idx + 2] = b;
        data[idx + 3] = a;
    }

    // Alpha blending optimized for different pixel formats
    void blendPixels(unsigned char* dst, const unsigned char* src, unsigned char srcPixelSize,
                    unsigned char srcAlpha, unsigned char dstPixelSize, unsigned char dstAlpha = 255) noexcept;
}

// Image class with proper resource management
class Image
{
  public:
    // Default constructor
    Image() { info.format = ImageFormat::UNKNOWN; }

    // Constructor with size allocation
    explicit Image(size_t size);

    // Constructor that adopts existing buffer without copying
    Image(unsigned char* buffer, size_t size);

    // Constructor for non-owning reference (for V4L2 buffers)
    Image(unsigned char* buffer, size_t size, bool takeOwnership);

    // Plain color constructor.
    Image(Pixel color, size_t width, size_t height);

    // Move constructor and assignment
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    // Delete copy constructor to prevent expensive copies
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    ~Image() = default;

    // Use explicit copy method instead
    void copyFrom(const Image& other);

    // Fast resize with optional data preservation
    void resize(size_t newSize, bool preserveData = true);

    // Const-correct accessors
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] unsigned char* data() noexcept { return data_.get(); }
    [[nodiscard]] unsigned char* data() const noexcept { return data_.get(); }
    [[nodiscard]] bool empty() const noexcept { return !data_ || size_ == 0; }

    // Fast pixel access (unsafe but fast for performance-critical code)
    [[nodiscard]] inline size_t pixelIndex(size_t x, size_t y) const noexcept
    {
        return (y * info.width + x) * info.pixelSizeBytes;
    }

    // Safe pixel access with bounds checking
    [[nodiscard]] Pixel getPixel(size_t x, size_t y) const;
    void setPixel(size_t x, size_t y, const Pixel& pixel);
    void
    setPixel(size_t x, size_t y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = DEFAULT_ALPHA);

    // Legacy pixel access methods (for backward compatibility)
    [[nodiscard]] Pixel operator()(size_t x, size_t y) const;
    void ppx(size_t x, size_t y, const Pixel& pixel);
    void pxy(size_t x, size_t y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = DEFAULT_ALPHA);
    void pidx(size_t idx, unsigned char r, unsigned char g, unsigned char b, unsigned char a = DEFAULT_ALPHA);
    [[nodiscard]] size_t index(size_t x, size_t y) const noexcept;

    // Helper method to determine if image is RGB/RGBA based on format
    [[nodiscard]] bool isColorImage() const noexcept;
    [[nodiscard]] unsigned char getExpectedPixelSize() const noexcept;
    [[nodiscard]] bool hasSameDimensions(const Image& other) const noexcept;
    [[nodiscard]] bool hasSameSize(const Image& other) const noexcept;
    [[nodiscard]] bool isCompatible(const Image& other) const noexcept;

    // Position and movement
    void move(size_t x, size_t y);
    [[nodiscard]] std::pair<unsigned long, unsigned long> getPosition() const noexcept { return {info.x, info.y}; }

    // Better scaling interface
    [[nodiscard]] std::unique_ptr<Image>
    scale(double factor, ScalingAlgorithm algorithm = ScalingAlgorithm::FAST_BOX) const;
    [[nodiscard]] std::unique_ptr<Image> scale(unsigned long newWidth, unsigned long newHeight,
                                               ScalingAlgorithm algorithm = ScalingAlgorithm::FAST_BOX) const;
    [[nodiscard]] std::unique_ptr<Image>
    scaleTo(size_t newWidth, size_t newHeight, ScalingAlgorithm algorithm = ScalingAlgorithm::FAST_BOX) const;

    // In-place scaling methods
    void scaleInPlace(double factor, ScalingAlgorithm algorithm = ScalingAlgorithm::FAST_BOX);
    void scaleInPlace(unsigned long newWidth, unsigned long newHeight,
                      ScalingAlgorithm algorithm = ScalingAlgorithm::FAST_BOX);
    void scaleToInPlace(size_t newWidth, size_t newHeight, ScalingAlgorithm algorithm = ScalingAlgorithm::FAST_BOX);
    [[nodiscard]] std::unique_ptr<Image> deepCopy() const;

    // Improved paste operations
    Image& paste(const Image& other, bool expandCanvas = false);
    Image& pasteAt(const Image& other, long x, long y, bool expandCanvas = false);

    // Tensor operations
    void toTensor(float* outputData, TensorPadding& padding, int new_width, int new_height,
                  NormalizationType normType) const;

    void fromTensor(const float* tensorData, std::vector<int64_t> tensorShape, int tensor_width, int tensor_height,
                    const TensorPadding& padding, NormalizationType normType);

    // Image operations
    [[nodiscard]] std::unique_ptr<Image> crop(const math_utils::Rect<float>& rect) const;
    bool saveToDisk(const std::string& dest_path) const;

    // Image manipulation methods
    void toGrayscale();
    void flipHorizontal();
    void flipVertical();

    math_utils::Point<double> rotate(double angleRad, math_utils::Point<double> center);
    void rotate90();
    void rotate180();
    void rotate270();
    void changeBackgroundImage(const Image& matting, const Image& background);
    void paintPoints(const std::vector<math_utils::Point<>>& points, const Pixel& color);

    // In-place transformations (for better performance when creating new images isn't needed)
    void flipHorizontalInPlace();
    void flipVerticalInPlace();
    void rotateInPlace(float angleDegrees);


    void setTextureId(unsigned int textureId) { info.textureId = textureId; }
    [[nodiscard]] unsigned int getTextureId() const { return info.textureId; }

    ImageMetadata info{};

    // Affine warp: apply 2x3 matrix (row-major) to image, output size w x h
    // Affine warp: apply 2x3 matrix (row-major) to image, output size w x h
    // If invM is provided, it is used directly; otherwise, the inverse is computed from M
    std::unique_ptr<Image> affineWarpBilinear(const double* M, int out_width, int out_height, const double* invM = nullptr) const;

    // Affine warp for single-channel mask
    std::unique_ptr<Image> affineWarpNearestNeighbour(const double* M, int out_width, int out_height, const double* invM = nullptr) const;

    // Alpha blend src onto this image using mask (mask: 0=background, 255=full src)
    void alphaBlend(const Image& src, const Image& mask);

    // Converts a single-channel image to RGB vector
    std::vector<unsigned char> convertToRGB() const;

    // Converts a single-channel image to RGB in-place
    bool convertToRGBInplace();

    // Converts RGB to RGBA in-place
    bool convertToRGBAInplace();

    void drawBorder(const Pixel& color, int thickness = 1);

    // Set all pixels to black (useful for clearing the image)
    void black();
    bool isFullyOpaque() const;
  private:
    // Optimized helper methods
    void copyPixelsOptimized(const Image& src, long srcX, long srcY, long dstX, long dstY, size_t copyWidth,
                             size_t copyHeight);
    void copyPixelsWithBlending(const Image& src, long srcGlobalX, long srcGlobalY, long canvasX, long canvasY,
                                size_t canvasWidth, size_t canvasHeight);
    Image& pasteImpl(const Image& other, long x, long y, bool expandCanvas);

    void scaleImageBuffer(const unsigned char* srcData, unsigned long srcWidth, unsigned long srcHeight,
                          unsigned char pixelSize, unsigned char* dstData, unsigned long dstWidth,
                          unsigned long dstHeight, ScalingAlgorithm algorithm) const;

    // Internal helper for affine warp (supports RGB and single-channel mask)
    std::unique_ptr<Image>
    affineWarpGeneric(const double* M, int out_width, int out_height, int channels, bool bilinear) const;

    std::shared_ptr<unsigned char> data_;
    size_t size_{0};
};

} // namespace linuxface

#endif // IMAGE_H
