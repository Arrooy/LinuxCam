#include "LinuxFace/ui.h"

#include "LinuxFace/UI/paintWebcam.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using namespace linuxface;

UI::UI()
{
    paintWebcam_ = std::make_unique<PaintWebcam>();
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

    // disable ini and log files for ImGui
    io.IniFilename = NULL;
    io.LogFilename = NULL;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

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

    // glfwSetKeyCallback(window, KeyCallback);
    // glfwSetWindowUserPointer(window, this);
    // glfwSetCursorPosCallback(window, UI::mouseCallback);
    common::log_info("UI initialized successfully");
    ready_ = true;
    return true;
}


void UI::shutdown()
{
    if (!ready_)
    {
        common::log_error("UI is not initialized, cannot shutdown.");
        return;
    }
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
    // ImGui::ShowDemoWindow();

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

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

        auto textColor = ImVec4(0.0f, 0.7f, 1.0f, 1.0f);
        auto durations = Profiler::getInstance().getDurationsSorted();

        for (const auto& pair : durations)
        {
            ImGui::TextColored(textColor, "%s - %s", pair.first.c_str(),
                               Profiler::format_duration(pair.second).c_str());
        }

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

    if(oneshot_add_device_popup_)
    {
        ImGui::OpenPopup("Add New Device");
        show_add_device_modal_ = true;
        oneshot_add_device_popup_ = false;
    }

    // Render add device modal
    if (show_add_device_modal_)
    {
        show_add_device_modal_ = paintWebcam_->paintAddDeviceModal(temp_modal_webcams_);
        if(!show_add_device_modal_)
        {
            // Cleanup the modal
            temp_modal_webcams_.clear();
        }
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
        auto managed_webcams = cameraManager_->getWebcams();
        unsigned int tab_index{0u};
        // Render tabs for existing devices
        for (auto& webcam : managed_webcams)
        {
            std::string tab_name = webcam->getName();
            ImGuiTabItemFlags flags = 0;

            // Check if we need to programmatically select this tab
            if (requestedTab_ == static_cast<int>(tab_index))
            {
                flags |= ImGuiTabItemFlags_SetSelected;
                requestedTab_ = -1; // Clear the request
                active_device_tab_ = tab_index;
            }

            // Add webcam type indicator
            tab_name += webcam->getType() == WebcamType::PhysicalInput ? " (IN)" : " (OUT)";

            // Add close button to tab
            bool tab_open = true;
            if (ImGui::BeginTabItem((tab_name + "###tab" + std::to_string(tab_index++)).c_str(), &tab_open, flags))
            {
                paintWebcam_->setWebcam(webcam);
                paintWebcam_->paintDevice();
                last_device_tab_index_ = tab_index; // Save last active tab
                ImGui::EndTabItem();
            }

            // Handle tab closing
            // TODO: not working.
            if (!tab_open)
            {
                common::log_error("Closing device tab: %s", tab_name.c_str());
                if (active_device_tab_ >= 1)
                {
                    active_device_tab_--;
                }
                break; // Break to avoid iterator invalidation
            }
        }

        // Add new device button as trailing tab
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip
                                          | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
        {
            // Check if this is the first frame this tab became active
            if (!was_plus_tab_active_ && !show_add_device_modal_)
            {
                // Create temporary webcams for all available devices
                temp_modal_webcams_.clear();

                // Use CameraManager to discover devices
                std::vector<std::string> device_paths = cameraManager_->discoverAvailableVideoDevices();
                temp_modal_webcams_.reserve(device_paths.size());

                // Create a temp webcam for each device.
                for (const auto& device_path : device_paths)
                {
                    auto temp_webcam = std::make_shared<InputWebcam>("temp_" + device_path, device_path, 640, 480, 1);
                    temp_modal_webcams_.push_back(temp_webcam);
                    common::log_info("Created temporary webcam for %s", device_path.c_str());
                }

                // Reset selected webcam
                paintWebcam_->setNewDeviceModalWebcam(nullptr);
                // Trigger the add device popup
                oneshot_add_device_popup_ = true;

                was_plus_tab_active_ = true;
                common::log_info("UI::paintDeviceConfigurationTabs - Opening add device modal");
            }
        }
        else
        {
            // Reset the flag when not on the + tab
            was_plus_tab_active_ = false;
        }

        ImGui::EndTabBar();
    }

    // Update position for next window
    current_y_ += ImGui::GetWindowSize().y;
    ImGui::End();
}


