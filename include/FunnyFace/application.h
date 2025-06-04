#ifndef APP_H
#define APP_H

#include "FunnyFace/camera.h"
#include "FunnyFace/imageRenderGL.h"
#include "FunnyFace/ui.h"
#include "FunnyFace/window.h"
#include "FunnyFace/detectors.h"
#include "FunnyFace/profiler.h"
#include <memory>

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

    std::unique_ptr<CameraManager> cameraManager_;
    ImageRenderGL imageRender_;

    Profiler& profiler_;

    std::unique_ptr<FaceDetector> faceDetector_;

    // Main loop methods
    void update();
    void process(Image& image);
    void render();
};

} // namespace funnyface

#endif // APP_H
