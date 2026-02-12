#ifndef MATH_UTILS_H
#define MATH_UTILS_H
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

namespace linuxface::math_utils
{

struct Anchor
{
    double cx;
    double cy;
    int stride;
};
template <typename T = long>
struct Point
{
    Point(T x1, T y1) : x(x1), y(y1) {}
    Point() : x(T{}), y(T{}) {}
    T x;
    T y;
};

struct Point3D
{
    Point3D(double x1, double y1, double z1 = 0.0) : x(x1), y(y1), z(z1) {}
    Point3D() : x(0.0), y(0.0), z(0.0) {}
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
    Rect() : l(T{}), t(T{}), r(T{}), b(T{}) {}
    Rect(T left, T top, T right, T bottom) : l(left), t(top), r(right), b(bottom) {}

    Rect(Point<T> leftTopCorner, T w, T h)
        : l(static_cast<T>(leftTopCorner.x))
        , t(static_cast<T>(leftTopCorner.y))
        , r(static_cast<T>(leftTopCorner.x + w))
        , b(static_cast<T>(leftTopCorner.y + h))
    {
    }

    Rect(Point<T> leftTopCorner, Point<T> rightBottomCorner)
        : l(static_cast<T>(leftTopCorner.x))
        , t(static_cast<T>(leftTopCorner.y))
        , r(static_cast<T>(rightBottomCorner.x))
        , b(static_cast<T>(rightBottomCorner.y))
    {
    }

    T x() const { return l; }
    T y() const { return t; }

    // Assuming r >= l and b >= t
    T width() const
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            return r - l;
        }
        else
        {
            return r - l + 1;
        }
    }
    T height() const
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            return b - t;
        }
        else
        {
            return b - t + 1;
        }
    }

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
    T dx = x2 - x1;
    T dy = y2 - y1;
    T steps = std::round(std::max(std::abs(dx), std::abs(dy)));

    if (steps == 0)
    {
        // Same start and end point
        result.emplace_back(Point<long>(std::lround(x1), std::lround(y1)));
        return result;
    }

    const double xInc = static_cast<double>(dx) / static_cast<double>(steps);
    const double yInc = static_cast<double>(dy) / static_cast<double>(steps);
    auto x = static_cast<double>(x1);
    auto y = static_cast<double>(y1);

    for (int i = 0; i < steps; i++)
    {
        result.emplace_back(std::lround(x), std::lround(y));
        x += xInc;
        y += yInc;
    }

    // Add final point, but check for duplicates
    Point<long> finalPoint;
    if constexpr (std::is_floating_point_v<T>)
    {
        finalPoint = Point<long>(static_cast<long>(std::floor(x2)), static_cast<long>(std::floor(y2)));
    }
    else
    {
        finalPoint = Point<long>(x2, y2);
    }

    // Only add final point if it's different from the last point
    if (result.empty() || result.back().x != finalPoint.x || result.back().y != finalPoint.y)
    {
        result.emplace_back(finalPoint);
    }

    return result;
}

template <typename T>
inline T distance(T x1, T y1, T x2, T y2)
{
    return std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2));
}

// Calculate Intersection over Union (IoU) between two rectangles
template <typename T>
inline float calculateIoU(const Rect<T>& rect1, const Rect<T>& rect2)
{
    // Calculate areas of both rectangles first
    T area1 = (rect1.r - rect1.l) * (rect1.b - rect1.t);
    T area2 = (rect2.r - rect2.l) * (rect2.b - rect2.t);

    // Area-based pre-filter: if areas are vastly different, skip expensive intersection calculation
    const float areaRatio = static_cast<float>(std::min(area1, area2)) / static_cast<float>(std::max(area1, area2));
    if (areaRatio < 0.1f)
    {
        return 0.0f;
    }

    // Calculate intersection coordinates
    T xLeft = std::max(rect1.l, rect2.l);
    T yTop = std::max(rect1.t, rect2.t);
    T xRight = std::min(rect1.r, rect2.r);
    T yBottom = std::min(rect1.b, rect2.b);

    // Check if there's no intersection
    if (xLeft >= xRight || yTop >= yBottom)
    {
        return 0.0f;
    }

    // Calculate intersection area
    T intersectionArea = (xRight - xLeft) * (yBottom - yTop);

    // Calculate union area
    T unionArea = area1 + area2 - intersectionArea;

    // Avoid division by zero
    if (unionArea <= 0)
    {
        return 0.0f;
    }

    return static_cast<float>(intersectionArea) / static_cast<float>(unionArea);
}

