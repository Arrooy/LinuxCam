#ifndef WINDOW_H
#define WINDOW_H
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on
#include <functional>
#include <string>

class Window
{
  public:
    Window();
    ~Window();

    // Initialize GLFW and create window
    bool initialize();

    // Cleanup GLFW and destroy window
    void shutdown();

    // Check if window should close
    bool shouldClose() const;

    // Poll events and swap buffers
    void pollEvents();

    // Should be called every frame to handle resize debounce/throttle
    void updateResizeEvents();
    void swapBuffers();

    // Get window dimensions
    void getFramebufferSize(int& width, int& height) const;

    // Get the GLFW window pointer
    GLFWwindow* getGLFWWindow() const { return window_; }

    // Get OpenGL version string
    const char* getGLSLVersion() const { return glslVersion_; }

    // Set viewport to match framebuffer size
    void setViewport();
    bool isKeyPressed(int key) const;

    // Called by GLFW on framebuffer resize
    void onFramebufferResize(int width, int height);

    // Set a callback to be called on framebuffer resize
    void setResizeCallback(std::function<void(int, int)> cb);

  private:
    GLFWwindow* window_;
    const char* glslVersion_;
    std::function<void(int, int)> resizeCallback_;

    // For resize throttling/debouncing
    int lastResizeWidth_ = 0;
    int lastResizeHeight_ = 0;
    double lastResizeCallbackTime_ = 0.0;
    double lastResizeEventTime_ = 0.0;
    bool resizePending_ = false;
    static constexpr double RESIZE_THROTTLE_INTERVAL = 0.1; // seconds
    static constexpr double RESIZE_DEBOUNCE_DELAY = 0.35;   // seconds

    // GLFW error callback
    static void errorCallback(int error, const char* description);
};

#endif // WINDOW_H
