/* -*- c++ -*- */

#ifndef MATH_UTILS_H
#define MATH_UTILS_H
#include <stdlib.h>

#include <cmath>
#include <vector>

namespace funnyface
{
namespace math_utils
{
struct Point
{
    Point(long x1, long y1) : x(x1), y(y1) {}
    long x;
    long y;
};

struct Rect
{
    Rect(long left, long top, long right, long bottom) : l(left), t(top), r(right), b(bottom) {}
    Rect(Point leftTopCorner, long w, long h)
        : l(leftTopCorner.x), t(leftTopCorner.y), r(leftTopCorner.x + w), b(leftTopCorner.y + h)
    {
    }
    Rect(Point leftTopCorner, Point rightBottomCorner)
        : l(leftTopCorner.x), t(leftTopCorner.y), r(rightBottomCorner.x), b(rightBottomCorner.y)
    {
    }

    long x() const { return l; }
    long y() const { return t; }
    unsigned long width() const { return r - l + 1; }
    unsigned long height() const { return b - t + 1; }

    bool contains(long x, long y) const
    {
        if (x < l || x > r || y < t || y > b)
        {
            return false;
        }
        return true;
    }

    bool contains(const Point& p) const { return contains(p.x, p.y); }

    long l;
    long t;
    long r;
    long b;
};

// function for line generation
template <typename T>
std::vector<Point> DDA(const T& x1, const T& y1, const T& x2, const T& y2)
{
    std::vector<Point> result;

    // calculate dx & dy
    T dx = x2 - x1;
    T dy = y2 - y1;

    // calculate steps required for generating pixels
    T steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    // calculate increment in x & y for each steps
    float xInc = static_cast<float>(dx) / static_cast<float>(steps);
    float yInc = static_cast<float>(dy) / static_cast<float>(steps);

    // Put pixel for each step
    float x = static_cast<float>(x1);
    float y = static_cast<float>(y1);
    for (int i = 0; i <= steps; i++)
    {
        const Point p = Point(round(x), round(y));
        result.emplace_back(p);
        x += xInc; // increment in x at each step
        y += yInc; // increment in y at each step
    }

    return result;
}

inline double distance(int a, int b, int c, int d)
{
    return sqrt(pow(c - a, 2) + pow(d - b, 2));
}

} // namespace math_utils
} // namespace funnyface
#endif // MATH_UTILS_H