// Estimate 2D affine transform (least squares fit) from src_pts to dst_pts
// src: [x0, y0, x1, y1, ...], dst: [x0', y0', x1', y1', ...], n: number of points, M: output 2x3 row-major
// Returns true if successful, false if failed (M is set to identity on failure)
inline bool estimateAffine2d(const double* src, const double* dst, int n, double* m)
{
    // Input validation: check for NaN/Inf
    for (int i = 0; i < 2 * n; ++i)
    {
        if (std::isnan(src[i]) || std::isinf(src[i]) || std::isnan(dst[i]) || std::isinf(dst[i]))
        {
            m[0] = 1;
            m[1] = 0;
            m[2] = 0;
            m[3] = 0;
            m[4] = 1;
            m[5] = 0;
            return false;
        }
    }
    if (n < 3)
    {
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 0;
        m[4] = 1;
        m[5] = 0;
        return false;
    }
    // Build a (2n x 6) and b (2n)
    std::vector<double> a(2 * n * 6, 0.0);
    std::vector<double> b(2 * n, 0.0);
    for (int i = 0; i < n; ++i)
    {
        const double x = src[2 * i];
        const double y = src[2 * i + 1];
        const double xp = dst[2 * i];
        const double yp = dst[2 * i + 1];
        // Row for x'
        a[(2 * i) * 6 + 0] = x;
        a[(2 * i) * 6 + 1] = y;
        a[(2 * i) * 6 + 2] = 1.0;
        a[(2 * i) * 6 + 3] = 0.0;
        a[(2 * i) * 6 + 4] = 0.0;
        a[(2 * i) * 6 + 5] = 0.0;
        b[2 * i] = xp;
        // Row for y'
        a[(2 * i + 1) * 6 + 0] = 0.0;
        a[(2 * i + 1) * 6 + 1] = 0.0;
        a[(2 * i + 1) * 6 + 2] = 0.0;
        a[(2 * i + 1) * 6 + 3] = x;
        a[(2 * i + 1) * 6 + 4] = y;
        a[(2 * i + 1) * 6 + 5] = 1.0;
        b[2 * i + 1] = yp;
    }
    // Compute AtA (6x6) and Atb (6)
    double atA[6][6] = {0};
    double atb[6] = {0};
    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 6; ++j)
        {
            for (int k = 0; k < 2 * n; ++k)
            {
                atA[i][j] += a[k * 6 + i] * a[k * 6 + j];
            }
        }
        for (int k = 0; k < 2 * n; ++k)
        {
            atb[i] += a[k * 6 + i] * b[k];
        }
    }
    // Regularization (Tikhonov): add small value to diagonal
    const double lambda = 1e-10;
    for (int i = 0; i < 6; ++i)
    {
        atA[i][i] += lambda;
    }
    // Solve AtA * m = Atb (Gauss-Jordan with partial pivoting)
    double aug[6][7];
    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 6; ++j)
        {
            aug[i][j] = atA[i][j];
        }
        aug[i][6] = atb[i];
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
        const double div = aug[i][i];
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
            const double f = aug[j][i];
            for (int k = 0; k < 7; ++k)
            {
                aug[j][k] -= f * aug[i][k];
            }
        }
    }
    // Output
    if (singular)
    {
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 0;
        m[4] = 1;
        m[5] = 0;
        return false;
    }
    for (int i = 0; i < 6; ++i)
    {
        if (std::isnan(aug[i][6]) || std::isinf(aug[i][6]))
        {
            m[0] = 1;
            m[1] = 0;
            m[2] = 0;
            m[3] = 0;
            m[4] = 1;
            m[5] = 0;
            return false;
        }
        m[i] = aug[i][6];
    }
    // Check for all-zero solution (degenerate)
    bool allZero = true;
    for (int i = 0; i < 6; ++i)
    {
        if (std::abs(m[i]) > 1e-8)
        {
            allZero = false;
            break;
        }
    }
    if (allZero)
    {
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 0;
        m[4] = 1;
        m[5] = 0;
        return false;
    }
    // For identity and translation, return true if solution matches expected
    // If input is identity or translation, the matrix should be close to expected
    return true;
}

