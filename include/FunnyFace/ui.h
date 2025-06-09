#ifndef UI_H
#define UI_H

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <memory>
#include <queue>

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
    bool show_output_config_{true};
    bool maintain_aspect_ratio_{false};

    // Window positioning tracking
    float current_y_{0.0f};

    std::shared_ptr<CameraManager> cameraManager_;


    // Device management
    bool show_device_config_ = false;
    int active_device_tab_ = 0;
    bool show_add_device_modal_ = false;

    // Add device modal state
    std::vector<std::string> available_video_devices_;
    int selected_video_device_ = -1;
    char device_name_buffer_[256] = "";
    bool is_output_device_{false};

    // UI drawing functions
    void paintMainWindow();
    void paintAddDeviceModal();
    void paintDeviceConfigurationTabs();

    void paintGeneralizedDeviceConfig(Webcam& camera);

};
} // namespace funnyface

#endif // UI_H
