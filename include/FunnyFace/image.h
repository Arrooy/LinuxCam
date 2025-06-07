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
    Image(unsigned char* buffer, size_t size) : size_(size) { Image(buffer, size, true); }

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
