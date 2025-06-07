#include "FunnyFace/ui.h"

#include "FunnyFace/common.h"
#include "FunnyFace/profiler.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using namespace funnyface;

UI::UI()
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
        if (ImGui::BeginMenu("More Options..."))
        {
            ImGui::MenuItem("Toggle Profiler", NULL, &show_profiler_);
            ImGui::MenuItem("Toggle Device Configuration", NULL, &show_device_config_);
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

        auto textColor = ImVec4(0.0f, 0.7f, 1.0f, 1.0f);
        auto durations = Profiler::getInstance().getDurations();
        for (const auto& pair : durations)
        {
            ImGui::TextColored(textColor, "%s - %s", pair.first.c_str(),
                               Profiler::format_duration(pair.second).c_str());
        }

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

        if (ImGui::Button("Close"))
        {
            show_profiler_ = false;
        }

        ImVec2 window_size = ImGui::GetWindowSize();
        current_y_ += window_size.y;
        ImGui::End();
    }

    // Render device configuration window with tabs
    if (show_device_config_)
    {
        ImGui::SetNextWindowPos(ImVec2(0, current_y_), ImGuiCond_Always);
        paintDeviceConfigurationTabs();
    }

    // Render add device modal
    if (show_add_device_modal_)
    {
        paintAddDeviceModal();
    }
}

