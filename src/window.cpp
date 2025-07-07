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

Window::Window() : window_(nullptr), glslVersion_("#version 400")
{
}

Window::~Window()
{
    shutdown();
}

// Callback for framebuffer resize
static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // Retrieve the pointer to the Window instance
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win)
    {
        win->onFramebufferResize(width, height);
    }
}

bool Window::initialize()
{
    // Setup GLFW
    glfwSetErrorCallback(errorCallback);

    if (!glfwInit())
    {
        common::log_error("Failed to initialize GLFW");
        return false;
    }

    // GL 4.0 + GLSL 400
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Remove title bar
    // glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    int width, height;
    Config::getInstance().getWindowSize(width, height);
    std::string title = Config::getInstance().getWindowTitle();

    // Create window with graphics context
    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window_ == nullptr)
    {
        common::log_error("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync

    // Set user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);
    // Set framebuffer resize callback
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);

    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress))
    {
        common::log_error("Failed to initialize GLAD");
        return false;
    }

    // GLFWimage images[1];
    // auto media_path = Config::getInstance().getMediaFolderPath();
    // images[0] = load_icon(media_path + "icon.png");
    // glfwSetWindowIcon(window_, 1, images);

    common::log_info("GLFW Window initialized successfully. Size %d x %d", width, height);
    return true;
}

// Called when the framebuffer is resized
void Window::onFramebufferResize(int width, int height)
{
    double now = glfwGetTime();

    // Record the latest resize event
    lastResizeWidth_ = width;
    lastResizeHeight_ = height;
    lastResizeEventTime_ = now;
    resizePending_ = true;

    // Throttle: only call callback if enough time has passed since last callback
    if (resizeCallback_ && (now - lastResizeCallbackTime_ > RESIZE_THROTTLE_INTERVAL))
    {
        resizeCallback_(width, height);
        lastResizeCallbackTime_ = now;
    }
}

void Window::shutdown()
{
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const
{
    return window_ ? glfwWindowShouldClose(window_) : true;
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
        // If enough time has passed since last event, fire the callback (debounce)
        if (now - lastResizeEventTime_ > RESIZE_DEBOUNCE_DELAY)
        {
            resizeCallback_(lastResizeWidth_, lastResizeHeight_);
            lastResizeCallbackTime_ = now;
            resizePending_ = false;
        }
    }
}

void Window::swapBuffers()
{
    if (window_)
    {
        glfwSwapBuffers(window_);
    }
}

void Window::getFramebufferSize(int& width, int& height) const
{
    if (window_)
    {
        glfwGetFramebufferSize(window_, &width, &height);
    }
    else
    {
        width = height = 0;
    }
}

void Window::setViewport()
{
    int width, height;
    getFramebufferSize(width, height);
    glViewport(0, 0, width, height);
}

void Window::errorCallback(int error, const char* description)
{
    common::log_error("GLFW Error %d: %s", error, description);
}

// Allow setting a callback for resize events
void Window::setResizeCallback(std::function<void(int, int)> cb)
{
    resizeCallback_ = std::move(cb);
}
