#ifndef TENSOR_PADDING_H
#define TENSOR_PADDING_H

#include <cstdint>

namespace linuxface
{

// PaddingType enum moved from image.h for better code organization
enum class PaddingType : std::uint8_t
{
    NO_PADDING,   // No padding
    ZERO,         // Fill with zeros
    CONSTANT,     // Fill with single constant value
    RGB_CONSTANT, // Fill with RGB values (for color images)
};

// TensorPadding struct moved from image.h for better code organization
struct TensorPadding
{
    PaddingType type{PaddingType::NO_PADDING};
    union
    {
        float constant_value{};
        float rgb_values[3];
    };

    // Transform metadata for reversing tensor operations
    mutable int tensor_width = 0;
    mutable int tensor_height = 0;
    mutable int resized_width = 0;
    mutable int resized_height = 0;
    mutable int offset_x = 0;
    mutable int offset_y = 0;
    mutable float scale_ratio = 1.0f;
    mutable bool has_transform = false;

    // Constructors for different padding types
    static TensorPadding noPadding()
    {
        TensorPadding p;
        p.type = PaddingType::NO_PADDING;
        return p;
    }
    static TensorPadding zero()
    {
        TensorPadding p;
        p.type = PaddingType::ZERO;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        p.constant_value = 0.0f;
        return p;
    }
    static TensorPadding constant(float value)
    {
        TensorPadding p;
        p.type = PaddingType::CONSTANT;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        p.constant_value = value;
        return p;
    }
    struct RgbValues
    {
        float r;
        float g;
        float b;
    };

    static TensorPadding rgb(const RgbValues& rgbVals)
    {
        TensorPadding p;
        p.type = PaddingType::RGB_CONSTANT;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        p.rgb_values[0] = rgbVals.r;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        p.rgb_values[1] = rgbVals.g;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        p.rgb_values[2] = rgbVals.b;
        return p;
    }
    static TensorPadding metric3d()
    {
        // Metric3D specific padding values [123.675, 116.28, 103.53] normalized
        // return rgb({123.675f / 255.0f, 116.28f / 255.0f, 103.53f / 255.0f});
        return rgb({123.675f, 116.28f, 103.53f});
    }
    static TensorPadding fsanet() { return constant(0.3f); }
    static TensorPadding scrfd() { return zero(); }
    void resetTransform() const
    {
        tensor_width = 0;
        tensor_height = 0;
        resized_width = 0;
        resized_height = 0;
        offset_x = 0;
        offset_y = 0;
        scale_ratio = 1.0f;
        has_transform = false;
    }
};

} // namespace linuxface

#endif // TENSOR_PADDING_H
