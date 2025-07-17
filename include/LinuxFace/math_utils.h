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
    double cx;
    double cy;
    int stride;
};
template<typename T=long>
struct Point
{
    Point(T x1, T y1) : x(x1), y(y1) {}
    Point() = default;
    T x;
    T y;
};

struct Point3D
{
    Point3D(double x1, double y1, double z1 = 0.0) : x(x1), y(y1), z(z1) {}
    Point3D() = default;
    double x;
    double y;
    double z;
};
struct StridePoint
{
    StridePoint(double x1, double y1, double s) : cx(x1), cy(y1), stride(s) {}
    double cx;
    double cy;
    double stride;
};

template <typename T = long>
struct Rect
{
    Rect() = default;
    Rect(T left, T top, T right, T bottom) : l(left), t(top), r(right), b(bottom) {}

    Rect(Point<T> leftTopCorner, T w, T h)
        : l(static_cast<T>(leftTopCorner.x)),
          t(static_cast<T>(leftTopCorner.y)),
          r(static_cast<T>(leftTopCorner.x + w)),
          b(static_cast<T>(leftTopCorner.y + h))
    {
    }

    Rect(Point<T> leftTopCorner, Point<T> rightBottomCorner)
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

    bool contains(const Point<T>& p) const { return contains(static_cast<T>(p.x), static_cast<T>(p.y)); }

    // Check if this rectangle is within bounds of maximum dimensions by given factor
    bool isWithinBounds(T maxWidth, T maxHeight, float scaleFactor = 1.2f) const
    {
        const T allowedMaxWidth = static_cast<T>(maxWidth * scaleFactor);
        const T allowedMaxHeight = static_cast<T>(maxHeight * scaleFactor);

        return (width() <= allowedMaxWidth) && (height() <= allowedMaxHeight) && (width() > 0) && (height() > 0);
    }

    void addPadding(T left, T top, T right, T bottom)
    {
        l -= left;
        t -= top;
        r += right;
        b += bottom;
    }

    T l;
    T t;
    T r;
    T b;
};


