#ifndef UI_H
#define UI_H

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <map>
#include <memory>
#include <queue>

#include "FunnyFace/GifReader.h"
#include "FunnyFace/cameraManager.h"
#include "imgui.h"

namespace funnyface
{

// Common resolution presets
struct Resolution
{
    unsigned int width;
    unsigned int height;
    const char* name;
};

static constexpr Resolution common_resolutions_[] = {
    {640,  480,  "640x480 (VGA) [4:3]"       },
    {800,  600,  "800x600 (SVGA) [4:3]"      },
    {1024, 768,  "1024x768 (XGA) [4:3]"      },
    {1280, 720,  "1280x720 (HD) [16:9]"      },
    {1280, 800,  "1280x800 (WXGA) [16:10]"   },
    {1280, 1024, "1280x1024 (SXGA) [5:4]"    },
    {1366, 768,  "1366x768 [16:9]"           },
    {1440, 900,  "1440x900 [16:10]"          },
    {1600, 900,  "1600x900 [16:9]"           },
    {1600, 1200, "1600x1200 (UXGA) [4:3]"    },
    {1920, 1080, "1920x1080 (Full HD) [16:9]"},
    {1920, 1200, "1920x1200 [16:10]"         },
    {2560, 1440, "2560x1440 (QHD) [16:9]"    },
    {3840, 2160, "3840x2160 (4K UHD) [16:9]" }
};

class UI
{
  public:
    UI();
    ~UI();

    // Initialize the UI system
    bool initialize(GLFWwindow* window, const char* glsl_version = "#version 130");

    inline void connect(std::shared_ptr<CameraManager> newCameraManager) { cameraManager_ = newCameraManager; }
    inline void connect(std::shared_ptr<GifReader> newGifReader) { gifReader_ = newGifReader; }

    // Cleanup the UI system
    void shutdown();

    // Start a new frame (call this at the beginning of your render loop)
    void newFrame();

    // Paint/render all UI elements (call this to draw your UI)
    void paint();

    // Render the final UI (call this after paint(), before swapping buffers)
    void render();

    void handleKeyboard();

  private:
    bool show_profiler_{true};
    bool show_input_config_{true};
    bool show_output_config_{true};
    bool maintain_aspect_ratio_{false};

    bool was_plus_tab_active_ = false;
    bool go_back_to_last_device_ = false;
    unsigned int last_device_tab_index_ = 0;
    std::unordered_map<std::string, std::shared_ptr<InputWebcam>> temp_modal_webcams_;

    // Window positioning tracking
    float current_y_{0.0f};

    std::shared_ptr<CameraManager> cameraManager_;
    std::shared_ptr<GifReader> gifReader_;


    // Device management
    bool show_device_config_ = false;
    int active_device_tab_ = 0;
    int requestedTab_ = 0;

    // Add device modal state
    bool show_add_device_modal_ = false;
    int selected_video_device_ = -1;
    char device_name_buffer_[256] = "";

    // Device configuration tracking
    std::map<std::string, int> selected_format_indices_;
    std::map<std::string, int> selected_size_indices_;
    std::map<std::string, int> selected_subsampling_;

    // Tracked camera state On/Off
    std::unordered_map<std::string, bool> cameraDesiredStates;

    // UI drawing functions
    void paintMainWindow();
    void paintAddDeviceModal();
    void paintDeviceConfigurationTabs();


    void paintGeneralizedDeviceConfig(std::shared_ptr<Webcam> camera);
    std::shared_ptr<Webcam> getCurrentCameraSharedPtr(const std::string& camera_key);
    // static void mouseCallback(GLFWwindow* window, double xpos, double ypos);

    static void mouseCallback(GLFWwindow* window, double xpos, double ypos)
    {
        UI* self = static_cast<UI*>(glfwGetWindowUserPointer(window));
        if (self)
        {
            // TODO: FIXME: adding the callback, we lost IMGUI interaction
            // common::log_error("Xpos %f.2 %f.2", xpos, ypos);
            self->gifReader_->move(xpos, ypos);
        }
    }
};
} // namespace funnyface

#endif // UI_H
