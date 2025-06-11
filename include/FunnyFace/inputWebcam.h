#ifndef INPUTWEBCAM_H
#define INPUTWEBCAM_H

#include <linux/videodev2.h>

#include <atomic>
#include <thread>

#include "FunnyFace/codec.h"
#include "FunnyFace/webcam.h"
namespace funnyface
{

class InputWebcam : public Webcam
{
  public:
    InputWebcam(const std::string& name, const std::string& devicePath, const unsigned int width,
                const unsigned int height, const unsigned int bufferCount);

    ~InputWebcam();
    bool setupDevice() override;
    bool start() override;
    bool stop() override;
    bool isRunning() override;

    TJSAMP getChrominanceSubsampling() const override { return TJSAMP::TJSAMP_444; };


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
} // namespace funnyface

#endif // INPUTWEBCAM_H
