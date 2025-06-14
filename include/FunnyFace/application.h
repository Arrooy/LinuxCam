#ifndef APP_H
#define APP_H

#include <memory>

#include "FunnyFace/GifReader.h"
#include "FunnyFace/cameraManager.h"
#include "FunnyFace/detectors.h"
#include "FunnyFace/imageRenderGL.h"
#include "FunnyFace/profiler.h"
#include "FunnyFace/ui.h"
#include "FunnyFace/window.h"
#include "FunnyFace/onnx/fsanet.h"
namespace funnyface
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

    // Shutdown the application
    void shutdown();

  private:
    Window window_;
    UI ui_;

    std::shared_ptr<CameraManager> cameraManager_;
    ImageRenderGL imageRender_;

    Profiler& profiler_;

    std::unique_ptr<FaceDetector> faceDetector_;
    std::unique_ptr<FsanetDetector> fsanetDetector_;

    std::shared_ptr<GifReader> gif_;

    // Main loop methods
    void update();
    void process(std::unique_ptr<Image>& image);
    void render();
};

} // namespace funnyface

#endif // APP_H
