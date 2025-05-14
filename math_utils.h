/* -*- c++ -*- */

#ifndef MATH_UTILS_H
#define MATH_UTILS_H
#include <stdlib.h>

#include <cmath>
#include <vector>

namespace math_utils
{
/**
// function for line generation
std::vector<dlib::point> DDA(int x1, int y1, int x2, int y2)
{
    std::vector<dlib::point> result;

    // calculate dx & dy
    int dx = x2 - x1;
    int dy = y2 - y1;

    // calculate steps required for generating pixels
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    // calculate increment in x & y for each steps
    float Xinc = (float) dx / (float) steps;
    float Yinc = (float) dy / (float) steps;

    // Put pixel for each step
    float x = (float) x1;
    float y = (float) y1;
    for (int i = 0; i <= steps; i++)
    {
        result.emplace_back(round(x), round(y));
        x += Xinc; // increment in x at each step
        y += Yinc; // increment in y at each step
    }

    return result;
} */

double distance(int a, int b, int c, int d)
{
    return sqrt(pow(c - a, 2) + pow(d - b, 2));
}

} // namespace math_utils

#endif // MATH_UTILS_H
