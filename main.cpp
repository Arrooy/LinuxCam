#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring> // For memset
#include <string>

#include "JPEGManager.h"
#include "camera.h"
#include "math_utils.h"
#include "common.h"

using namespace funnyface;

// Both images are raw RGBA images.
void paint(Image& image)
{
    // Paint a circle in the middle of the image
    for (int x = 0; x < image.info.width; x++)
    {
        for (int y = 0; y < image.info.height; y++)
        {
            int pos = (x + y * image.info.width) * image.info.pixelSizeBytes;
            if(static_cast<unsigned long>(pos + image.info.pixelSizeBytes - 1) >= image.size)
            {
                common::log_warn("Paint - Invalid pos x,y %d&%d - index %d. Image size is %d. Skipping.",x,y, pos, image.size);
                continue;
            }

            image.data[pos] = 0;
            // image.data[pos + 1] = 0;
            image.data[pos + 2] = 0;
            image.data[pos + 3] = 0;
        }
    }
}

int main()
{
    common::init_logger("a0.0.0");
    CameraManager camera;
    if(!camera.configureVirtualOuputCamera("/dev/video8", 640L, 480L))
    {
        common::log_error("Cannot proceed. Aborting.");
        return 1;
    }

    if(!camera.configureInputCamera("/dev/video0", 640L, 480L))
    {
        common::log_error("Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return 1;
    }

    if(!camera.configureInputBuffers(4))
    {
        common::log_error("Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return 1;
    }

    std::shared_ptr<JPEGManager> jpegManager = std::make_shared<JPEGManager>(camera.getOutputFd(), 640L, 480L, TJSAMP::TJSAMP_422);
    camera.setJPEGManager(jpegManager);

    if(!camera.update(paint))
    {
        common::log_error("Main - Cannot proceed. Aborting.");
        return 1;
    }

    common::log_info("Finished with no problems!");
}
