/* -*- c++ -*- */

#ifndef IMAGE_H
#define IMAGE_H

#include <turbojpeg.h>

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
    int width;
    int height;
    TJSAMP TJSampleFormat; // TJSAMP_444
    TJCS TJColorSpace;     // TJCS_RGB
    TJPF TJPixelFormat;    // TJPF_RGB
    int pixelSizeBytes;
};

struct Image
{
    void px(unsigned long x, unsigned long y, const Pixel& c) { this->px(x, y, c.r, c.g, c.b, c.a); }
    void px(unsigned long x, unsigned long y, const unsigned char r, const unsigned char g, const unsigned char b,
            const unsigned char a = DEFAULT_ALPHA)
    {
        long index = (x + y * info.width) * info.pixelSizeBytes;
        // TODO: Byte order depends on pixelFormat. Forced to RGBA for now
        data[index] = r;
        data[index + 1] = g;
        data[index + 2] = b;
        if (info.pixelSizeBytes == 4)
        {
            data[index + 3] = a;
        }
    }

    Pixel operator()(unsigned long x, unsigned long y) const
    {
        // TODO: Byte order depends on pixelFormat. Forced to RGBA for now
        long index = (x + y * info.width) * info.pixelSizeBytes;
        return Pixel(data[index], data[index + 1], data[index + 2], data[index + 3]);
    }

    unsigned char* data;
    unsigned long size;
    TJImageDescription info;
};
} // namespace funnyface


#endif // IMAGE_H
