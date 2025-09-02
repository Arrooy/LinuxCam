#pragma once

#include "alpha_blender.h"
#include "pixel_converter.h"
#include "pixel_format.h"

#include <cstddef>
#include <cstdint>

namespace linuxface::image
{

/**
 * High-level image processing operations coordinator
 * Single responsibility: orchestrate pixel operations using composition
 */
class ImageProcessor
{
  public:
    /**
     * Process pixels with conversion and optional blending
     * This replaces the complex convertPixel function with clean composition
     */
    static void processPixel(const uint8_t* src, uint8_t* dst, PixelFormat srcFormat, PixelFormat dstFormat,
                             bool enableBlending = false, uint8_t alpha = 255) noexcept;

    /**
     * Process entire image row for performance
     */
    static void processRow(const uint8_t* srcRow, uint8_t* dstRow, size_t pixelCount, PixelFormat srcFormat,
                           PixelFormat dstFormat, bool enableBlending = false, uint8_t alpha = 255) noexcept;

    /**
     * Process entire image with stride support
     */
    static void processImage(const uint8_t* srcData, uint8_t* dstData, size_t width, size_t height, size_t srcStride,
                             size_t dstStride, PixelFormat srcFormat, PixelFormat dstFormat,
                             bool enableBlending = false, uint8_t alpha = 255);

    /**
     * Convert image format without blending
     */
    static void convertImage(const uint8_t* srcData, uint8_t* dstData, size_t width, size_t height, size_t srcStride,
                             size_t dstStride, PixelFormat srcFormat, PixelFormat dstFormat);

    /**
     * Blend image with alpha
     */
    static void blendImage(const uint8_t* srcData, uint8_t* dstData, size_t width, size_t height, size_t srcStride,
                           size_t dstStride, PixelFormat srcFormat, PixelFormat dstFormat, uint8_t alpha = 255);

    /**
     * Calculate required buffer size for image conversion
     */
    static size_t calculateBufferSize(size_t width, size_t height, PixelFormat format) noexcept;

    /**
     * Calculate optimal stride for given width and format
     */
    static size_t calculateStride(size_t width, PixelFormat format, size_t alignment = 4) noexcept;

  private:
    /**
     * Check if blending is supported for given format combination
     */
    static constexpr bool canBlend(PixelFormat srcFormat, PixelFormat dstFormat) noexcept
    {
        // Only RGB and RGBA formats support blending
        return (srcFormat == PixelFormat::RGB || srcFormat == PixelFormat::RGBA)
               && (dstFormat == PixelFormat::RGB || dstFormat == PixelFormat::RGBA);
    }
};

} // namespace linuxface::image
