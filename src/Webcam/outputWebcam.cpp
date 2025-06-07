#if 0
#include "FunnyFace/outputWebcam.h"

#include "FunnyFace/common.h"
#include "FunnyFace/profiler.h"
using namespace funnyface;

OutputWebcam::OutputWebcam(const std::string& name, const std::string& devicePath, const unsigned int width,
                           const unsigned int height, const TJSAMP subsample)
    : Webcam(name, devicePath, WebcamType::VirtualOutput, width, height)
{
    chrominance_subsampling_ = subsample;
    running_ = false;
}

OutputWebcam::~OutputWebcam()
{
    running_ = false;

    if (encoder_ != nullptr)
    {
        encoder_.reset();
    }

    if (fd_ >= 0)
    {
        common::log_info("OutputWebcam - Closing fd!");
        close(fd_);
        fd_ = -1;
    }
}

bool OutputWebcam::setupDevice()
{
    // Open the webcam device
    if (!Webcam::open())
    {
        return false;
    }

    auto pixelFormat = TJPF_RGB;

    ConfigBuilder configBuilder;
    configBuilder.imageFormat(selectedFormat_->format)
        .pixelFormat(pixelFormat)
        .width(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width)
        .height(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height)
        .quality(100)
        .chrominance_subsampling(chrominance_subsampling_);

    encoder_ = CodecFactory::create<Encoder>(configBuilder);
    if (encoder_ == nullptr)
    {
        common::log_error("OutputWebcam::setupDevice - Failed to create encoder");
        return false;
    }

    unsigned long required_size{0u};
    encoder_->encodeSizeInBytes(outputImage_, required_size);

    outputImage_.resize(required_size);
    running_ = true;
    return true;
}
bool OutputWebcam::start()
{
    return true;
}

bool OutputWebcam::stop()
{
    return true;
}

bool OutputWebcam::isRunning()
{
    return running_;
}

bool OutputWebcam::writeFrame(Image& image)
{
    // bool success{true};
    if(!running_)
    {
        return false;
    }

    Profiler::getInstance().start("2", "Encode and write output image");

    unsigned long encodedSize{0u};

    // Encode and send to output
    if (!encoder_->encode(image, outputImage_, encodedSize))
    {
        return false;
    }

    // Use the actual compressed size for writing
    int written = write(fd_, outputImage_.data(), encodedSize);
    if (written < 0)
    {
        common::log_error("JPEGManager::encodeAndWriteToOutput - Cant write to output!");
        return false;
    }

    Profiler::getInstance().stop("2", "Encode and write output image");
    return true;
}
#endif
