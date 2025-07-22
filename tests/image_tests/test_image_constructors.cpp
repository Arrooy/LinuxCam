// Tests for Image constructors, memory management, and move semantics
// Covers: Image(size), Image(buffer, size), Image(buffer, size, takeOwnership), Image(Pixel, width, height), move constructor/assignment
// Edge cases: zero size, null buffer, ownership flag, move from empty, move self

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>

using namespace linuxface;

TEST(ImageConstructor, DefaultAndSize) {
    Image img;
    EXPECT_TRUE(img.empty());
    Image img2(0);
    EXPECT_TRUE(img2.empty());
    Image img3(10);
    EXPECT_EQ(img3.size(), 10);
    EXPECT_NE(img3.data(), nullptr);
}

TEST(ImageConstructor, BufferOwnership) {
    // Use heap allocation for both cases to avoid stack buffer issues
    unsigned char* heap_buf1 = new unsigned char[6]{1,2,3,4,5,6};
    Image img(heap_buf1, 6, true);
    EXPECT_EQ(img.size(), 6);
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(img.data()[i], heap_buf1[i]);
    }
    unsigned char* heap_buf2 = new unsigned char[6]{7,8,9,10,11,12};
    Image img2(heap_buf2, 6);
    EXPECT_EQ(img2.size(), 6);
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(img2.data()[i], heap_buf2[i]);
    }
    // No manual delete needed, Images will free heap_buf1 and heap_buf2 automatically
}

TEST(ImageConstructor, ColorConstructor) {
    Pixel p(10,20,30,40);
    Image img(p, 2, 2);
    EXPECT_EQ(img.size(), 12);
    for (size_t i = 0; i < 12; i+=3) {
        EXPECT_EQ(img.data()[i], 10);
        EXPECT_EQ(img.data()[i+1], 20);
        EXPECT_EQ(img.data()[i+2], 30);
    }
}

TEST(ImageConstructor, MoveSemantics) {
    Image img(5);
    unsigned char* ptr = img.data();
    Image img2(std::move(img));
    EXPECT_EQ(img2.size(), 5);
    EXPECT_EQ(img.size(), 0);
    EXPECT_EQ(img2.data(), ptr);
    Image img3(7);
    img2 = std::move(img3);
    EXPECT_EQ(img2.size(), 7);
    EXPECT_EQ(img3.size(), 0);
}

TEST(ImageConstructor, ZeroSizeOwnership) {
    unsigned char* buf = nullptr;
    Image img(buf, 0);
    EXPECT_TRUE(img.empty());
    Image img2(buf, 0, true);
    EXPECT_TRUE(img2.empty());
}

TEST(ImageConstructor, NullBufferOwnership) {
    unsigned char* buf = nullptr;
    Image img(buf, 10);
    EXPECT_TRUE(img.empty());
    Image img2(buf, 10, true);
    EXPECT_TRUE(img2.empty());
}

TEST(ImageConstructor, StressManyImages) {
    std::vector<Image> images;
    for (int i = 0; i < 1000; ++i) {
        images.emplace_back(Image(Pixel(i%256, (i*2)%256, (i*3)%256), 2, 2));
        EXPECT_EQ(images.back().size(), 12);
    }
}

TEST(ImageConstructor, MoveFromEmpty) {
    Image img;
    Image img2(std::move(img));
    EXPECT_TRUE(img2.empty());
    EXPECT_TRUE(img.empty());
}

TEST(ImageConstructor, MoveSelfAssignment) {
    Image img(10);
    img = std::move(img);
    EXPECT_EQ(img.size(), 0);
    EXPECT_EQ(img.data(), nullptr);
}

TEST(ImageConstructor, DestructorStress) {
    for (int i = 0; i < 1000; ++i) {
        Image* img = new Image(Pixel(1,2,3), 2, 2);
        delete img;
    }
}