void UI::paintDeviceConfigurationTabs()
{
    // Calculate available space
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float available_height = viewport->WorkSize.y - current_y_ - 10; // 10px padding from bottom

    // Set window size constraints
    ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 200),             // Minimum size
                                        ImVec2(-1, available_height) // Maximum size (limited by available space)
    );

    ImGui::Begin("Device Configuration", &show_device_config_,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                     | ImGuiWindowFlags_NoMove);


    if (ImGui::BeginTabBar("DeviceTabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable))
    {
        const auto& managed_webcams = cameraManager_->getWebcams();
        unsigned int tab_index{0u};
        // Render tabs for existing devices
        for (const auto& webcam : managed_webcams)
        {
            std::string tab_name = webcam->getName();

            // Add webcam type indicator
            tab_name += webcam->getType() == WebcamType::PhysicalInput ? " (IN)" : " (OUT)";

            // Add close button to tab
            bool tab_open = true;
            if (ImGui::BeginTabItem((tab_name + "###tab" + std::to_string(tab_index++)).c_str(), &tab_open))
            {
                paintGeneralizedDeviceConfig(*webcam);
                ImGui::EndTabItem();
            }

            // Handle tab closing
            // if (!tab_open && managed_devices_.size() > 1) // Keep at least one device
            // {
            //     managed_devices_.erase(managed_devices_.begin() + i);
            //     if (i < device_is_output_.size())
            //     {
            //         device_is_output_.erase(device_is_output_.begin() + i);
            //     }
            //     if (active_device_tab_ >= managed_devices_.size())
            //     {
            //         active_device_tab_ = managed_devices_.size() - 1;
            //     }
            //     break; // Break to avoid iterator invalidation
            // }
        }

        // Add new device button as trailing tab
        if (ImGui::BeginTabItem("+", nullptr, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
        {
            // This tab content won't be shown, we just use it as a button
            ImGui::EndTabItem();

            // Open add device modal when + tab is clicked
            show_add_device_modal_ = true;
            available_video_devices_.clear();
            available_video_devices_.push_back("/dev/arroyo!");
        }

        ImGui::EndTabBar();
    }

    // Update position for next window
    current_y_ += ImGui::GetWindowSize().y;
    ImGui::End();
}


void UI::paintGeneralizedDeviceConfig(Webcam& camera)
{
    // Device Information
    if (ImGui::CollapsingHeader("Device Information"))
    {
        ImGui::Text("Name: %s", camera.getName().c_str());
        ImGui::Text("Path: %s", camera.getDevicePath().c_str());
        CameraCapabilities capabilities = camera.getCapabilities();
        if (!capabilities.driver.empty())
        {
            ImGui::Text("Driver: %s", capabilities.driver.c_str());
            ImGui::Text("Card: %s", capabilities.card.c_str());
            ImGui::Text("Bus Info: %s", capabilities.bus_info.c_str());
        }
    }

    // Current Settings
    if (ImGui::CollapsingHeader("Current Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Format usedFormat = camera.getSelectedFormat();
        FrameSize selectedFrameSize = usedFormat.sizes[usedFormat.selectedFrameSize];
        ImGui::Text("Format: %s", usedFormat.description.c_str());
        ImGui::Text("Resolution: %ux%u", selectedFrameSize.width, selectedFrameSize.height);
        ImGui::Text("Pixel format %u", usedFormat.pixelformat);
        ImGui::Text("Image format %s", fromImageFormatToString(usedFormat.format).c_str());

        // Show subsampling for output devices
        if (camera.getType() == WebcamType::VirtualOutput)
        {
            const char* subsampling_options[] = {"4:4:4", "4:2:2", "4:2:0", "GRAY", "4:4:0", "4:1:1"};
            int current_subsampling = static_cast<int>(camera.getChrominanceSubsampling());

            if (ImGui::Combo("Subsampling", &current_subsampling, subsampling_options,
                             IM_ARRAYSIZE(subsampling_options)))
            {
                common::log_warn("User changed subsampling to %s", subsampling_options[current_subsampling]);
                // device.subsampling = static_cast<TJSAMP>(current_subsampling);
            }

            ImGui::Text("Current: %s", subsampling_options[current_subsampling]);
        }
    }

    // Available Formats (for input devices)
    if (camera.getType() == WebcamType::PhysicalInput
        && ImGui::CollapsingHeader("Available Formats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static int selected_format = -1;
        static int selected_size = -1;
        static int width = 0;
        static int height = 0;
        CameraCapabilities capabilities = camera.getCapabilities();
        for (std::size_t fmt_idx = 0; fmt_idx < capabilities.formats.size(); fmt_idx++)
        {
            const auto& format = capabilities.formats[fmt_idx];

            if (ImGui::TreeNode(("Format " + std::to_string(fmt_idx) + ": " + format.description).c_str()))
            {
                ImGui::Text("Pixel Format: 0x%08X", format.pixelformat);

                if (ImGui::TreeNode("Available Sizes"))
                {
                    for (std::size_t size_idx = 0; size_idx < format.sizes.size(); size_idx++)
                    {
                        const auto& size = format.sizes[size_idx];
                        bool is_current = (selected_format == static_cast<int>(fmt_idx)
                                           && selected_size == static_cast<int>(size_idx));

                        ImGui::PushID(fmt_idx * 1000 + size_idx);
                        if (ImGui::Selectable((std::to_string(size.width) + "x" + std::to_string(size.height)).c_str(),
                                              is_current))
                        {
                            if (is_current)
                            {
                                selected_format = -1;
                                selected_size = -1;
                                width = 0;
                                height = 0;
                            }
                            else
                            {
                                common::log_warn("User selected format %d, size is %d, %d", fmt_idx, size.width,
                                                 size.height);
                                // device.width = size.width;
                                // device.height = size.height;
                                width = size.width;
                                height = size.height;
                                selected_format = fmt_idx;
                                selected_size = size_idx;
                            }
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
            ImGui::Text("Selected: %s - %ux%u", capabilities.formats[selected_format].description.c_str(), width,
                        height);
        }
    }

    // // Resolution Settings (for output devices)
    // if (is_output && ImGui::CollapsingHeader("Resolution Settings"))
    // {
    //     // Maintain aspect ratio toggle
    //     ImGui::Checkbox("Maintain Input Device Aspect Ratio", &maintain_aspect_ratio_);

    //     if (maintain_aspect_ratio_ && !managed_devices_.empty())
    //     {
    //         // Find first input device for aspect ratio reference
    //         for (int i = 0; i < managed_devices_.size() && i < device_is_output_.size(); i++)
    //         {
    //             if (!device_is_output_[i])
    //             {
    //                 const auto& input_device = managed_devices_[i]->getDevice();
    //                 float input_aspect =
    //                     static_cast<float>(input_device.width) / static_cast<float>(input_device.height);
    //                 ImGui::Text("Input Aspect Ratio: %.3f (%ux%u)", input_aspect, input_device.width,
    //                             input_device.height);
    //                 break;
    //             }
    //         }
    //     }

    //     ImGui::Separator();

    //     // Find current resolution index
    //     static int current_resolution_index = -1;
    //     static int custom_width = device.width;
    //     static int custom_height = device.height;

    //     // Check if current resolution matches any preset
    //     current_resolution_index = -1;
    //     for (int i = 0; i < IM_ARRAYSIZE(common_resolutions_) - 1; i++) // -1 to exclude "Custom"
    //     {
    //         if (common_resolutions_[i].width == device.width && common_resolutions_[i].height == device.height)
    //         {
    //             current_resolution_index = i;
    //             break;
    //         }
    //     }

    //     // Resolution preset combobox
    //     const char* preview_text =
    //         (current_resolution_index >= 0) ? common_resolutions_[current_resolution_index].name : "Custom
    //         Resolution";

    //     if (ImGui::BeginCombo("Resolution Preset", preview_text))
    //     {
    //         // Add common resolutions
    //         for (int i = 0; i < IM_ARRAYSIZE(common_resolutions_) - 1; i++) // -1 to exclude "Custom"
    //         {
    //             bool is_compatible = true;

    //             // Check aspect ratio compatibility if maintaining aspect ratio
    //             if (maintain_aspect_ratio_)
    //             {
    //                 // Find first input device for reference
    //                 for (int j = 0; j < managed_devices_.size() && j < device_is_output_.size(); j++)
    //                 {
    //                     if (!device_is_output_[j])
    //                     {
    //                         const auto& input_device = managed_devices_[j]->getDevice();
    //                         float input_aspect =
    //                             static_cast<float>(input_device.width) / static_cast<float>(input_device.height);
    //                         float preset_aspect = static_cast<float>(common_resolutions_[i].width)
    //                                               / static_cast<float>(common_resolutions_[i].height);

    //                         // Allow small tolerance for aspect ratio matching
    //                         const float tolerance = 0.01f;
    //                         is_compatible = (std::abs(input_aspect - preset_aspect) < tolerance);
    //                         break;
    //                     }
    //                 }
    //             }

    //             // Skip incompatible resolutions entirely
    //             if (!is_compatible)
    //             {
    //                 continue;
    //             }

    //             bool is_selected = (current_resolution_index == i);

    //             if (ImGui::Selectable(common_resolutions_[i].name, is_selected))
    //             {
    //                 device.width = common_resolutions_[i].width;
    //                 device.height = common_resolutions_[i].height;
    //                 current_resolution_index = i;
    //                 custom_width = device.width;
    //                 custom_height = device.height;
    //             }

    //             if (is_selected)
    //             {
    //                 ImGui::SetItemDefaultFocus();
    //             }
    //         }

    //         // Custom resolution option
    //         ImGui::Separator();
    //         bool is_custom_selected = (current_resolution_index == -1);
    //         if (ImGui::Selectable("Custom Resolution", is_custom_selected))
    //         {
    //             current_resolution_index = -1;
    //         }

    //         ImGui::EndCombo();
    //     }

    //     // Custom resolution inputs (only show if custom is selected or no preset matches)
    //     if (current_resolution_index == -1)
    //     {
    //         ImGui::Separator();
    //         ImGui::Text("Custom Resolution:");

    //         if (ImGui::InputInt("Width", &custom_width))
    //         {
    //             if (custom_width > 0)
    //             {
    //                 device.width = custom_width;

    //                 // Auto-adjust height if maintaining aspect ratio
    //                 if (maintain_aspect_ratio_)
    //                 {
    //                     // Find first input device for reference
    //                     for (int i = 0; i < managed_devices_.size() && i < device_is_output_.size(); i++)
    //                     {
    //                         if (!device_is_output_[i])
    //                         {
    //                             const auto& input_device = managed_devices_[i]->getDevice();
    //                             float input_aspect =
    //                                 static_cast<float>(input_device.width) / static_cast<float>(input_device.height);
    //                             custom_height = static_cast<int>(custom_width / input_aspect);
    //                             device.height = custom_height;
    //                             break;
    //                         }
    //                     }
    //                 }
    //             }
    //         }

    //         if (ImGui::InputInt("Height", &custom_height))
    //         {
    //             if (custom_height > 0)
    //             {
    //                 device.height = custom_height;
    //             }
    //         }
    //     }

    //     ImGui::Text("Current: %ux%u", device.width, device.height);
    // }

    // Apply Changes Button
    ImGui::Separator();
    if (ImGui::Button("Apply Changes"))
    {
        common::log_info("UI::paintGeneralizedDeviceConfig - Applying device configuration changes for %s",
                         camera.getName().c_str());

        // Reconfigure the device
        // deviceContext.reconfigureDevice(device);

        // // If this is one of the original devices, also update camera manager
        // if (cameraManager_ && device_index < 2)
        // {
        //     if (device_index == 0) // First device (originally input)
        //     {
        //         cameraManager_->reconfigureInputCamera();
        //     }
        //     else if (device_index == 1) // Second device (originally output)
        //     {
        //         cameraManager_->reconfigureOutputCamera();
        //     }
        // }
    }

    ImGui::SameLine();
    if (camera.getType() == WebcamType::VirtualOutput)
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to apply resolution and subsampling changes");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to apply resolution and buffer changes");
    }
}

void UI::paintAddDeviceModal()
{
    ImGui::OpenPopup("Add New Device");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Add New Device", &show_add_device_modal_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Select a video device to add:");
        ImGui::Separator();

        // Device selection
        if (available_video_devices_.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No video devices found in /dev/video*");
        }
        else
        {
            const char* preview_text =
                (selected_video_device_ >= 0 && selected_video_device_ < static_cast<int>(available_video_devices_.size()))
                    ? available_video_devices_[selected_video_device_].c_str()
                    : "Select device...";

            if (ImGui::BeginCombo("Video Device", preview_text))
            {
                for (int i = 0; i < static_cast<int>(available_video_devices_.size()); i++)
                {
                    bool is_selected = (selected_video_device_ == i);
                    if (ImGui::Selectable(available_video_devices_[i].c_str(), is_selected))
                    {
                        selected_video_device_ = i;
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Device name input
        ImGui::InputText("Device Name", device_name_buffer_, sizeof(device_name_buffer_));

        // Device type
        ImGui::Checkbox("Output Device", &is_output_device_);
        ImGui::SameLine();
        ImGui::TextDisabled("(unchecked = Input Device)");

        ImGui::Separator();

        // Buttons
        bool can_add = (selected_video_device_ >= 0 && strlen(device_name_buffer_) > 0);

        if (!can_add)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Add Device"))
        {
            common::log_warn("User added a device");
            // // Create new device context
            // auto new_context = std::make_unique<InputDeviceContext>();

            // // Create device configuration
            // CapturingDevice new_device;
            // new_device.name = std::string(device_name_buffer_);
            // new_device.device_path = available_video_devices_[selected_video_device_];
            // new_device.width = 640; // Default resolution
            // new_device.height = 480;
            // new_device.buffer_count = 4; // Default buffer count
            // new_device.subsampling = TJSAMP_420;

            // // Setup the device (this will call your internal logic)
            // if (new_context->setupDevice(new_device))
            // {
            //     managed_devices_.push_back(std::move(new_context));
            //     device_is_output_.push_back(is_output_device_);

            //     common::log_info("UI::paintAddDeviceModal - Successfully added device: %s", new_device.name.c_str());
            // }
            // else
            // {
            //     common::log_error("UI::paintAddDeviceModal - Failed to setup device: %s", new_device.name.c_str());
            // }

            // Reset modal state
            memset(device_name_buffer_, 0, sizeof(device_name_buffer_));
            is_output_device_ = false;
            selected_video_device_ = -1;
            show_add_device_modal_ = false;
        }

        if (!can_add)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            // Reset modal state
            memset(device_name_buffer_, 0, sizeof(device_name_buffer_));
            is_output_device_ = false;
            selected_video_device_ = -1;
            show_add_device_modal_ = false;
        }

        ImGui::EndPopup();
    }
}