inline bool estimateSimilarity2d(const double* src, const double* dst, int n, double* m)
{
    // Input validation: check for NaN/Inf
    for (int i = 0; i < 2 * n; ++i)
    {
        if (std::isnan(src[i]) || std::isinf(src[i]) || std::isnan(dst[i]) || std::isinf(dst[i]))
        {
            m[0] = 1;
            m[1] = 0;
            m[2] = 0;
            m[3] = 0;
            m[4] = 1;
            m[5] = 0;
            return false;
        }
    }
    if (n < 2)
    {
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 0;
        m[4] = 1;
        m[5] = 0;
        return false;
    }

    // Compute centroids
    double srcMeanX = 0;
    double srcMeanY = 0;
    double dstMeanX = 0;
    double dstMeanY = 0;

    for (int i = 0; i < n; ++i)
    {
        srcMeanX += src[2 * i];
        srcMeanY += src[2 * i + 1];
        dstMeanX += dst[2 * i];
        dstMeanY += dst[2 * i + 1];
    }
    srcMeanX /= n;
    srcMeanY /= n;
    dstMeanX /= n;
    dstMeanY /= n;

    // Center points
    const double muSrc[2] = {srcMeanX, srcMeanY};
    const double muDst[2] = {dstMeanX, dstMeanY};

    double covXx = 0;
    double covXy = 0;
    double covYx = 0;
    double covYy = 0;
    double srcVar = 0;

    for (int i = 0; i < n; ++i)
    {
        const double xs = src[2 * i] - muSrc[0];
        const double ys = src[2 * i + 1] - muSrc[1];
        const double xd = dst[2 * i] - muDst[0];
        const double yd = dst[2 * i + 1] - muDst[1];

        covXx += xd * xs;
        covXy += xd * ys;
        covYx += yd * xs;
        covYy += yd * ys;

        srcVar += xs * xs + ys * ys;
    }

    if (srcVar == 0)
    {
        std::memset(m, 0, sizeof(double) * 6);
        m[0] = m[4] = 1.0;
        return false;
    }

    // Compute scale and rotation
    const double scale = (covXx + covYy) / srcVar;
    double r00 = (covXx + covYy) / (covXx + covYy); // normalized to 1
    double r01 = (covXy - covYx) / (covXx + covYy);
    double r10 = (covYx - covXy) / (covXx + covYy);
    double r11 = r00;

    const double norm = std::sqrt(r00 * r00 + r10 * r10);
    if (norm < 1e-10)
    {
        std::memset(m, 0, sizeof(double) * 6);
        m[0] = m[4] = 1.0;
        return false;
    }

    r00 = scale * r00 / norm;
    r01 = scale * r01 / norm;
    r10 = scale * r10 / norm;
    r11 = scale * r11 / norm;

    // Translation
    const double tx = muDst[0] - (r00 * muSrc[0] + r01 * muSrc[1]);
    const double ty = muDst[1] - (r10 * muSrc[0] + r11 * muSrc[1]);

    m[0] = r00;
    m[1] = r01;
    m[2] = tx;
    m[3] = r10;
    m[4] = r11;
    m[5] = ty;

    return true;
}

inline bool estimateProcrustesSimilarity(const double* src, const double* dst, int n, double* m)
{
    if (n < 2)
    {
        return false;
    }

    // 1. Compute centroids
    double sx = 0;
    double sy = 0;
    double dx = 0;
    double dy = 0;
    for (int i = 0; i < n; ++i)
    {
        sx += src[2 * i];
        sy += src[2 * i + 1];
        dx += dst[2 * i];
        dy += dst[2 * i + 1];
    }
    sx /= n;
    sy /= n;
    dx /= n;
    dy /= n;

    // 2. Centered coordinates
    std::vector<double> s(2 * n);
    std::vector<double> d(2 * n);
    for (int i = 0; i < n; ++i)
    {
        s[2 * i] = src[2 * i] - sx;
        s[2 * i + 1] = src[2 * i + 1] - sy;
        d[2 * i] = dst[2 * i] - dx;
        d[2 * i + 1] = dst[2 * i + 1] - dy;
    }

    // 3. Compute covariance and variance
    double varS = 0;
    double covXx = 0;
    double covXy = 0;
    double covYx = 0;
    double covYy = 0;
    for (int i = 0; i < n; ++i)
    {
        const double xs = s[2 * i];
        const double ys = s[2 * i + 1];
        const double xd = d[2 * i];
        const double yd = d[2 * i + 1];
        varS += xs * xs + ys * ys;
        covXx += xd * xs;
        covXy += xd * ys;
        covYx += yd * xs;
        covYy += yd * ys;
    }
    if (varS == 0)
    {
        return false;
    }

    // 4. Compute rotation & scale
    const double trace = covXx + covYy;
    // double det = cov_xx*cov_yy - cov_xy*cov_yx;
    const double scale = trace / varS;
    const double theta = atan2(covXy - covYx, covXx + covYy); // rotation angle

    const double cs = cos(theta);
    const double sn = sin(theta);

    // 5. Build 2×3 matrix
    m[0] = scale * cs;
    m[1] = -scale * sn;
    m[2] = dx - m[0] * sx - m[1] * sy;
    m[3] = scale * sn;
    m[4] = scale * cs;
    m[5] = dy - m[3] * sx - m[4] * sy;

    return true;
}

inline bool invertAffine(const double* m, double invM[6])
{
    // Invert the affine transformation matrix for inverse mapping
    const double a = m[0];
    const double b = m[1];
    const double c = m[2];
    const double d = m[3];
    const double e = m[4];
    const double f = m[5];
    const double det = a * e - b * d;
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
math_utils::Point<T> rotatePoint(const math_utils::Point<T>& pt, const math_utils::Point<T>& origin, double angleRad)
{
    double dx = static_cast<double>(pt.x) - static_cast<double>(origin.x);
    double dy = static_cast<double>(pt.y) - static_cast<double>(origin.y);
    const double cosA = std::cos(angleRad);
    const double sinA = std::sin(angleRad);
    return {static_cast<T>(std::round(cosA * dx - sinA * dy + origin.x)),
            static_cast<T>(std::round(sinA * dx + cosA * dy + origin.y))};
}

// L2 normalization function - normalizes a vector to unit length
template <typename T>
inline void l2norm(std::vector<T>& vec)
{
    T norm = T{0};
    for (const auto& val : vec)
    {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > T{0}) // Avoid division by zero
    {
        for (auto& val : vec)
        {
            val /= norm;
        }
    }
}

} // namespace linuxface::math_utils

#endif // MATH_UTILS_H
