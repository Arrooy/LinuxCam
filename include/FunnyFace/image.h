#ifndef IMAGE_H
#define IMAGE_H

#include <turbojpeg.h>

#include <memory>
#include <mutex>

#include "FunnyFace/common.h"

namespace funnyface
{

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

    // Copy constructor - properly handle mutex
    Image(const Image& other) : info(other.info), size_(other.size_)
    {
        data_ = other.data_; // shared_ptr copy

        // Copy the beingUsed state with proper locking
        std::lock_guard<std::recursive_mutex> lock(other.beingUsedMutex_);
        beingUsed_ = other.beingUsed_;
    }

    // Move constructor
    Image(Image&& other) noexcept : info(other.info), size_(other.size_)
    {
        data_ = std::move(other.data_);

        // Move the beingUsed state with proper locking
        std::lock_guard<std::recursive_mutex> lock(other.beingUsedMutex_);
        beingUsed_ = other.beingUsed_;
        other.setBeingUsed(false);
        other.size_ = 0;
    }

    // Copy assignment operator - properly handle mutex
    Image& operator=(const Image& other)
    {
        if (this != &other)
        {
            // Copy basic data
            data_ = other.data_; // shared_ptr copy
            size_ = other.size_;
            info = other.info;

            // Copy beingUsed state with proper locking (lock both mutexes in consistent order)
            std::lock(beingUsedMutex_, other.beingUsedMutex_);
            std::lock_guard<std::recursive_mutex> lock1(beingUsedMutex_, std::adopt_lock);
            std::lock_guard<std::recursive_mutex> lock2(other.beingUsedMutex_, std::adopt_lock);

            beingUsed_ = other.beingUsed_;
        }
        return *this;
    }

    // Move assignment operator
    Image& operator=(Image&& other) noexcept
    {
        if (this != &other)
        {
            // Move basic data
            data_ = std::move(other.data_);
            size_ = other.size_;
            info = other.info;

            // Move beingUsed state with proper locking
            std::lock(beingUsedMutex_, other.beingUsedMutex_);
            std::lock_guard<std::recursive_mutex> lock1(beingUsedMutex_, std::adopt_lock);
            std::lock_guard<std::recursive_mutex> lock2(other.beingUsedMutex_, std::adopt_lock);

            beingUsed_ = other.beingUsed_;
            other.setBeingUsed(false);
            other.size_ = 0;
        }
        return *this;
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
            if (data_ && other.data() && size_ > 0)
            {
                std::memcpy(data_.get(), other.data(), size_);
            }
            size_ = other.size_;
            info = other.info;
            std::lock_guard<std::recursive_mutex> lock(beingUsedMutex_);
            beingUsed_ = other.beingUsed_;
        }
    }

    std::unique_ptr<Image> deepCopy()
    {
        auto copy = std::make_unique<Image>(size_);
        if (data_ && copy->data() && size_ > 0)
        {
            std::memcpy(copy->data(), data_.get(), size_);
        }
        copy->info = this->info;
        setBeingUsed(false);
        copy->setBeingUsed(true);
        return copy;
    }

    bool getBeingUsed() const
    {
        std::lock_guard<std::recursive_mutex> lock(beingUsedMutex_);
        return beingUsed_;
    }

    void setBeingUsed(bool val)
    {
        std::lock_guard<std::recursive_mutex> lock(beingUsedMutex_);
        beingUsed_ = val;
    }

    // Fast bilinear scaling - creates a new scaled image
    std::unique_ptr<Image> scale(unsigned long newWidth, unsigned long newHeight)
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

        // Ensure the scaled image is marked as being used
        scaledImage->setBeingUsed(true);

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
                        common::log_error("Image::scale - Source index out of bounds: idx1=%lu idx2=%lu idx3=%lu idx4=%lu size=%zu", 
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

  private:
    inline unsigned long index(unsigned long col, unsigned long row) const
    {
        return (row * info.width + col) * info.pixelSizeBytes;
    }

    mutable std::recursive_mutex beingUsedMutex_;
    bool beingUsed_{false};

    std::shared_ptr<unsigned char> data_;
    size_t size_;
};

} // namespace funnyface

#endif // IMAGE_H