// void UI::paintAddDeviceModal()
// {
//     ImGui::OpenPopup("Add New Device");

//     ImVec2 center = ImGui::GetMainViewport()->GetCenter();
//     ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
//     ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Appearing);

//     bool modal_open = true;
//     if (ImGui::BeginPopupModal("Add New Device", &modal_open, ImGuiWindowFlags_AlwaysAutoResize))
//     {
//         ImGui::Text("Select a video device to add:");
//         ImGui::Separator();

//         // Device selection
//         if (temp_modal_webcams_.empty())
//         {
//             ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No video devices found in /dev/video*");
//         }
//         else
//         {
//             // Create vector of device paths from temp_modal_webcams_ for combo box
//             std::vector<std::string> device_paths;
//             for (const auto& pair : temp_modal_webcams_)
//             {
//                 device_paths.push_back(pair.first);
//             }
//             // TODO: New created webcams should appear in the modal.
//             const char* preview_text =
//                 (selected_video_device_ >= 0 && selected_video_device_ < static_cast<int>(device_paths.size()))
//                     ? device_paths[selected_video_device_].c_str()
//                     : "Select device...";

//             if (ImGui::BeginCombo("Video Device", preview_text))
//             {
//                 for (int i = 0; i < static_cast<int>(device_paths.size()); i++)
//                 {
//                     bool is_selected = (selected_video_device_ == i);
//                     if (ImGui::Selectable(device_paths[i].c_str(), is_selected))
//                     {
//                         selected_video_device_ = i;
//                         // Reset device name when device changes
//                         if (selected_video_device_ >= 0)
//                         {
//                             std::string device_path = device_paths[selected_video_device_];
//                             std::string device_name = "Device " + device_path.substr(device_path.find_last_of('/') +
//                             1); strncpy(device_name_buffer_, device_name.c_str(), sizeof(device_name_buffer_) - 1);
//                             device_name_buffer_[sizeof(device_name_buffer_) - 1] = '\0';
//                         }
//                     }
//                     if (is_selected)
//                     {
//                         ImGui::SetItemDefaultFocus();
//                     }
//                 }
//                 ImGui::EndCombo();
//             }
//         }

//         // Device name input
//         ImGui::InputText("Device Name", device_name_buffer_, sizeof(device_name_buffer_),
//         ImGuiInputTextFlags_ReadOnly);

//         // Show device information and formats for input devices
//         if (selected_video_device_ >= 0)
//         {
//             ImGui::Separator();

//             // Get device path from temp_modal_webcams_
//             std::vector<std::string> device_paths;
//             for (const auto& pair : temp_modal_webcams_)
//             {
//                 device_paths.push_back(pair.first);
//             }

//             if (selected_video_device_ < static_cast<int>(device_paths.size()))
//             {
//                 std::string device_path = device_paths[selected_video_device_];

//                 // Use existing temporary webcam
//                 auto temp_webcam_it = temp_modal_webcams_.find(device_path);
//                 if (temp_webcam_it != temp_modal_webcams_.end())
//                 {
//                     auto temp_webcam = temp_webcam_it->second;
//                     CameraCapabilities capabilities = temp_webcam->getCapabilities();

//                     // Device Information
//                     if (ImGui::CollapsingHeader("Device Information", ImGuiTreeNodeFlags_DefaultOpen))
//                     {
//                         ImGui::Text("Path: %s", device_path.c_str());
//                         if (!capabilities.driver.empty())
//                         {
//                             ImGui::Text("Driver: %s", capabilities.driver.c_str());
//                             ImGui::Text("Card: %s", capabilities.card.c_str());
//                             ImGui::Text("Bus Info: %s", capabilities.bus_info.c_str());
//                         }
//                     }

