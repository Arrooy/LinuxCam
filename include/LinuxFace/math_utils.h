#ifndef MATH_UTILS_H
#define MATH_UTILS_H
#include <stdlib.h>

#include <cmath>
#include <vector>

namespace linuxface
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

    inline T x() const { return l; }
    inline T y() const { return t; }

    // Assuming r >= l and b >= t
    inline T width() const { return r - l + 1; }
    inline T height() const { return b - t + 1; }

    bool contains(T x, T y) const { return !(x < l || x > r || y < t || y > b); }

    bool contains(const Point& p) const { return contains(static_cast<T>(p.x), static_cast<T>(p.y)); }

    // Check if this rectangle is within bounds of maximum dimensions by given factor
    bool isWithinBounds(T maxWidth, T maxHeight, float scaleFactor = 1.2f) const
    {
        const T allowedMaxWidth = static_cast<T>(maxWidth * scaleFactor);
        const T allowedMaxHeight = static_cast<T>(maxHeight * scaleFactor);

        return (width() <= allowedMaxWidth) && ( height() <= allowedMaxHeight) && 
               (width() > 0) && ( height() > 0);
    }
    
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

// Calculate Intersection over Union (IoU) between two rectangles
template <typename T>
inline float calculateIoU(const Rect<T>& rect1, const Rect<T>& rect2)
{
    // Calculate areas of both rectangles first
    T area1 = (rect1.r - rect1.l) * (rect1.b - rect1.t);
    T area2 = (rect2.r - rect2.l) * (rect2.b - rect2.t);
    
    // Area-based pre-filter: if areas are vastly different, skip expensive intersection calculation
    float area_ratio = static_cast<float>(std::min(area1, area2)) / static_cast<float>(std::max(area1, area2));
    if (area_ratio < 0.1f) {
        return 0.0f;
    }
    
    // Calculate intersection coordinates
    T x_left = std::max(rect1.l, rect2.l);
    T y_top = std::max(rect1.t, rect2.t);
    T x_right = std::min(rect1.r, rect2.r);
    T y_bottom = std::min(rect1.b, rect2.b);
    
    // Check if there's no intersection
    if (x_left >= x_right || y_top >= y_bottom) {
        return 0.0f;
    }
    
    // Calculate intersection area
    T intersection_area = (x_right - x_left) * (y_bottom - y_top);
    
    // Calculate union area
    T union_area = area1 + area2 - intersection_area;
    
    // Avoid division by zero
    if (union_area <= 0) {
        return 0.0f;
    }
    
    return static_cast<float>(intersection_area) / static_cast<float>(union_area);
}

// Estimate 2D affine transform (least squares fit) from src_pts to dst_pts
// src: [x0, y0, x1, y1, ...], dst: [x0', y0', x1', y1', ...], n: number of points, M: output 2x3 row-major
inline void estimate_affine_2d(const float* src, const float* dst, int n, float* M)
{
    // Solve for M in dst = M * src (M is 2x3)
    // Build A and b for least squares: A * m = b
    // m = [m00, m01, m02, m10, m11, m12]^T
    float A[36] = {0}; // 2*n x 6
    float b[10] = {0}; // 2*n
    for (int i = 0; i < n; ++i) {
        float x = src[2*i];
        float y = src[2*i+1];
        float xp = dst[2*i];
        float yp = dst[2*i+1];
        // Row for x'
        A[2*i*6 + 0] = x;
        A[2*i*6 + 1] = y;
        A[2*i*6 + 2] = 1;
        A[2*i*6 + 3] = 0;
        A[2*i*6 + 4] = 0;
        A[2*i*6 + 5] = 0;
        b[2*i] = xp;
        // Row for y'
        A[(2*i+1)*6 + 0] = 0;
        A[(2*i+1)*6 + 1] = 0;
        A[(2*i+1)*6 + 2] = 0;
        A[(2*i+1)*6 + 3] = x;
        A[(2*i+1)*6 + 4] = y;
        A[(2*i+1)*6 + 5] = 1;
        b[2*i+1] = yp;
    }
    // Solve least squares: m = (A^T A)^-1 A^T b
    float AtA[36] = {0};
    float Atb[6] = {0};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 2*n; ++k) {
                AtA[i*6+j] += A[k*6+i] * A[k*6+j];
            }
        }
        for (int k = 0; k < 2*n; ++k) {
            Atb[i] += A[k*6+i] * b[k];
        }
    }
    // Solve AtA * m = Atb (Gaussian elimination)
    float m[6] = {0};
    // Simple Gauss-Jordan elimination for 6x6
    float aug[6][7];
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) aug[i][j] = AtA[i*6+j];
        aug[i][6] = Atb[i];
    }
    for (int i = 0; i < 6; ++i) {
        // Find pivot
        int pivot = i;
        for (int j = i+1; j < 6; ++j) if (fabs(aug[j][i]) > fabs(aug[pivot][i])) pivot = j;
        if (pivot != i) for (int j = 0; j < 7; ++j) std::swap(aug[i][j], aug[pivot][j]);
        float div = aug[i][i];
        if (fabs(div) < 1e-8f) continue;
        for (int j = 0; j < 7; ++j) aug[i][j] /= div;
        for (int j = 0; j < 6; ++j) {
            if (j == i) continue;
            float f = aug[j][i];
            for (int k = 0; k < 7; ++k) aug[j][k] -= f * aug[i][k];
        }
    }
    for (int i = 0; i < 6; ++i) m[i] = aug[i][6];
    // Output
    for (int i = 0; i < 6; ++i) M[i] = m[i];
}

} // namespace math_utils
} // namespace linuxface
#endif // MATH_UTILS_H
