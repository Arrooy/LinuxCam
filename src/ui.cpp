#include "LinuxFace/ui.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "LinuxFace/UI/paintWebcam.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

ImVec4 getProfileColorFromDuration(int64_t value);

UI::UI(std::shared_ptr<LayerManager> layerManager) : layerManager_(std::move(layerManager))
{
    paintWebcam_ = std::make_unique<PaintWebcam>();
    // MediaBrowserUI will be constructed in connect()
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

void UI::loadingScreen()
{
    newFrame();
    // Centered loading text
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("LoadingWindow", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                     | ImGuiWindowFlags_NoBackground);
    // Show placeholder when nothing is selected
    std::string msg = "Loading content, please wait...";
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    ImVec2 textSize = ImGui::CalcTextSize(msg.c_str());
    ImGui::SetCursorPos(ImVec2((contentSize.x - textSize.x) * 0.5f, (contentSize.y - textSize.y) * 0.5f));
    ImGui::TextDisabled("%s", msg.c_str());
    ImGui::End();

    render();
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
    // Handle layer dragging globally (for the whole window)
    handleLayerDragging();
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
        if (ImGui::BeginMenu("Options..."))
        {
            ImGui::MenuItem("Toggle Profiler", NULL, &show_profiler_);
            ImGui::MenuItem("Toggle Device Configuration", NULL, &show_device_config_);
            ImGui::MenuItem("Toggle Media Browser", NULL, &mediaBrowserVisible_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Load media..."))
        {
            ImGui::Text("Media categories");
            ImGui::Separator();
            ImGui::Spacing();

            renderCollapsingHeader("Images", mediaManager_->getImageNames(), "image");
            renderCollapsingHeader("GIFs", mediaManager_->getGifNames(), "gif");
            ImGui::EndMenu();
        }
        // Add new menu for text layer
        if (ImGui::BeginMenu("Load text..."))
        {
            static float text_font_size = 16.0f;
            static ImVec4 text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            ImGui::InputText("Text", add_text_layer_buffer_, IM_ARRAYSIZE(add_text_layer_buffer_));
            ImGui::InputFloat("Font Size", &text_font_size, 1.0f, 5.0f, "%.1f");
            ImGui::ColorEdit4("Color", (float*) &text_color, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            if (ImGui::Button("Add Text Layer"))
            {
                if (layerManager_)
                {
                    Layer newText;
                    newText.id = Layer::next_id++;
                    newText.type = LayerType::Text;
                    newText.textContent = add_text_layer_buffer_;
                    newText.name = add_text_layer_buffer_;
                    newText.x = 100;
                    newText.y = 100;
                    newText.fontSize = text_font_size;
                    newText.textColor = ImGui::ColorConvertFloat4ToU32(text_color);
                    layerManager_->addLayer(newText);
                }
                // Optionally clear buffer or reset
                strncpy(add_text_layer_buffer_, "Write here", 11);
            }
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
        const auto frameDuration = 1.0f / io.Framerate;
        ImGui::TextColored(getProfileColorFromDuration(frameDuration), "Application average %.3f ms/frame (%.1f FPS)",
                           frameDuration * 1000.0f, io.Framerate);

        auto durations = Profiler::getInstance().getDurationsSorted();

        for (const auto& pair : durations)
        {
            ImGui::TextColored(getProfileColorFromDuration(pair.second.count()), "%s - %s", pair.first.c_str(),
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

    if (oneshot_add_device_popup_)
    {
        ImGui::OpenPopup("Add New Device");
        show_add_device_modal_ = true;
        oneshot_add_device_popup_ = false;
    }

    // Render add device modal
    if (show_add_device_modal_)
    {
        show_add_device_modal_ = paintWebcam_->paintAddDeviceModal(temp_modal_webcams_);
        if (!show_add_device_modal_)
        {
            // Cleanup the modal
            temp_modal_webcams_.clear();
        }
    }

    if (mediaBrowserUI_ && mediaBrowserVisible_)
    {
        mediaBrowserVisible_ = mediaBrowserUI_->render();
    }
}
void UI::renderCollapsingHeader(const std::string& headerName, const std::vector<std::string>& items,
                                const std::string& type)
{
    if (items.empty())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::CollapsingHeader(headerName.c_str()))
    {
        for (const auto& item : items)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::PushID(item.c_str());
            float buttonWidth = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 4.0f;

            ImGui::BeginGroup();
            ImGui::Text("%s", item.c_str());
            ImGui::EndGroup();

            if (type == "image")
            {
                ImGui::SameLine();
                ImGui::BeginGroup();
                if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
                {
                    auto origImage = mediaManager_->getImage(item);
                    if (origImage && layerManager_)
                    {
                        auto newImage = origImage->deepCopy();
                        if (newImage)
                        {
                            newImage->info.textureId = 0;
                            Layer newLayer;
                            newLayer.id = Layer::next_id++;
                            newLayer.type = LayerType::Image;
                            newLayer.name = item;
                            newLayer.img = std::move(newImage);
                            newLayer.img->setTextureId(0);
                            newLayer.dirty = true;

                            layerManager_->addLayer(newLayer);
                        }
                    }
                }
                ImGui::EndGroup();
            }
            else if (type == "gif")
            {
                ImGui::SameLine();
                ImGui::BeginGroup();
                if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
                {
                    auto origGif = mediaManager_->getGif(item);
                    if (origGif && !origGif->frames().empty() && layerManager_)
                    {
                        Layer newLayer;
                        newLayer.id = Layer::next_id++;
                        newLayer.type = LayerType::Gif;
                        newLayer.name = item;
                        newLayer.gif = origGif;
                        newLayer.x = 100;
                        newLayer.y = 100;
                        newLayer.gifFrameIndex = 0;
                        newLayer.dirty = true;
                        layerManager_->addLayer(newLayer);
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::PopID();
        }
    }
    if (items.empty())
    {
        ImGui::EndDisabled();
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
                return;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            auto selected_layer = mediaBrowserUI_->getSelectedLayer();
            if (selected_layer)
            {
                // Remove the selected layer
                layerManager_->removeLayer(selected_layer->id);
            }
        }
    }
}

// Converts an int64 duration (microSecs) to a color ranging from green to red
ImVec4 getProfileColorFromDuration(int64_t duration)
{
    constexpr int64_t greenThreshold = 10000; // 10ms
    constexpr int64_t redThreshold = 40000;   // 40ms

    // Clamp and normalize duration thresholds
    float t;
    if (duration <= greenThreshold)
    {
        t = 0.0f;
    }
    else if (duration >= redThreshold)
    {
        t = 1.0f;
    }
    else
    {
        t = static_cast<float>(duration - greenThreshold) / static_cast<float>(redThreshold - greenThreshold);
    }

    // Interpolate green to red
    float r = common::lerp(0.0f, 1.0f, t);
    float g = common::lerp(1.0f, 0.0f, t);
    float b = 0.2f;
    float a = 1.0f;

    return ImVec4(r, g, b, a);
}

void UI::handleLayerDragging()
{
    if (!layerManager_)
    {
        return;
    }
    // Prevent dragging while interacting with any ImGui item (e.g., color picker)
    if (ImGui::IsAnyItemActive())
    {
        return;
    }
    auto& layers = layerManager_->getLayers();
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    // On mouse click, select the topmost layer under the mouse
    if (ImGui::IsMouseClicked(0))
    {
        bool foundLayer = false;
        // Iterate from topmost to bottom (reverse order)
        for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i)
        {
            Layer& layer = layers[i];
            float lx = layer.x;
            float ly = layer.y;
            float lw = 0, lh = 0;
            if (layer.type == LayerType::Image && layer.img)
            {
                lw = static_cast<float>(layer.img->info.width);
                lh = static_cast<float>(layer.img->info.height);
            }
            else if (layer.type == LayerType::Gif && layer.gif)
            {
                // Use the first frame's dimensions for GIFs
                if (layer.gif->frames().empty())
                {
                    continue; // Skip empty GIFs
                }
                lw = static_cast<float>(layer.gif->frames()[0]->info.width);
                lh = static_cast<float>(layer.gif->frames()[0]->info.height);
            }
            else if (layer.type == LayerType::Text)
            {
                // Estimate text bounding box (simple, not perfect)
                ImVec2 textSize = ImGui::CalcTextSize(layer.textContent.c_str(), nullptr, false, -1);
                lw = textSize.x * (layer.fontSize / 16.0f);
                lh = textSize.y * (layer.fontSize / 16.0f);
            }

            if (mousePos.x >= lx && mousePos.x <= lx + lw && mousePos.y >= ly && mousePos.y <= ly + lh)
            {
                // Select this layer, deselect others
                for (auto& l : layers)
                {
                    l.selected = false;
                }
                layer.selected = true;
                foundLayer = true;
                break;
            }
        }
        if (!foundLayer)
        {
            // If no layer was found, deselect all layers
            for (auto& l : layers)
            {
                l.selected = false;
            }
        }
    }
    if (!ImGui::IsMouseDown(0))
    {
        return;
    }
    // Find the selected layer (search all layers for .selected)
    auto it = std::find_if(layers.begin(), layers.end(), [](const Layer& l) { return l.selected; });
    if (it == layers.end())
    {
        return;
    }
    Layer& selectedLayer = *it;
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    // Only allow dragging if mouse is dragging and a layer is selected
    if (ImGui::IsMouseDragging(0) && !selectedLayer.isBaseLayer)
    {
        selectedLayer.x += delta.x;
        selectedLayer.y += delta.y;
        // Mark the layer as dirty to trigger a redraw
        selectedLayer.dirty = true;
    }
}

