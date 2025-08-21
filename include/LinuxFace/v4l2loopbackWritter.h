#ifndef V4L2LOOPBACKWRITTER_H
#define V4L2LOOPBACKWRITTER_H


#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "LinuxFace/common.h"
#include "LinuxFace/webcam.h"

namespace linuxface
{

class V4L2LoopbackWriter : public Webcam
{

  public:
    V4L2LoopbackWriter(const std::string& name, const std::string& devicePath, unsigned int width, unsigned int height,
                       TJSAMP subsample);

    ~V4L2LoopbackWriter() override;

    bool setupDevice() override;
    bool start() override;
    bool stop() override;
    bool isRunning() override { return streaming_; }

    bool writeFrame(Image& image);
    void cleanup();

    bool reconfigure(TJSAMP subsampling, int quality);

    TJSAMP getChrominanceSubsampling() const { return chrominance_subsampling_; }
    int getQuality() const { return quality_; }
  private:
    std::vector<Buffer> buffers_;
    bool streaming_{false};

    TJSAMP chrominance_subsampling_;
    int quality_{100};

    // Encoder of output image
    std::unique_ptr<Encoder> encoder_;
};
} // namespace linuxface


#endif // V4L2LOOPBACKWRITTER_H
