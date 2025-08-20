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
    PHYSICAL_INPUT,
    VIRTUAL_OUTPUT
};

struct Buffer
{
    size_t length;
    void* start;
};

struct FrameSize
{
    unsigned int width{};
    unsigned int height{};
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
        common::logInfo("\t\tSize width: %d height: %d", width, height);
        common::logInfo("\t\tSelected fps index is %d", selectedFPS);
        for (const auto& fps : fps)
        {
            common::logInfo("\t\t\tFPS: %d", fps);
        }
        common::logInfo("\t\tSelected fps value is %d", getFps(selectedFPS));
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
    unsigned int pixelformat{};
    unsigned int selectedFrameSize{0u};
    std::vector<FrameSize> sizes;

    void print() const
    {
        common::logInfo("\tFormat description %s", description.c_str());
        common::logInfo("\tFormat enum: %s", fromImageFormatToString(format).c_str());
        common::logInfo("\tPixel format raw: %d", pixelformat);
        common::logInfo("\tSelected frame size index: %d", selectedFrameSize);

        common::logInfo("\tAvailable frame sizes:");
        for (const auto& size : sizes)
        {
            size.print();
        }
        const auto& selSize = sizes[selectedFrameSize];
        common::logInfo("\tSelected frame size value: %dx%d with %dFPS", selSize.width, selSize.height,
                        selSize.getFps(selSize.selectedFPS));
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
        common::logInfo("Driver name: %s", driver.c_str());
        common::logInfo("Driver card: %s", card.c_str());
        common::logInfo("Driver bus info: %s", bus_info.c_str());
        for (const auto& format : formats)
        {
            format.print();
        }
    }
};


class Webcam
{
  public:
    Webcam(std::string name, std::string devicePath, WebcamType type, unsigned int width, unsigned int height);
    virtual ~Webcam() = default;
    // Webcam(const Webcam&) = delete;
    // Webcam& operator=(Webcam&&) = delete;
    // Webcam& operator=(const Webcam&) = delete;
    // Webcam(Webcam&&) = delete;

    virtual bool setupDevice() = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRunning() = 0;

    std::string getDevicePath() const { return device_path; }
    WebcamType getType() const { return type; }

    std::string getName() const { return name; }
    CameraCapabilities getCapabilities() const { return capabilities; }

    Format getSelectedFormat() const
    {
        if (selected_format)
        {
            return *selected_format;
        }
        return Format{};
    }
    unsigned int getDesiredWidth() const
    {
        return selected_format ? selected_format->sizes[selected_format->selectedFrameSize].width : 0;
    }
    unsigned int getDesiredHeight() const
    {
        return selected_format ? selected_format->sizes[selected_format->selectedFrameSize].height : 0;
    }

    bool isCurrentlySelected() const { return currently_selected; }
    void setCurrentlySelected(bool selected) { currently_selected = selected; }

  protected:
    bool open();
    static bool configureDeviceFormat();
    bool updateDeviceCapabilities();

    static bool requeueFrame(struct v4l2_buffer& buf);
    static bool queueAllBuffersAgain(int numBuffers, int bufferType);

    static void selectBestFormat();
    static std::tuple<unsigned int, unsigned int, double> findBestFrameSize(const Format& fmt);
    static double
    calculateDistance(unsigned int width1, unsigned int height1, unsigned int width2, unsigned int height2);

    std::string name;
    std::string device_path;
    int fd{-1};

    WebcamType type{WebcamType::UNKNOWN};

    CameraCapabilities capabilities;
    std::unique_ptr<Format> selected_format;

    bool currently_selected{false}; // True if the user has selected this webcam in the UI
};

} // namespace linuxface
#endif // WEBCAM_H
