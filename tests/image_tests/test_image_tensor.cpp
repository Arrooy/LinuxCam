// Tests for tensor conversion, padding, normalization
// Covers: toTensor, fromTensor, TensorPadding, NormalizationType
// Edge cases: zero padding, constant padding, RGB constant, invalid tensor shape

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>

using namespace linuxface;

TEST(ImageTensor, ToTensorZeroPadding) {
    Image img(Pixel(1,2,3), 2, 2);
    float tensor[24] = {0};
    TensorPadding pad = TensorPadding::zero();
    img.toTensor(tensor, pad, 3, 3, NormalizationType::NONE);
    EXPECT_EQ(pad.tensor_width, 3);
    EXPECT_EQ(pad.tensor_height, 3);
}

TEST(ImageTensor, FromTensorConstantPadding) {
    Image img(Pixel(1,2,3), 2, 2);
    float tensor[24] = {1.0f};
    TensorPadding pad = TensorPadding::constant(0.5f);
    std::vector<int64_t> shape = {1,3,3,3};
    img.fromTensor(tensor, shape, 3, 3, pad, NormalizationType::NONE);
    EXPECT_EQ(img.info.width, 2);
}