// function for line generation
template <typename T>
std::vector<Point<long>> DDA(const T& x1, const T& y1, const T& x2, const T& y2)
{
    std::vector<Point<long>> result;

    // calculate dx & dy
    T dx = x2 - x1;
    T dy = y2 - y1;

    // calculate steps required for generating pixels
    T steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    // calculate increment in x & y for each steps
    double xInc = static_cast<double>(dx) / static_cast<double>(steps);
    double yInc = static_cast<double>(dy) / static_cast<double>(steps);

    // Put pixel for each step
    double x = static_cast<double>(x1);
    double y = static_cast<double>(y1);
    for (int i = 0; i <= steps; i++)
    {
        result.emplace_back(Point(lround(x), lround(y)));
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
    if (area_ratio < 0.1f)
    {
        return 0.0f;
    }

    // Calculate intersection coordinates
    T x_left = std::max(rect1.l, rect2.l);
    T y_top = std::max(rect1.t, rect2.t);
    T x_right = std::min(rect1.r, rect2.r);
    T y_bottom = std::min(rect1.b, rect2.b);

    // Check if there's no intersection
    if (x_left >= x_right || y_top >= y_bottom)
    {
        return 0.0f;
    }

    // Calculate intersection area
    T intersection_area = (x_right - x_left) * (y_bottom - y_top);

    // Calculate union area
    T union_area = area1 + area2 - intersection_area;

    // Avoid division by zero
    if (union_area <= 0)
    {
        return 0.0f;
    }

    return static_cast<float>(intersection_area) / static_cast<float>(union_area);
}

// Estimate 2D affine transform (least squares fit) from src_pts to dst_pts
// src: [x0, y0, x1, y1, ...], dst: [x0', y0', x1', y1', ...], n: number of points, M: output 2x3 row-major
// Returns true if successful, false if failed (M is set to identity on failure)
inline bool estimate_affine_2d(const double* src, const double* dst, int n, double* M)
{
    // Input validation: check for NaN/Inf
    for (int i = 0; i < 2 * n; ++i)
    {
        if (std::isnan(src[i]) || std::isinf(src[i]) || std::isnan(dst[i]) || std::isinf(dst[i]))
        {
            M[0] = 1;
            M[1] = 0;
            M[2] = 0;
            M[3] = 0;
            M[4] = 1;
            M[5] = 0;
            return false;
        }
    }
    if (n < 3)
    {
        M[0] = 1;
        M[1] = 0;
        M[2] = 0;
        M[3] = 0;
        M[4] = 1;
        M[5] = 0;
        return false;
    }
    // Build A (2n x 6) and b (2n)
    std::vector<double> A(2 * n * 6, 0.0);
    std::vector<double> b(2 * n, 0.0);
    for (int i = 0; i < n; ++i)
    {
        double x = src[2 * i];
        double y = src[2 * i + 1];
        double xp = dst[2 * i];
        double yp = dst[2 * i + 1];
        // Row for x'
        A[(2 * i) * 6 + 0] = x;
        A[(2 * i) * 6 + 1] = y;
        A[(2 * i) * 6 + 2] = 1.0;
        A[(2 * i) * 6 + 3] = 0.0;
        A[(2 * i) * 6 + 4] = 0.0;
        A[(2 * i) * 6 + 5] = 0.0;
        b[2 * i] = xp;
        // Row for y'
        A[(2 * i + 1) * 6 + 0] = 0.0;
        A[(2 * i + 1) * 6 + 1] = 0.0;
        A[(2 * i + 1) * 6 + 2] = 0.0;
        A[(2 * i + 1) * 6 + 3] = x;
        A[(2 * i + 1) * 6 + 4] = y;
        A[(2 * i + 1) * 6 + 5] = 1.0;
        b[2 * i + 1] = yp;
    }
    // Compute AtA (6x6) and Atb (6)
    double AtA[6][6] = {0};
    double Atb[6] = {0};
    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 6; ++j)
        {
            for (int k = 0; k < 2 * n; ++k)
            {
                AtA[i][j] += A[k * 6 + i] * A[k * 6 + j];
            }
        }
        for (int k = 0; k < 2 * n; ++k)
        {
            Atb[i] += A[k * 6 + i] * b[k];
        }
    }
    // Regularization (Tikhonov): add small value to diagonal
    const double lambda = 1e-10;
    for (int i = 0; i < 6; ++i)
    {
        AtA[i][i] += lambda;
    }
    // Solve AtA * m = Atb (Gauss-Jordan with partial pivoting)
    double aug[6][7];
    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 6; ++j)
        {
            aug[i][j] = AtA[i][j];
        }
        aug[i][6] = Atb[i];
    }
    bool singular = false;
    for (int i = 0; i < 6; ++i)
    {
        // Find pivot
        int pivot = i;
        double maxval = fabs(aug[i][i]);
        for (int j = i + 1; j < 6; ++j)
        {
            if (fabs(aug[j][i]) > maxval)
            {
                maxval = fabs(aug[j][i]);
                pivot = j;
            }
        }
        if (maxval < 1e-12)
        {
            singular = true;
            break;
        }
        if (pivot != i)
        {
            for (int j = 0; j < 7; ++j)
            {
                std::swap(aug[i][j], aug[pivot][j]);
            }
        }
        double div = aug[i][i];
        for (int j = 0; j < 7; ++j)
        {
            aug[i][j] /= div;
        }
        for (int j = 0; j < 6; ++j)
        {
            if (j == i)
            {
                continue;
            }
            double f = aug[j][i];
            for (int k = 0; k < 7; ++k)
            {
                aug[j][k] -= f * aug[i][k];
            }
        }
    }
    if (singular)
    {
        M[0] = 1;
        M[1] = 0;
        M[2] = 0;
        M[3] = 0;
        M[4] = 1;
        M[5] = 0;
        return false;
    }
    // Output
    for (int i = 0; i < 6; ++i)
    {
        if (std::isnan(aug[i][6]) || std::isinf(aug[i][6]))
        {
            M[0] = 1;
            M[1] = 0;
            M[2] = 0;
            M[3] = 0;
            M[4] = 1;
            M[5] = 0;
            return false;
        }
        M[i] = aug[i][6];
    }
    return true;
}

inline bool invert_affine(const double* M, double invM[6])
{
    // Invert the affine transformation matrix for inverse mapping
    double a = M[0], b = M[1], c = M[2];
    double d = M[3], e = M[4], f = M[5];
    double det = a * e - b * d;
    if (fabs(det) < 1e-12)
    {
        return false;
    }
    invM[0] = e / det;
    invM[1] = -b / det;
    invM[2] = (b * f - e * c) / det;
    invM[3] = -d / det;
    invM[4] = a / det;
    invM[5] = (d * c - a * f) / det;
    return true;
}

template <typename T>
math_utils::Point<T> rotate_point(const math_utils::Point<T>& pt,
                                       const math_utils::Point<T>& origin,
                                       double angleRad)
{
    T dx = pt.x - origin.x;
    T dy = pt.y - origin.y;
    double cosA = std::cos(angleRad);
    double sinA = std::sin(angleRad);

    return {
        cosA * dx - sinA * dy + origin.x,
        sinA * dx + cosA * dy + origin.y
    };
}

} // namespace math_utils
} // namespace linuxface
#endif // MATH_UTILS_H
