#pragma once

// Modern pixel processing architecture with clean separation of concerns
// This is the main entry point for all pixel processing operations

#include "pixel_format.h"
#include "pixel_converter.h"
#include "alpha_blender.h"
#include "image_processor.h"

namespace linuxface::pixel_conversion
{

// ============================================================================
// CLEAN ARCHITECTURE EXPORTS
// ============================================================================

// Re-export new architecture for convenient access
using PixelFormat = linuxface::image::PixelFormat;
using PixelFormatInfo = linuxface::image::PixelFormatInfo;
using PixelConverter = linuxface::image::PixelConverter;
using AlphaBlender = linuxface::image::AlphaBlender;
using ImageProcessor = linuxface::image::ImageProcessor;

// ============================================================================
// CONVENIENCE FUNCTIONS FOR COMMON OPERATIONS
// ============================================================================

/**
 * Convert single pixel between formats
 * Recommended for new code - clean and type-safe
 */
inline void convertPixel(const uint8_t* src, uint8_t* dst,
                        PixelFormat srcFormat, PixelFormat dstFormat) noexcept
{
    PixelConverter::convertPixel(src, dst, srcFormat, dstFormat);
}

/**
 * Convert single pixel with optional alpha blending
 * Most comprehensive pixel operation available
 */
inline void processPixel(const uint8_t* src, uint8_t* dst,
                        PixelFormat srcFormat, PixelFormat dstFormat,
                        bool enableBlending = false, uint8_t alpha = 255) noexcept
{
    ImageProcessor::processPixel(src, dst, srcFormat, dstFormat, enableBlending, alpha);
}

/**
 * Convert row of pixels for better performance
 */
inline void convertRow(const uint8_t* srcRow, uint8_t* dstRow, size_t width,
                      PixelFormat srcFormat, PixelFormat dstFormat) noexcept
{
    PixelConverter::convertRow(srcRow, dstRow, width, srcFormat, dstFormat);
}

/**
 * Process row of pixels with optional blending
 */
inline void processRow(const uint8_t* srcRow, uint8_t* dstRow, size_t width,
                      PixelFormat srcFormat, PixelFormat dstFormat,
                      bool enableBlending = false, uint8_t alpha = 255) noexcept
{
    ImageProcessor::processRow(srcRow, dstRow, width, srcFormat, dstFormat, enableBlending, alpha);
}

/**
 * Convert entire image
 */
inline void convertImage(const uint8_t* srcData, uint8_t* dstData,
                        size_t width, size_t height,
                        size_t srcStride, size_t dstStride,
                        PixelFormat srcFormat, PixelFormat dstFormat)
{
    ImageProcessor::convertImage(srcData, dstData, width, height,
                                srcStride, dstStride, srcFormat, dstFormat);
}

/**
 * Blend images with alpha
 */
inline void blendImage(const uint8_t* srcData, uint8_t* dstData,
                      size_t width, size_t height,
                      size_t srcStride, size_t dstStride,
                      PixelFormat srcFormat, PixelFormat dstFormat,
                      uint8_t alpha = 255)
{
    ImageProcessor::blendImage(srcData, dstData, width, height,
                              srcStride, dstStride, srcFormat, dstFormat, alpha);
}

/**
 * Calculate required buffer size
 */
inline size_t calculateBufferSize(size_t width, size_t height, PixelFormat format) noexcept
{
    return ImageProcessor::calculateBufferSize(width, height, format);
}

/**
 * Calculate optimal stride
 */
inline size_t calculateStride(size_t width, PixelFormat format, size_t alignment = 4) noexcept
{
    return ImageProcessor::calculateStride(width, format, alignment);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Convert RGB to grayscale using perceptual weighting
 */
inline constexpr uint8_t rgbToGrayscale(uint8_t r, uint8_t g, uint8_t b) noexcept
{
    return PixelConverter::rgbToGrayscale(r, g, b);
}

/**
 * Get bytes per pixel for given format
 */
inline constexpr uint8_t getBytesPerPixel(PixelFormat format) noexcept
{
    return PixelFormatInfo::getBytesPerPixel(format);
}

/**
 * Check if format has alpha channel
 */
inline constexpr bool hasAlpha(PixelFormat format) noexcept
{
    return PixelFormatInfo::hasAlpha(format);
}

/**
 * Check if format is color (not grayscale)
 */
inline constexpr bool isColor(PixelFormat format) noexcept
{
    return PixelFormatInfo::isColor(format);
}

/**
 * Check if two formats are compatible for direct conversion
 */
inline constexpr bool isCompatible(PixelFormat src, PixelFormat dst) noexcept
{
    return PixelFormatInfo::isCompatible(src, dst);
}

} // namespace linuxface::pixel_conversion
