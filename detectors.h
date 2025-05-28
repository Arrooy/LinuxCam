#ifndef DETECTORS_H
#define DETECTORS_H

#include "face.h"
namespace funnyface
{

class FaceDetector
{
  public:
    virtual std::vector<math_utils::Rect> detect(const Image& image) = 0;
};

class ShapeDetector
{
    virtual std::vector<Face> detect(const Image& image, const std::vector<math_utils::Rect>& faces_rect) = 0;
};

} // namespace funnyface

#endif // DETECTORS_H
