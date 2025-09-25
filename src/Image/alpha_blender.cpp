#include "LinuxFace/Image/alpha_blender.h"

#include <algorithm>
#include <cmath>
#include <cstring> // For std::memcpy

namespace linuxface::image
{

void AlphaBlender::blendRGB(const uint8_t* src, uint8_t* dst, uint8_t alpha) noexcept
{
    if (alpha == 0)
    {
        return; // No change
    }
    if (alpha == 255) // Full opacity - direct copy
    {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        return;
    }

    const uint32_t invAlpha = 255U - alpha;
    dst[0] = static_cast<uint8_t>(
        (static_cast<uint32_t>(src[0]) * alpha + static_cast<uint32_t>(dst[0]) * invAlpha + 127U) / 255U);
    dst[1] = static_cast<uint8_t>(
        (static_cast<uint32_t>(src[1]) * alpha + static_cast<uint32_t>(dst[1]) * invAlpha + 127U) / 255U);
    dst[2] = static_cast<uint8_t>(
        (static_cast<uint32_t>(src[2]) * alpha + static_cast<uint32_t>(dst[2]) * invAlpha + 127U) / 255U);
}

void AlphaBlender::blendRGBA(const uint8_t* src, uint8_t* dst) noexcept
{
    const uint8_t srcAlpha = src[3];

    if (srcAlpha == 0)
    {
        return; // Source is transparent
    }
    if (srcAlpha == 255)
    {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        return;
    }

    const uint8_t dstAlpha = dst[3];
    if (dstAlpha == 0)
    {
        // Destination fully transparent, result is just source
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = srcAlpha;
        return;
    }

    const uint32_t invSrcAlpha = 255U - srcAlpha;
    const uint32_t outAlpha = static_cast<uint32_t>(srcAlpha)
                              + static_cast<uint32_t>((static_cast<uint32_t>(dstAlpha) * invSrcAlpha + 127U) / 255U);

    if (outAlpha == 0)
    {
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 0;
        return;
    }

    const uint32_t srcPremulR = static_cast<uint32_t>(src[0]) * srcAlpha;
    const uint32_t srcPremulG = static_cast<uint32_t>(src[1]) * srcAlpha;
    const uint32_t srcPremulB = static_cast<uint32_t>(src[2]) * srcAlpha;

    const uint32_t dstPremulR = static_cast<uint32_t>(dst[0]) * dstAlpha;
    const uint32_t dstPremulG = static_cast<uint32_t>(dst[1]) * dstAlpha;
    const uint32_t dstPremulB = static_cast<uint32_t>(dst[2]) * dstAlpha;

    const uint32_t dstScale = invSrcAlpha;

    const uint32_t outPremulR = srcPremulR + (dstPremulR * dstScale + 127U) / 255U;
    const uint32_t outPremulG = srcPremulG + (dstPremulG * dstScale + 127U) / 255U;
    const uint32_t outPremulB = srcPremulB + (dstPremulB * dstScale + 127U) / 255U;

    dst[0] = static_cast<uint8_t>((outPremulR + outAlpha / 2U) / outAlpha);
    dst[1] = static_cast<uint8_t>((outPremulG + outAlpha / 2U) / outAlpha);
    dst[2] = static_cast<uint8_t>((outPremulB + outAlpha / 2U) / outAlpha);
    dst[3] = static_cast<uint8_t>(std::min(outAlpha, 255U));
}

void AlphaBlender::blendRGBAToRGB(const uint8_t* src, uint8_t* dst) noexcept
{
    blendRGB(src, dst, src[3]);
}

void AlphaBlender::blendRGBToRGBA(const uint8_t* src, uint8_t* dst, uint8_t alpha) noexcept
{
    if (alpha == 0)
    {
        return;
    }

    // Blend color channels only, preserve destination alpha
    const uint8_t originalAlpha = dst[3];
    blendRGB(src, dst, alpha);
    dst[3] = originalAlpha;
}

void AlphaBlender::blendRow(const uint8_t* srcRow, uint8_t* dstRow, size_t pixelCount, PixelFormat srcFormat,
                            PixelFormat dstFormat, uint8_t globalAlpha) noexcept
{
    const uint8_t srcBytes = PixelFormatInfo::getBytesPerPixel(srcFormat);
    const uint8_t dstBytes = PixelFormatInfo::getBytesPerPixel(dstFormat);

    // Fast path for identical formats with full opacity
    if (srcFormat == dstFormat && globalAlpha == 255)
    {
        const size_t totalBytes = pixelCount * srcBytes;
        std::memcpy(dstRow, srcRow, totalBytes);
        return;
    }

    // Fast path for RGB to RGB blending with consistent alpha
    if (srcFormat == PixelFormat::RGB && dstFormat == PixelFormat::RGB && globalAlpha != 0 && globalAlpha != 255)
    {
        const float srcAlpha = globalAlpha / 255.0f;
        const float dstAlpha = 1.0f - srcAlpha;

        // Process multiple pixels at once when possible
        size_t i = 0;
        const size_t alignedCount = pixelCount - (pixelCount % 4); // Process 4 pixels at a time

        for (; i < alignedCount; i += 4)
        {
            // Unroll loop for better performance
            for (int j = 0; j < 4; ++j)
            {
                const size_t idx = (i + j) * 3;
                dstRow[idx] = static_cast<uint8_t>(std::round(srcRow[idx] * srcAlpha + dstRow[idx] * dstAlpha));
                dstRow[idx + 1] =
                    static_cast<uint8_t>(std::round(srcRow[idx + 1] * srcAlpha + dstRow[idx + 1] * dstAlpha));
                dstRow[idx + 2] =
                    static_cast<uint8_t>(std::round(srcRow[idx + 2] * srcAlpha + dstRow[idx + 2] * dstAlpha));
            }
        }

        // Handle remaining pixels
        for (; i < pixelCount; ++i)
        {
            blendRGB(srcRow + i * 3, dstRow + i * 3, globalAlpha);
        }
        return;
    }

    // General case: process pixel by pixel
    for (size_t i = 0; i < pixelCount; ++i)
    {
        blendPixel(srcRow + i * srcBytes, dstRow + i * dstBytes, srcFormat, dstFormat, globalAlpha);
    }
}

void AlphaBlender::blendPixel(const uint8_t* src, uint8_t* dst, PixelFormat srcFormat, PixelFormat dstFormat,
                              uint8_t alpha) noexcept
{
    // Direct format-specific blending
    if (srcFormat == PixelFormat::RGBA && dstFormat == PixelFormat::RGBA)
    {
        blendRGBA(src, dst);
    }
    else if (srcFormat == PixelFormat::RGBA && dstFormat == PixelFormat::RGB)
    {
        blendRGBAToRGB(src, dst);
    }
    else if (srcFormat == PixelFormat::RGB && dstFormat == PixelFormat::RGBA)
    {
        blendRGBToRGBA(src, dst, alpha);
    }
    else if (srcFormat == PixelFormat::RGB && dstFormat == PixelFormat::RGB)
    {
        blendRGB(src, dst, alpha);
    }
    // Note: Grayscale blending could be added here if needed
}

} // namespace linuxface::image
