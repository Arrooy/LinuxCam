#ifndef MATH_UTILS_H
#define MATH_UTILS_H
#include <stdlib.h>

#include <cmath>
#include <vector>

namespace funnyface
{
namespace math_utils
{

struct Anchor
{
    float cx;
    float cy;
    int stride;
};
struct Point
{
    Point(long x1, long y1) : x(x1), y(y1) {}
    Point() = default;
    long x;
    long y;
};

struct StridePoint
{
    StridePoint(float x1, float y1, float s) : cx(x1), cy(y1), stride(s) {}
    float cx;
    float cy;
    float stride;
};

template <typename T = long>
struct Rect
{   
    Rect() = default;
    Rect(T left, T top, T right, T bottom) : l(left), t(top), r(right), b(bottom) {}

    Rect(Point leftTopCorner, T w, T h)
        : l(static_cast<T>(leftTopCorner.x)),
          t(static_cast<T>(leftTopCorner.y)),
          r(static_cast<T>(leftTopCorner.x + w)),
          b(static_cast<T>(leftTopCorner.y + h))
    {
    }

    Rect(Point leftTopCorner, Point rightBottomCorner)
        : l(static_cast<T>(leftTopCorner.x)),
          t(static_cast<T>(leftTopCorner.y)),
          r(static_cast<T>(rightBottomCorner.x)),
          b(static_cast<T>(rightBottomCorner.y))
    {
    }

    T x() const { return l; }
    T y() const { return t; }

    // Assuming r >= l and b >= t
    T width() const { return r - l + 1; }
    T height() const { return b - t + 1; }

    bool contains(T x, T y) const { return !(x < l || x > r || y < t || y > b); }

    bool contains(const Point& p) const { return contains(static_cast<T>(p.x), static_cast<T>(p.y)); }

    T l;
    T t;
    T r;
    T b;
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

    // common::log_info("DDA line: (%d, %d) -> (%d, %d)", x1, y1, x2, y2);
    // common::log_info("Steps and increment: %d, XInc %f, YInc%f", steps, xInc, yInc);

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
