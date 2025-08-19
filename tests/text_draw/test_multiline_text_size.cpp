#include <gtest/gtest.h>
#include "LinuxFace/Image/text_draw.h"

using namespace linuxface;

class MultilineTextSizeTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MultilineTextSizeTest, BasicMultilineSize)
{
    // Test that multiline text has correct dimensions
    std::string multiline = "Line1\nLine2\nLine3";
    TextSize multiSize = getMultilineTextSize(multiline, 1);
    
    // Expected: 3 lines of 5 characters each
    // Width should be 5 * 8 = 40 pixels
    // Height should be 3 * (8 + 2) - 2 = 28 pixels (3 lines with 2px spacing, minus last spacing)
    
    EXPECT_EQ(multiSize.width, 40);
    EXPECT_EQ(multiSize.height, 28);
}

TEST_F(MultilineTextSizeTest, CompareWithSingleLine)
{
    std::string multiline = "Line1\nLine2\nLine3";
    std::string singleLine = "Line1Line2Line3";
    
    TextSize multiSize = getMultilineTextSize(multiline, 1);
    TextSize singleSize = getTextSize(singleLine, 1);
    
    // Multiline should be taller but narrower
    EXPECT_GT(multiSize.height, singleSize.height);
    EXPECT_LT(multiSize.width, singleSize.width);
}

TEST_F(MultilineTextSizeTest, DifferentLineLengths)
{
    std::string text = "Short\nThis is a longer line\nMed";
    TextSize size = getMultilineTextSize(text, 1);
    
    // Width should be based on longest line (21 characters)
    EXPECT_EQ(size.width, 21 * 8);
    // Height should be for 3 lines
    EXPECT_EQ(size.height, 3 * (8 + 2) - 2); // 28 pixels
}

TEST_F(MultilineTextSizeTest, WithScaling)
{
    std::string text = "Line1\nLine2";
    TextSize size1 = getMultilineTextSize(text, 1);
    TextSize size2 = getMultilineTextSize(text, 2);
    
    // With scale 2, width should be doubled
    EXPECT_EQ(size2.width, size1.width * 2);
    
    // Height calculation: 2 lines with scale 1: 2*(8+2)-2 = 18
    // Height calculation: 2 lines with scale 2: 2*(16+2)-2 = 34
    // So it's NOT simply doubling because the spacing is added before scaling
    EXPECT_EQ(size1.height, 18); // 2*(8+2)-2 = 18
    EXPECT_EQ(size2.height, 34); // 2*(16+2)-2 = 34
}

TEST_F(MultilineTextSizeTest, EmptyString)
{
    TextSize size = getMultilineTextSize("", 1);
    EXPECT_EQ(size.width, 0);
    EXPECT_EQ(size.height, 0);
}

TEST_F(MultilineTextSizeTest, SingleLineViaMultiline)
{
    std::string text = "SingleLine";
    TextSize multiSize = getMultilineTextSize(text, 1);
    TextSize singleSize = getTextSize(text, 1);
    
    // Should be identical for single line
    EXPECT_EQ(multiSize.width, singleSize.width);
    EXPECT_EQ(multiSize.height, singleSize.height);
}

TEST_F(MultilineTextSizeTest, CustomLineSpacing)
{
    std::string text = "Line1\nLine2\nLine3";
    TextSize size2 = getMultilineTextSize(text, 1, 2); // default spacing
    TextSize size5 = getMultilineTextSize(text, 1, 5); // more spacing
    
    // Width should be same, height should be different
    EXPECT_EQ(size2.width, size5.width);
    EXPECT_LT(size2.height, size5.height);
    
    // Height difference should be (5-2) * 2 = 6 pixels (for 2 spacing differences)
    EXPECT_EQ(size5.height - size2.height, 6);
}
