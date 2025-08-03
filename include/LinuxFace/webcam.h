#ifndef WEBCAM_H
#define WEBCAM_H

#include <linux/videodev2.h>
#include <memory>
#include <string>
#include <tuple>
#include <turbojpeg.h>
#include <vector>

#include "LinuxFace/codecFactory.h"
namespace linuxface
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
    unsigned int selectedFPS{0u};
    std::vector<unsigned int> fps{0u};
    unsigned int getFps(size_t index) const
    {
        if (index < fps.size())
        {
            return fps[index];
        }
        return 0u; // Return 0 if index is out of bounds
    }

    void print() const
    {
        common::log_info("\t\tSize width: %d height: %d", width, height);
        common::log_info("\t\tSelected fps index is %d", selectedFPS);
        for (const auto& fps : fps)
        {
            common::log_info("\t\t\tFPS: %d", fps);
        }
        common::log_info("\t\tSelected fps value is %d", getFps(selectedFPS));
    }

    bool operator==(const FrameSize& other) const
    {
        return width == other.width && height == other.height && fps.size() == other.fps.size()
               && std::equal(fps.begin(), fps.end(), other.fps.begin());
    }
};

struct Format
{
    std::string description;
    ImageFormat format{ImageFormat::UNKNOWN};
    unsigned int pixelformat;
    unsigned int selectedFrameSize{0u};
    std::vector<FrameSize> sizes;

    void print() const
    {
        common::log_info("\tFormat description %s", description.c_str());
        common::log_info("\tFormat enum: %s", fromImageFormatToString(format).c_str());
        common::log_info("\tPixel format raw: %d", pixelformat);
        common::log_info("\tSelected frame size index: %d", selectedFrameSize);

        common::log_info("\tAvailable frame sizes:");
        for (const auto& size : sizes)
        {
            size.print();
        }
        const auto& sel_size = sizes[selectedFrameSize];
        common::log_info("\tSelected frame size value: %dx%d with %dFPS", sel_size.width, sel_size.height,
                         sel_size.getFps(sel_size.selectedFPS));
    }
};

struct CameraCapabilities
{
    std::string driver;
    std::string card;
    std::string bus_info;
    std::vector<Format> formats;

    void print() const
    {
        common::log_info("Driver name: %s", driver.c_str());
        common::log_info("Driver card: %s", card.c_str());
        common::log_info("Driver bus info: %s", bus_info.c_str());
        for (const auto& format : formats)
        {
            format.print();
        }
    }
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
    unsigned int getDesiredWidth() const
    {
        return selectedFormat_ ? selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width : 0;
    }
    unsigned int getDesiredHeight() const
    {
        return selectedFormat_ ? selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height : 0;
    }

    bool isCurrentlySelected() const { return currentlySelected_; }
    void setCurrentlySelected(bool selected) { currentlySelected_ = selected; }

  protected:
    bool open();
    bool configureDeviceFormat();
    bool updateDeviceCapabilities();

    bool requeueFrame(struct v4l2_buffer& buf);
    bool queueAllBuffersAgain(int numBuffers, int bufferType);


    void selectBestFormat();
    std::tuple<unsigned int, unsigned int, double> findBestFrameSize(const Format& fmt) const;
    double
    calculateDistance(unsigned int width1, unsigned int height1, unsigned int width2, unsigned int height2) const;

    std::string name_;
    std::string device_path_;
    int fd_{-1};

    WebcamType type_{WebcamType::UNKNOWN};

    CameraCapabilities capabilities_;
    std::unique_ptr<Format> selectedFormat_;

    bool currentlySelected_{false}; // True if the user has selected this webcam in the UI
};

} // namespace linuxface
#endif // WEBCAM_H
