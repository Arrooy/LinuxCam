#ifndef APP_H
#define APP_H

#include <memory>

#include "LinuxFace/GifReader.h"
#include "LinuxFace/cameraManager.h"
#include "LinuxFace/detectors.h"
#include "LinuxFace/imageRenderGL.h"
#include "LinuxFace/onnx/fsanet.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/profiler.h"
#include "LinuxFace/ui.h"
#include "LinuxFace/window.h"
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/onnx/MODNet.h"
#include "LinuxFace/onnx/rvm.h"

namespace linuxface
{

class Application
{
  public:
    Application();
    ~Application();

    // Initialize the application
    bool initialize();

    // Run the main application loop
    void run();

    // Run a simple test with no loop.
    void runSingleShot();

    // Shutdown the application
    void shutdown();

  private:
    Window window_;
    UI ui_;

    std::shared_ptr<CameraManager> cameraManager_;
    ImageRenderGL imageRender_;

    Profiler& profiler_;

    std::unique_ptr<FaceDetector> faceDetector_;
    std::unique_ptr<FsanetDetector> fsanetDetectorVar_;
    std::unique_ptr<FsanetDetector> fsanetDetectorConv_;
    std::unique_ptr<SCRFDetector> scrfdDetector_;
    std::unique_ptr<MODNetDetector> modnetDetector_;
    std::unique_ptr<RobustVideoMatting> rvmDetector_;

    std::shared_ptr<GifReader> gif_;
    std::unique_ptr<Image> testImg_;

    // Main loop methods
    void update();
    void process(std::unique_ptr<Image>& image);
    void render();
};

} // namespace linuxface

#endif // APP_H
