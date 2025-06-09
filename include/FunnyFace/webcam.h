#ifndef WEBCAM_H
#define WEBCAM_H

#include <linux/videodev2.h>
#include <turbojpeg.h>

#include <memory>
#include <string>
#include <vector>

#include "FunnyFace/codecFactory.h"
namespace funnyface
{

enum class WebcamType
{
    UNKNOWN,
    PhysicalInput,
    VirtualOutput
};

struct Buffer
{
    size_t length;
    void* start;
};

struct FrameSize
{
    unsigned int width;
    unsigned int height;
};

struct Format
{
    std::string description;
    ImageFormat format{ImageFormat::UNKNOWN};
    unsigned int pixelformat;
    unsigned int selectedFrameSize{0u};
    std::vector<FrameSize> sizes;
};

struct CameraCapabilities
{
    std::string driver;
    std::string card;
    std::string bus_info;
    std::vector<Format> formats;
};


class Webcam
{
  public:
    Webcam(const std::string& name, const std::string& devicePath, const WebcamType type, const unsigned int width,
           const unsigned int height);
    virtual ~Webcam() = default;
    // Webcam(const Webcam&) = delete;
    // Webcam& operator=(Webcam&&) = delete;
    // Webcam& operator=(const Webcam&) = delete;
    // Webcam(Webcam&&) = delete;

    virtual bool setupDevice() = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRunning() = 0;


    virtual TJSAMP getChrominanceSubsampling() const = 0;

    std::string getDevicePath() const { return device_path_; }
    WebcamType getType() const { return type_; }

    std::string getName() const { return name_; }
    CameraCapabilities getCapabilities() const { return capabilities_; }
    Format getSelectedFormat() const
    {
        if (selectedFormat_)
        {
            return *selectedFormat_;
        }
        return Format{};
    }
    unsigned int getDesiredWidth() const { return desiredWidth_; }
    unsigned int getDesiredHeight() const { return desiredHeight_; }
  protected:
    bool open();
    bool configureDeviceFormat();
    bool updateDeviceCapabilities();

    bool requeueFrame(struct v4l2_buffer& buf);
    bool queueAllBuffersAgain(int numBuffers, int bufferType);


    void selectBestFormat();
    std::pair<unsigned int, double> findBestFrameSize(const Format& fmt) const;
    double
    calculateDistance(unsigned int width1, unsigned int height1, unsigned int width2, unsigned int height2) const;

    std::string name_;
    std::string device_path_;
    int fd_{-1};

    unsigned int desiredWidth_{0u};
    unsigned int desiredHeight_{0u};

    WebcamType type_{WebcamType::UNKNOWN};

    CameraCapabilities capabilities_;
    std::unique_ptr<Format> selectedFormat_;
};

} // namespace funnyface
#endif // WEBCAM_H
