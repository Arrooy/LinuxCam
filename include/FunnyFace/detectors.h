#ifndef DETECTORS_H
#define DETECTORS_H

#include <memory>

#include "FunnyFace/face.h"

namespace funnyface
{

class FaceDetector
{
  public:
    virtual std::vector<FaceBoundingBox> detect(const std::unique_ptr<Image>& image) = 0;
};

class ShapeDetector
{
    virtual std::vector<Face>
    detect(const std::unique_ptr<Image>& image, const std::vector<math_utils::Rect<float>>& faces_rect) = 0;
};

} // namespace funnyface

#endif // DETECTORS_H
