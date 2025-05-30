#ifndef APP_H
#define APP_H

#include "camera.h"
#include "imageRenderGL.h"
#include "ui.h"
#include "window.h"
#include "detectors.h"
#include "profiler.h"


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
    CameraManager cameraManager_;
    ImageRenderGL imageRender_;

    Profiler& profiler_;

    std::shared_ptr<FaceDetector> faceDetector_ptr_;

    // Main loop methods
    void update();
    void process(Image& image);
    void render();
};

} // namespace funnyface

#endif // APP_H
