#ifndef DETECTORS_H
#define DETECTORS_H

#include <memory>

#include "LinuxFace/face.h"

namespace linuxface
{

class FaceDetector
{
  public:
    virtual std::vector<Face> detect(const std::unique_ptr<Image>& image) = 0;
};

class ShapeDetector
{
    virtual std::vector<Face>
    detect(const std::unique_ptr<Image>& image, const std::vector<math_utils::Rect<float>>& faces_rect) = 0;
};

} // namespace linuxface

#endif // DETECTORS_H