//                     // Available Formats
//                     if (ImGui::CollapsingHeader("Available Formats", ImGuiTreeNodeFlags_DefaultOpen))
//                     {
//                         std::string modal_key = "modal_" + device_path;

//                         // Initialize if not exists
//                         if (selected_format_indices_.find(modal_key) == selected_format_indices_.end())
//                         {
//                             selected_format_indices_[modal_key] = -1;
//                             selected_size_indices_[modal_key] = -1;
//                         }

//                         int& selected_format = selected_format_indices_[modal_key];
//                         int& selected_size = selected_size_indices_[modal_key];

//                         for (std::size_t fmt_idx = 0; fmt_idx < capabilities.formats.size(); fmt_idx++)
//                         {
//                             const auto& format = capabilities.formats[fmt_idx];

//                             if (ImGui::TreeNode(
//                                     ("Format " + std::to_string(fmt_idx) + ": " + format.description).c_str()))
//                             {
//                                 ImGui::Text("Pixel Format: 0x%08X", format.pixelformat);

//                                 if (ImGui::TreeNode("Available Sizes"))
//                                 {
//                                     for (std::size_t size_idx = 0; size_idx < format.sizes.size(); size_idx++)
//                                     {
//                                         const auto& size = format.sizes[size_idx];
//                                         bool is_current = (selected_format == static_cast<int>(fmt_idx)
//                                                            && selected_size == static_cast<int>(size_idx));

//                                         ImGui::PushID(fmt_idx * 1000 + size_idx + 10000); // Offset to avoid
//                                         conflicts if (ImGui::Selectable((std::to_string(size.width) + "x"
//                                                                          + std::to_string(size.height))
//                                                                             .c_str(),
//                                                                         is_current))
//                                         {
//                                             if (is_current)
//                                             {
//                                                 selected_format = -1;
//                                                 selected_size = -1;
//                                             }
//                                             else
//                                             {
//                                                 selected_format = fmt_idx;
//                                                 selected_size = size_idx;
//                                             }
//                                         }
//                                         ImGui::PopID();
//                                     }
//                                     ImGui::TreePop();
//                                 }
//                                 ImGui::TreePop();
//                             }
//                         }

//                         if (selected_format >= 0 && selected_size >= 0)
//                         {
//                             ImGui::Separator();
//                             const auto& sel_format = capabilities.formats[selected_format];
//                             const auto& sel_size = sel_format.sizes[selected_size];
//                             ImGui::Text("Selected: %s - %ux%u", sel_format.description.c_str(), sel_size.width,
//                                         sel_size.height);
//                         }
//                     }
//                 }
//                 else
//                 {
//                     ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Device information not available");
//                 }
//             }
//         }

//         ImGui::Separator();

//         // Buttons
//         bool can_add = (selected_video_device_ >= 0 && strlen(device_name_buffer_) > 0);

//         if (!can_add)
//         {
//             ImGui::BeginDisabled();
//         }

//         if (ImGui::Button("Add Device"))
//         {
//             // Get device path from temp_modal_webcams_
//             std::vector<std::string> device_paths;
//             for (const auto& pair : temp_modal_webcams_)
//             {
//                 device_paths.push_back(pair.first);
//             }

//             std::string device_path = device_paths[selected_video_device_];
//             std::string device_name = std::string(device_name_buffer_);

//             common::log_info("UI::paintAddDeviceModal - User adding input device: %s at %s", device_name.c_str(),
//                              device_path.c_str());

//             // Use selected format/size if available, otherwise defaults
//             uint32_t width = 640, height = 480;

//             std::string modal_key = "modal_" + device_path;
//             if (selected_format_indices_.find(modal_key) != selected_format_indices_.end()
//                 && selected_format_indices_[modal_key] >= 0 && selected_size_indices_[modal_key] >= 0)
//             {
//                 auto temp_webcam_it = temp_modal_webcams_.find(device_path);
//                 if (temp_webcam_it != temp_modal_webcams_.end())
//                 {
//                     auto capabilities = temp_webcam_it->second->getCapabilities();
//                     auto& selected_format = capabilities.formats[selected_format_indices_[modal_key]];
//                     auto& selected_size = selected_format.sizes[selected_size_indices_[modal_key]];
//                     width = selected_size.width;
//                     height = selected_size.height;
//                 }
//             }

