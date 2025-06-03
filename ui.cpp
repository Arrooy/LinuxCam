#include "ui.h"

#include "common.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "profiler.h"

using namespace funnyface;

UI::UI() : profiler_(Profiler::getInstance())
{
}

UI::~UI()
{
    shutdown();
}

bool UI::initialize(GLFWwindow* window, const char* glsl_version)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight(); // Alternative style

    // Setup Platform/Renderer backends
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
    {
        common::log_error("Failed to initialize ImGui GLFW backend");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init(glsl_version))
    {
        common::log_error("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }

    common::log_info("UI initialized successfully");
    return true;
}

void UI::shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UI::newFrame()
{
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UI::paint()
{
    ImGui::ShowDemoWindow();

    // Paint all UI windows
    paintMainWindow();
}

void UI::render()
{
    // Render everything
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UI::paintMainWindow()
{
    if (ImGui::BeginMainMenuBar())
    {
        // Menu bar
        if (ImGui::BeginMenu("More Options..."))
        {
            ImGui::MenuItem("Toggle Profiler", NULL, &show_profiler_);
            ImGui::MenuItem("Toggle Input Device", NULL, &show_input_config_);
            ImGui::MenuItem("Toggle Output Device", NULL, &show_output_config_);

            // ImGui::Separator();
            // if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Calculate base position for window stacking
    float menu_bar_height = ImGui::GetFrameHeight();
    current_y_ = menu_bar_height;

    // Render profiler window
    if (show_profiler_)
    {
        ImGui::SetNextWindowPos(ImVec2(0, current_y_), ImGuiCond_Always);
        ImGui::Begin("Profiler", &show_profiler_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

        // TODO: Change color base on hardcoded thresholds.
        auto textColor = ImVec4(0.0f, 0.7f, 1.0f, 1.0f);
        auto durations = profiler_.getDurations();
        for (const auto& pair : durations)
        {
            ImGui::TextColored(textColor, "%s - %s", pair.first.c_str(),
                               Profiler::format_duration(pair.second).c_str());
        }

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

        // TODO: Make a sliding window fo this graph.
        //  static float arr[] = {0.6f, 0.1f, 1.0f, 0.5f, 0.92f, 0.1f, 0.2f};
        //  ImGui::PlotHistogram("Histogram", arr, IM_ARRAYSIZE(arr), 0, NULL, 0.0f, 1.0f, ImVec2(0, 50.0f));

        // Add close button
        if (ImGui::Button("Close"))
        {
            show_profiler_ = false;
        }
        // Get window size while we're inside the window
        ImVec2 window_size = ImGui::GetWindowSize();
        current_y_ += window_size.y;
        ImGui::End();
    }

    // Render input device window
    if (show_input_config_ && cameraManager_)
    {
        ImGui::SetNextWindowPos(ImVec2(0, current_y_), ImGuiCond_Always);
        paintInputDeviceConfig(cameraManager_->getInputDevice());
    }

    // Render output device window
    if (show_output_config_ && cameraManager_)
    {
        ImGui::SetNextWindowPos(ImVec2(0, current_y_), ImGuiCond_Always);
        paintOutputDeviceConfig(cameraManager_->getOutputDevice());
    }
}

void UI::paintInputDeviceConfig(CapturingDevice& device)
{
    ImGui::Begin("Input Device", &show_input_config_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // Device Information
    if (ImGui::CollapsingHeader("Device Information", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Name: %s", device.name.c_str());
        ImGui::Text("Path: %s", device.device_path.c_str());
        ImGui::Text("FD: %d", device.fd);
        ImGui::Text("Driver: %s", device.caps.driver.c_str());
        ImGui::Text("Card: %s", device.caps.card.c_str());
        ImGui::Text("Bus Info: %s", device.caps.bus_info.c_str());
    }

    // Current Settings
    if (ImGui::CollapsingHeader("Current Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Resolution: %ux%u", device.width, device.height);
        ImGui::Text("Buffer Count: %u", device.buffer_count);
    }

    // Available Formats
    if (ImGui::CollapsingHeader("Available Formats"))
    {
        static int selected_format = -1;
        static int selected_size = -1;

        for (std::size_t fmt_idx = 0; fmt_idx < device.caps.formats.size(); fmt_idx++)
        {
            const Format& format = device.caps.formats[fmt_idx];

            if (ImGui::TreeNode(("Format " + std::to_string(fmt_idx)).c_str()))
            {
                ImGui::Text("Description: %s", format.description.c_str());
                ImGui::Text("Pixel Format: 0x%08X", format.pixelformat);

                if (ImGui::TreeNode("Available Sizes"))
                {
                    for (std::size_t size_idx = 0; size_idx < format.sizes.size(); size_idx++)
                    {
                        const FrameSize& size = format.sizes[size_idx];
                        bool is_current = (device.width == size.width && device.height == size.height);

                        ImGui::PushID(fmt_idx * 1000 + size_idx);
                        if (ImGui::Selectable((std::to_string(size.width) + "x" + std::to_string(size.height)).c_str(),
                                              is_current))
                        {
                            device.width = size.width;
                            device.height = size.height;
                            selected_format = fmt_idx;
                            selected_size = size_idx;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }

        if (selected_format >= 0 && selected_size >= 0)
        {
            ImGui::Separator();
            ImGui::Text("Selected: %s - %ux%u", device.caps.formats[selected_format].description.c_str(), device.width,
                        device.height);
        }
    }

    // Apply Changes Button
    ImGui::Separator();
    if (ImGui::Button("Apply Changes"))
    {
        if (cameraManager_)
        {
            common::log_info("UI::paintInputDeviceConfig - Applying camera configuration changes");
            cameraManager_->reconfigureInputCamera();
        }
        else
        {
            common::log_error("UI::paintInputDeviceConfig - Camera manager not available");
        }
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to apply resolution and buffer changes");


    // Update position for next window
    current_y_ += ImGui::GetWindowSize().y;

    ImGui::End();
}

void UI::paintOutputDeviceConfig(CapturingDevice& device)
{
    ImGui::Begin("Output Device", &show_output_config_,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // Device Information
    if (ImGui::CollapsingHeader("Device Information", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Name: %s", device.name.c_str());
        ImGui::Text("Path: %s", device.device_path.c_str());
        ImGui::Text("FD: %d", device.fd);
    }

    // Current Settings
    if (ImGui::CollapsingHeader("Current Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Resolution: %ux%u", device.width, device.height);

        // Subsampling selection
        const char* subsampling_options[] = {"4:4:4", "4:2:2", "4:2:0", "GRAY", "4:4:0", "4:1:1"};
        int current_subsampling = static_cast<int>(device.subsampling);

        if (ImGui::Combo("Subsampling", &current_subsampling, subsampling_options, IM_ARRAYSIZE(subsampling_options)))
        {
            device.subsampling = static_cast<TJSAMP>(current_subsampling);
        }

        ImGui::Text("Current: %s", subsampling_options[current_subsampling]);
    }

    // Resolution Settings
    if (ImGui::CollapsingHeader("Resolution Settings"))
    {
        // Maintain aspect ratio toggle
        ImGui::Checkbox("Maintain Input Device Aspect Ratio", &maintain_aspect_ratio_);

        if (maintain_aspect_ratio_ && cameraManager_)
        {
            auto& input_device = cameraManager_->getInputDevice();
            float input_aspect = static_cast<float>(input_device.width) / static_cast<float>(input_device.height);
            ImGui::Text("Input Aspect Ratio: %.3f (%ux%u)", input_aspect, input_device.width, input_device.height);
        }

        ImGui::Separator();

        // Find current resolution index
        static int current_resolution_index = -1;
        static int custom_width = device.width;
        static int custom_height = device.height;

        // Check if current resolution matches any preset
        current_resolution_index = -1;
        for (int i = 0; i < IM_ARRAYSIZE(common_resolutions_); i++)
        {
            if (common_resolutions_[i].width == device.width && common_resolutions_[i].height == device.height)
            {
                current_resolution_index = i;
                break;
            }
        }

        // Resolution preset combobox
        const char* preview_text =
            (current_resolution_index >= 0) ? common_resolutions_[current_resolution_index].name : "Custom Resolution";

        if (ImGui::BeginCombo("Resolution Preset", preview_text))
        {
            // Add common resolutions
            for (int i = 0; i < IM_ARRAYSIZE(common_resolutions_); i++)
            {
                bool is_compatible = true;

                // Check aspect ratio compatibility if maintaining aspect ratio
                if (maintain_aspect_ratio_ && cameraManager_)
                {
                    auto& input_device = cameraManager_->getInputDevice();
                    float input_aspect =
                        static_cast<float>(input_device.width) / static_cast<float>(input_device.height);
                    float preset_aspect = static_cast<float>(common_resolutions_[i].width)
                                          / static_cast<float>(common_resolutions_[i].height);

                    // Allow small tolerance for aspect ratio matching
                    const float tolerance = 0.01f;
                    is_compatible = (std::abs(input_aspect - preset_aspect) < tolerance);
                }

                // Skip incompatible resolutions entirely
                if (!is_compatible)
                {
                    continue;
                }

                bool is_selected = (current_resolution_index == i);

                if (ImGui::Selectable(common_resolutions_[i].name, is_selected))
                {
                    device.width = common_resolutions_[i].width;
                    device.height = common_resolutions_[i].height;
                    current_resolution_index = i;
                    custom_width = device.width;
                    custom_height = device.height;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            // Custom resolution option
            ImGui::Separator();
            bool is_custom_selected = (current_resolution_index == -1);
            if (ImGui::Selectable("Custom Resolution", is_custom_selected))
            {
                current_resolution_index = -1;
            }

            ImGui::EndCombo();
        }

        // Custom resolution inputs (only show if custom is selected or no preset matches)
        if (current_resolution_index == -1)
        {
            ImGui::Separator();
            ImGui::Text("Custom Resolution:");

            if (ImGui::InputInt("Width", &custom_width))
            {
                if (custom_width > 0)
                {
                    device.width = custom_width;

                    // Auto-adjust height if maintaining aspect ratio
                    if (maintain_aspect_ratio_ && cameraManager_)
                    {
                        auto& input_device = cameraManager_->getInputDevice();
                        float input_aspect =
                            static_cast<float>(input_device.width) / static_cast<float>(input_device.height);
                        custom_height = (int) (custom_width / input_aspect);
                        device.height = custom_height;
                    }
                }
            }
        }

        ImGui::Text("Current: %ux%u", device.width, device.height);
    }

    // Apply Changes Button
    ImGui::Separator();
    if (ImGui::Button("Apply Changes"))
    {
        if (cameraManager_)
        {
            common::log_info("UI::paintOutputDeviceConfig - Applying output camera configuration changes");
            cameraManager_->reconfigureOutputCamera();
        }
        else
        {
            common::log_error("UI::paintOutputDeviceConfig - Camera manager not available");
        }
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to apply resolution and subsampling changes");

    // Update position for next window
    current_y_ += ImGui::GetWindowSize().y;

    ImGui::End();
}
