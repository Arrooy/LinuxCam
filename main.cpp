#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring> // For memset
#include <string>

#include "JPEGManager.h"
#include "camera.h"
#include "math_utils.h"
#include "common.h"
#include "detectors.h"
#include "face.h"
#include <dlib/dnn.h>
// Dlib and this size, we get 20ms paint. 2ms Compress/Decompress.
#define CAMERA_WIDTH 640L
#define CAMERA_HEIGHT 480L

using namespace funnyface;

// Both images are raw RGBA images.
void paint(Image& image, std::shared_ptr<FaceDetector> faceDetector)
{

    if(faceDetector != nullptr)
    {
        auto faces_rect = faceDetector->detect(image);
        if(faces_rect.size() > 0)
        {
            // common::log_info("Paint - Found %d faces at %d,%d,%d,%d", faces_rect.size(), faces_rect[0].l, faces_rect[0].t, faces_rect[0].r, faces_rect[0].b);
            for (const auto& face_rect : faces_rect)
            {
                Face face(face_rect);
                face.paintBoundingBox(image);
            }
        }
    }

    // // Paint a circle in the middle of the image
    // for (int x = 0; x < image.info.width; x++)
    // {
    //     for (int y = 0; y < image.info.height; y++)
    //     {
    //         int pos = (x + y * image.info.width) * image.info.pixelSizeBytes;
    //         if(static_cast<unsigned long>(pos + image.info.pixelSizeBytes - 1) >= image.size)
    //         {
    //             common::log_warn("Paint - Invalid pos x,y %d&%d - index %d. Image size is %d. Skipping.",x,y, pos, image.size);
    //             continue;
    //         }

    //         image.data[pos] = 0;
    //         image.data[pos + 1] = 0;
    //         image.data[pos + 2] = 0;
    //         image.data[pos + 3] = 0;
    //     }
    // }
}

int main()
{
    common::init_logger("a0.0.0");
    common::log_info("Num of cuda devices: %d", dlib::cuda::get_num_devices());

    CameraManager camera;
    if(!camera.configureVirtualOuputCamera("/dev/video8", CAMERA_WIDTH, CAMERA_HEIGHT))
    {
        common::log_error("Cannot proceed. Aborting.");
        return 1;
    }

    if(!camera.configureInputCamera("/dev/video0", CAMERA_WIDTH, CAMERA_HEIGHT))
    {
        common::log_error("Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return 1;
    }

    if(!camera.configureInputBuffers(2))
    {
        common::log_error("Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return 1;
    }

    std::shared_ptr<JPEGManager> jpegManager = std::make_shared<JPEGManager>(camera.getOutputFd(), CAMERA_WIDTH, CAMERA_HEIGHT, TJSAMP::TJSAMP_444);
    camera.setJPEGManager(jpegManager);

    if(!camera.configureFaceDetector())
    {
        common::log_error("Cannot proceed. Aborting.");
        return 1;
    }

    if(!camera.update(paint))
    {
        common::log_error("Main - Cannot proceed. Aborting.");
        return 1;
    }

    common::log_info("Finished with no problems!");
}
