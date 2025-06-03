#ifndef UI_H
#define UI_H

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <memory>
#include <queue>

#include "camera.h"
#include "imgui.h"

namespace funnyface
{
class Profiler; // Forward declaration

class UI
{
  public:
    UI();
    ~UI();

    // Initialize the UI system
    bool initialize(GLFWwindow* window, const char* glsl_version = "#version 130");

    inline void connect(std::shared_ptr<CameraManager> newCameraManager) { cameraManager_ = newCameraManager; }

    // Cleanup the UI system
    void shutdown();

    // Start a new frame (call this at the beginning of your render loop)
    void newFrame();

    // Paint/render all UI elements (call this to draw your UI)
    void paint();

    // Render the final UI (call this after paint(), before swapping buffers)
    void render();

  private:
    bool show_profiler_{true};
    bool show_input_config_{true};
    bool show_output_config_{false};
    funnyface::Profiler& profiler_;

    std::shared_ptr<CameraManager> cameraManager_;
    
    // Window positioning tracking
    float current_y_{0.0f};
    
    // UI drawing functions
    void paintMainWindow();
    void paintDebugWindow();
    void paintInputDeviceConfig(CapturingDevice& device);
    void paintOutputDeviceConfig(CapturingDevice& device);
};
} // namespace funnyface

#endif // UI_H
