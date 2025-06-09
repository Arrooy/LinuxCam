#ifndef V4L2LOOPBACKWRITTER_H
#define V4L2LOOPBACKWRITTER_H


#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <vector>

#include "FunnyFace/common.h"
#include "FunnyFace/webcam.h"

namespace funnyface
{

class V4L2LoopbackWriter : public Webcam
{

  public:
    V4L2LoopbackWriter(const std::string& name, const std::string& devicePath, const unsigned int width,
                       const unsigned int height, const TJSAMP subsample);

    ~V4L2LoopbackWriter();

    bool setupDevice() override;
    bool start() override;
    bool stop() override;
    bool isRunning() override { return streaming_; }


    bool writeFrame(Image& image);
    void cleanup();


    TJSAMP getChrominanceSubsampling() const override { return chrominance_subsampling_; };
    bool reconfigureSubsampling(TJSAMP subsampling);
  private:
    int fd_;
    std::vector<Buffer> buffers_;
    bool streaming_;

    TJSAMP chrominance_subsampling_;

    // Encoder of output image
    std::unique_ptr<Encoder> encoder_;
};
} // namespace funnyface


#endif // V4L2LOOPBACKWRITTER_H
