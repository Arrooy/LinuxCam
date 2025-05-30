#ifndef WINDOW_H
#define WINDOW_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
    void swapBuffers();


    // Get window dimensions
    void getFramebufferSize(int& width, int& height) const;

    // Get the GLFW window pointer
    GLFWwindow* getGLFWWindow() const { return window_; }

    // Get OpenGL version string
    const char* getGLSLVersion() const { return glslVersion_; }

    // Set viewport to match framebuffer size
    void setViewport();

  private:
    GLFWwindow* window_;
    const char* glslVersion_;

    // GLFW error callback
    static void errorCallback(int error, const char* description);
};

#endif // WINDOW_H
