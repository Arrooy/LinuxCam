#ifndef UI_H
#define UI_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include <queue>

namespace funnyface {
    class Profiler; // Forward declaration
}
class UI
{
  public:
    UI();
    ~UI();

    // Initialize the UI system
    bool initialize(GLFWwindow* window, const char* glsl_version = "#version 130");

    // Cleanup the UI system
    void shutdown();

    // Start a new frame (call this at the beginning of your render loop)
    void newFrame();

    // Paint/render all UI elements (call this to draw your UI)
    void paint();

    // Render the final UI (call this after paint(), before swapping buffers)
    void render();

  private:

    bool show_profile_window_{true};
    funnyface::Profiler& profiler_;
    // UI drawing functions
    void paintMainWindow();
    void paintDebugWindow();
};

// class Histogram 
// {
//   public:

//   private:
//    std::queue<std::chrono::microseconds> data_;
// };

#endif // UI_H
