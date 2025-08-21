#ifndef INPUTWEBCAM_H
#define INPUTWEBCAM_H

#include <atomic>
#include <linux/videodev2.h>
#include <thread>

#include "LinuxFace/codec.h"
#include "LinuxFace/webcam.h"
namespace linuxface
{

class InputWebcam : public Webcam
{
  public:
    InputWebcam(const std::string& name, const std::string& devicePath, unsigned int width, unsigned int height,
                unsigned int bufferCount);

    ~InputWebcam() override;
    bool setupDevice() override;
    bool start() override;
    bool stop() override;
    bool isRunning() override;

    bool getImage(std::unique_ptr<Image>& outImage);

    bool reconfigureFormat(int formatIndex, int sizeIndex, int fpsIndex);

  private:
    bool startRecording();
    void stopRecording();
    bool startStreaming();
    bool stopStreaming();
    void cleanup();

    void cleanupBuffers();
    void imageAcquisitionLoop();

    bool ready_{false};

    // Adcquisition Buffers
    bool configureBuffers();
    unsigned int buffer_count_{0u};
    Buffer* buffers_{nullptr};
    struct v4l2_requestbuffers bufrequest_;

    // Threading
    std::thread recordThread_;
    std::atomic<bool> isRecording_{false};

    // Decoder of input image
    std::unique_ptr<Decoder> decoder_;

    mutable std::mutex imageMutex_;
    std::unique_ptr<Image> latestImage_;
};
} // namespace linuxface

#endif // INPUTWEBCAM_H