//             // Create input device
//             auto inputCam = std::make_shared<InputWebcam>(device_name, device_path, width, height, 4);
//             bool success = cameraManager_->addCamera(inputCam);

//             // Remove the temporary webcam for this device since we're using a real one now
//             auto temp_it = temp_modal_webcams_.find(device_path);
//             if (temp_it != temp_modal_webcams_.end())
//             {
//                 temp_modal_webcams_.erase(temp_it);
//             }

//             if (success)
//             {
//                 common::log_info("Successfully added input device: %s", device_name.c_str());
//             }
//             else
//             {
//                 common::log_error("Failed to add input device: %s", device_name.c_str());
//             }

//             // Clean up all remaining temporary webcams and reset modal state
//             temp_modal_webcams_.clear();
//             memset(device_name_buffer_, 0, sizeof(device_name_buffer_));
//             selected_video_device_ = -1;

//             // Close modal and reset states
//             modal_open = false;
//             show_add_device_modal_ = false;
//             was_plus_tab_active_ = false;
//             go_back_to_last_device_ = true;
//             ImGui::CloseCurrentPopup();
//         }

//         if (!can_add)
//         {
//             ImGui::EndDisabled();
//         }

//         ImGui::SameLine();

//         if (ImGui::Button("Cancel"))
//         {
//             // Clean up all temporary webcams and reset modal state
//             temp_modal_webcams_.clear();
//             memset(device_name_buffer_, 0, sizeof(device_name_buffer_));
//             selected_video_device_ = -1;

//             // Close modal and reset states
//             modal_open = false;
//             show_add_device_modal_ = false;
//             was_plus_tab_active_ = false;
//             go_back_to_last_device_ = true;

//             common::log_info("UI::paintAddDeviceModal - User cancelled adding device");
//             ImGui::CloseCurrentPopup();
//             // Force tab selection back to a valid device tab
//             if (last_device_tab_index_ > 0)
//             {
//                 // This will be handled in the next frame by the tab system
//                 active_device_tab_ = last_device_tab_index_ - 1;
//             }
//         }


//         if (ImGui::IsKeyPressed(ImGuiKey_Escape))
//         {
//             ImGui::CloseCurrentPopup();
//         }

//         ImGui::EndPopup();
//     }

//     // Handle modal being closed by X button or ESC key
//     if (!modal_open && show_add_device_modal_)
//     {
//         // Clean up all temporary webcams and reset modal state
//         temp_modal_webcams_.clear();
//         memset(device_name_buffer_, 0, sizeof(device_name_buffer_));
//         selected_video_device_ = -1;

//         // Reset states
//         show_add_device_modal_ = false;
//         was_plus_tab_active_ = false;
//         go_back_to_last_device_ = true;

//         // Force tab selection back to a valid device tab
//         if (last_device_tab_index_ > 0)
//         {
//             // This will be handled in the next frame by the tab system
//             active_device_tab_ = last_device_tab_index_ - 1;
//         }
//     }
// }


void UI::handleKeyboard()
{
    // Ignore keyboard shortcuts if ImGui has an active item
    // This is to prevent the user from accidentally switching tabs while typing in the camera settings
    if (!ImGui::IsAnyItemActive())
    {
        const auto& managed_webcams = cameraManager_->getWebcams();
        int size = managed_webcams.size();

        // Ctrl+1-9: Switch to specific tab
        for (int i = 0; i < 9 && i < size; ++i)
        {
            ImGuiKey key = (ImGuiKey) (ImGuiKey_1 + i);
            if (ImGui::IsKeyPressed(key) && active_device_tab_ != i)
            {
                requestedTab_ = i;
                managed_webcams[i]->setCurrentlySelected(true);
                for (const auto& webcam : managed_webcams)
                {
                    if (webcam->getDevicePath() != managed_webcams[i]->getDevicePath())
                    {
                        webcam->setCurrentlySelected(false);
                    }
                }
            }
        }
    }
}
