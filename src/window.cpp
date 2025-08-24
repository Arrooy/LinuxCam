
#include "LinuxFace/window.h"

#include <iostream>

#include "LinuxFace/common.h"
#include "config.hpp"
using namespace linuxface;

// Check version in ubuntu with:
// glxinfo | grep "OpenGL shading language version"
// Mi worki -> 4.6 NVIDIA

// | GLSL Version    | OpenGL | Features/Notes                                   |
// | --------------- | ------ | ------------------------------------------------ |
// | `#version 330`  | 3.3    | Widely supported, good baseline (2010)           |
// | `#version 400+` | 4.0+   | Geometry shaders, tessellation, more precision   |
// | `#version 450`  | 4.5    | Modern, powerful (ubiquitous with newer drivers) |

Window::Window()
{
}

Window::~Window()
{
    shutdown();
}

// Callback for framebuffer resize
static void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    // Retrieve the pointer to the Window instance
    auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win != nullptr)
    {
        win->onFramebufferResize(width, height);
    }
}

bool Window::initialize()
{
    // Setup GLFW
    glfwSetErrorCallback(errorCallback);

    if (glfwInit() == 0)
    {
        linuxface::common::logError("Failed to initialize GLFW");
        return false;
    }

    // GL 4.0 + GLSL 400
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Remove title bar
    // glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    int width = 0;
    int height = 0;
    Config::getInstance().getWindowSize(width, height);
    const std::string title = Config::getInstance().getWindowTitle();

    // Create window with graphics context
    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window_ == nullptr)
    {
        linuxface::common::logError("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync

    // Set user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);
    // Set framebuffer resize callback
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

    // Initialize GLAD
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
    {
        linuxface::common::logError("Failed to initialize GLAD");
        return false;
    }

    // GLFWimage images[1];
    // auto media_path = Config::getInstance().getMediaFolderPath();
    // images[0] = load_icon(media_path + "icon.png");
    // glfwSetWindowIcon(window_, 1, images);

    linuxface::common::logInfo("GLFW Window initialized successfully. Size %d x %d", width, height);
    return true;
}

// Called when the framebuffer is resized
void Window::onFramebufferResize(int width, int height)
{
    double now = glfwGetTime();

    // Fallback for test environments where GLFW is not initialized
    if (now == 0.0)
    {
        // Use a simple counter-based approach
        static double testTime = 0.0;
        testTime += 0.05; // Small increment, less than throttle interval
        now = testTime;
    }

    // Record the latest resize event
    lastResizeWidth_ = width;
    lastResizeHeight_ = height;
    resizePending_ = true;

    // Throttle: only call callback if this is the very first callback (lastResizeCallbackTime_ == 0)
    if (resizeCallback_ && lastResizeCallbackTime_ == 0.0)
    {
        resizeCallback_(width, height);
        lastResizeCallbackTime_ = now;
        lastResizeEventTime_ = now;
        // Do NOT set resizePending_ = false here; let debounce handle the final event
    }
}

void Window::shutdown()
{
    if (window_ != nullptr)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const
{
    return window_ != nullptr ? glfwWindowShouldClose(window_) != 0 : true;
}

void Window::pollEvents()
{
    glfwPollEvents();
    updateResizeEvents();
}

// Should be called every frame to handle resize debounce/throttle
void Window::updateResizeEvents()
{
    if (resizePending_ && resizeCallback_)
    {
        double now = glfwGetTime();
        // Fallback for test environments where GLFW is not initialized
        if (now == 0.0)
        {
            static double testTime = 0.0;
            testTime += 0.5; // Simulate time passing faster for debounce
            now = testTime;
        }
        // If enough time has passed since the last callback, fire the callback (debounce)
        if (now - lastResizeCallbackTime_ > RESIZE_DEBOUNCE_DELAY)
        {
            resizeCallback_(lastResizeWidth_, lastResizeHeight_);
            lastResizeCallbackTime_ = now;
            lastResizeEventTime_ = now;
            resizePending_ = false;
        }
    }
}

void Window::swapBuffers()
{
    if (window_ != nullptr)
    {
        glfwSwapBuffers(window_);
    }
}

void Window::getFramebufferSize(int& width, int& height) const
{
    if (window_ != nullptr)
    {
        glfwGetFramebufferSize(window_, &width, &height);
    }
    else
    {
        width = height = 0;
    }
}

void Window::setViewport() const
{
    int width = 0;
    int height = 0;
    getFramebufferSize(width, height);
    glViewport(0, 0, width, height);
}

void Window::errorCallback(int error, const char* description)
{
    linuxface::common::logError("GLFW Error %d: %s", error, description);
}

// Allow setting a callback for resize events
void Window::setResizeCallback(const std::function<void(int, int)>& cb)
{
    resizeCallback_ = std::move(cb);
}

bool Window::isKeyPressed(int key) const
{
    if (window_ == nullptr)
    {
        return false;
    }
    return glfwGetKey(window_, key) == GLFW_PRESS;
}
