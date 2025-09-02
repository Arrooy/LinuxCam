#pragma once

#include "pixel_format.h"

#include <cstddef>
#include <cstdint>

namespace linuxface::image
{

/**
 * Alpha blending operations with proper mathematical compositing
 */
class AlphaBlender
{
  public:
    /**
     * Blend two RGB pixels using alpha value (0-255)
     * Uses standard over operator: out = src_over_dst = src + dst * (1 - src_alpha)
     */
    static void blendRGB(const uint8_t* src, uint8_t* dst, uint8_t alpha) noexcept;

    /**
     * Blend two RGBA pixels using premultiplied alpha compositing
     * Handles alpha channels correctly for proper compositing
     */
    static void blendRGBA(const uint8_t* src, uint8_t* dst) noexcept;

    /**
     * Blend RGBA source onto RGB destination
     * Extracts alpha from source and applies to RGB target
     */
    static void blendRGBAToRGB(const uint8_t* src, uint8_t* dst) noexcept;

    /**
     * Blend RGB source onto RGBA destination with specified alpha
     * Maintains destination alpha channel integrity
     */
    static void blendRGBToRGBA(const uint8_t* src, uint8_t* dst, uint8_t alpha) noexcept;

    /**
     * High-performance row blending for batch operations
     */
    static void blendRow(const uint8_t* srcRow, uint8_t* dstRow, size_t pixelCount, PixelFormat srcFormat,
                         PixelFormat dstFormat, uint8_t globalAlpha = 255) noexcept;

    /**
     * Vectorized row blending for maximum performance (when available)
     * Falls back to standard implementation if SIMD not available
     */

    /**
     * Blend single pixel with format awareness
     */
    static void blendPixel(const uint8_t* src, uint8_t* dst, PixelFormat srcFormat, PixelFormat dstFormat,
                           uint8_t alpha = 255) noexcept;
};

} // namespace linuxface::image
