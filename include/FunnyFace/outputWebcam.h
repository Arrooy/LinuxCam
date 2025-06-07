#if 0
#ifndef OUTPUTWEBCAM_H
#define OUTPUTWEBCAM_H

#include "FunnyFace/webcam.h"
namespace funnyface
{

class OutputWebcam : public Webcam
{
  public:
    OutputWebcam(const std::string& name, const std::string& devicePath, const unsigned int width,
                 const unsigned int height, const TJSAMP subsample);
    ~OutputWebcam();

    virtual bool writeFrame(Image& image) = 0;

    TJSAMP getChrominanceSubsampling() const override { return chrominance_subsampling_; };
  private:
    TJSAMP chrominance_subsampling_;

    // Encoder of output image
    std::unique_ptr<Encoder> encoder_;

    Image outputImage_;
    bool running_;
};

} // namespace funnyface

#endif // OUTPUTWEBCAM_H
#endif
