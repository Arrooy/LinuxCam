#ifndef TENSOR_PADDING_H
#define TENSOR_PADDING_H

namespace linuxface {

// PaddingType enum moved from image.h for better code organization
enum class PaddingType {
    NO_PADDING,   // No padding
    ZERO,         // Fill with zeros
    CONSTANT,     // Fill with single constant value
    RGB_CONSTANT, // Fill with RGB values (for color images)
};

// TensorPadding struct moved from image.h for better code organization
struct TensorPadding {
    PaddingType type;
    union {
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
    static TensorPadding noPadding() {
        TensorPadding p;
        p.type = PaddingType::NO_PADDING;
        return p;
    }
    static TensorPadding zero() {
        TensorPadding p;
        p.type = PaddingType::ZERO;
        p.constant_value = 0.0f;
        return p;
    }
    static TensorPadding constant(float value) {
        TensorPadding p;
        p.type = PaddingType::CONSTANT;
        p.constant_value = value;
        return p;
    }
    static TensorPadding rgb(float r, float g, float b) {
        TensorPadding p;
        p.type = PaddingType::RGB_CONSTANT;
        p.rgb_values[0] = r;
        p.rgb_values[1] = g;
        p.rgb_values[2] = b;
        return p;
    }
    static TensorPadding metric3d() {
        // Metric3D specific padding values [123.675, 116.28, 103.53] normalized
        // return rgb(123.675f / 255.0f, 116.28f / 255.0f, 103.53f / 255.0f);
        return rgb(123.675f, 116.28f, 103.53f);
    }
    static TensorPadding fsanet() { return constant(0.3f); }
    static TensorPadding scrfd() { return zero(); }
    void resetTransform() const {
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
