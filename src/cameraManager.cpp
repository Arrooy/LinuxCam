#include "FunnyFace/cameraManager.h"

#include "FunnyFace/common.h"
#include "FunnyFace/profiler.h"

using namespace funnyface;

CameraManager::CameraManager()
{
}

CameraManager::~CameraManager()
{
    shutdown();
}

void CameraManager::shutdown()
{
    for (const auto& camera : inWebcam_)
    {
        camera->stop();
    }
    inWebcam_.clear();

    for (const auto& camera : outWebcam_)
    {
        camera->stop();
    }
    outWebcam_.clear();
}

bool CameraManager::updateInput(std::unique_ptr<Image>& outputImage)
{
    for (auto& input : inWebcam_)
    {
        if (!input->isRunning())
        {
            if (!input->start())
            {
                common::log_error("CameraManager::updateInput - Failed to start input device %s",
                                  input->getDevicePath().c_str());
                return false;
            }
        }

        Image* inImage = nullptr;

        if (!input->getImage(inImage))
        {
            // Input device doesn't have an image for us yet.
            continue;
        }
        else if (inImage == nullptr)
        {
            common::log_error("CameraManager::updateInput - Input image is null");
            return false;
        }
        else if (inImage->info.width == 0 || inImage->info.height == 0)
        {
            common::log_error("CameraManager::updateInput - Input image invalid size size: %d x %d",
                              inImage->info.width, inImage->info.height);
            inImage->setBeingUsed(false);
            continue;
        }

        // Valid image, copy it to output image
        outputImage = inImage->deepCopy(); // TODO: Instead of coping, we would need to merge them.
        inImage->setBeingUsed(false);

        if (outputImage == nullptr)
        {
            common::log_error("CameraManager::updateInput - Failed to create outputImage");
            continue;
        }

        break;
    }

    return outputImage != nullptr;
}

bool CameraManager::updateOutput(std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("1", "Encode and write all output images");

    if (!image)
    {
        common::log_error("No image to encode and write to output");
        return false;
    }
    bool success = true;
    for (auto& output : outWebcam_)
    {
        if (!output->isRunning())
        {
            if (!output->start())
            {
                common::log_error("Failed to start output device %s", output->getDevicePath().c_str());
                success = false;
                break;
            }
        }
        if (!output->writeFrame(*image))
        {
            common::log_error("Failed to write frame to output device %s", output->getDevicePath().c_str());
            success = false;
        }
        break;
    }

    // Release the image
    image->setBeingUsed(false);
    Profiler::getInstance().stop("1", "Encode and write all output images");
    return success;
}

bool CameraManager::addCamera(std::shared_ptr<Webcam> camera)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return addCameraImpl(inWebcam_, std::move(input));
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return addCameraImpl(outWebcam_, std::move(output));
    }

    common::log_error("CameraManager::addCamera - Unknown webcam type");
    return false;
}


bool CameraManager::removeCamera(std::shared_ptr<Webcam> camera)
{
    const std::string& devicePath = camera->getDevicePath();

    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return removeCameraImpl(inWebcam_, devicePath);
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return removeCameraImpl(outWebcam_, devicePath);
    }

    common::log_error("CameraManager::removeCamera unknown webcam type");
    return false;
}

bool CameraManager::updateCamera(std::shared_ptr<Webcam> camera)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return updateCameraImpl(inWebcam_, std::move(input));
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return updateCameraImpl(outWebcam_, std::move(output));
    }

    common::log_error("CameraManager::updateCamera unknown webcam type");
    return false;
}
