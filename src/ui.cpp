#include "LinuxFace/ui.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cmath>

#include "LinuxFace/Image/text_renderer.h"
#include "LinuxFace/UI/paintWebcam.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

ImVec4 getProfileColorFromDuration(int64_t duration);

UI::UI(std::shared_ptr<LayerManager> layerManager) : layerManager_(std::move(layerManager))
{
    paintWebcam_ = std::make_unique<PaintWebcam>();
    // MediaBrowserUI will be constructed in connect()
}

UI::~UI()
{
    shutdown();
}

bool UI::initialize(GLFWwindow* window, const char* glslVersion)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // disable ini and log files for ImGui
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
    {
        common::logError("Failed to initialize ImGui GLFW backend");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init(glslVersion))
    {
        common::logError("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }

    // glfwSetKeyCallback(window, KeyCallback);
    // glfwSetWindowUserPointer(window, this);
    // glfwSetCursorPosCallback(window, UI::mouseCallback);
    common::logInfo("UI initialized successfully");
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
    const std::string msg = "Loading content, please wait...";
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const ImVec2 textSize = ImGui::CalcTextSize(msg.c_str());
    ImGui::SetCursorPos(ImVec2((contentSize.x - textSize.x) * 0.5f, (contentSize.y - textSize.y) * 0.5f));
    ImGui::TextDisabled("%s", msg.c_str());
    ImGui::End();

    render();
}

void UI::shutdown() const
{
    if (!ready_)
    {
        common::logError("UI is not initialized, cannot shutdown.");
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
            ImGui::MenuItem("Toggle Profiler", nullptr, &show_profiler_);
            ImGui::MenuItem("Toggle Device Configuration", nullptr, &show_device_config_);
            ImGui::MenuItem("Toggle Media Browser", nullptr, &mediaBrowserVisible_);
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
            static int textScale = 1;
            static ImVec4 textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            static ImVec4 bgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            static bool useBackground = false;
            static bool centerText = true;
            static bool forceSingleLine = false;
            static bool useTextWrapping = false;
            static int textWidthLimit = 400;
            static int textAlignment = 1;  // 0=LEFT, 1=CENTER, 2=RIGHT
            static int textVAlignment = 1; // 0=TOP, 1=MIDDLE, 2=BOTTOM
            static int padding = 2;

            ImGui::InputText("Text", add_text_layer_buffer_, IM_ARRAYSIZE(add_text_layer_buffer_));

            ImGui::Separator();
            ImGui::Text("Text Styling");

            ImGui::SliderInt("Font Scale", &textScale, 0.05, 25, "%dx");
            ImGui::ColorEdit4("Text Color", reinterpret_cast<float*>(&textColor), ImGuiColorEditFlags_NoInputs);

            ImGui::Checkbox("Use Background", &useBackground);
            if (useBackground)
            {
                ImGui::SameLine();
                ImGui::ColorEdit4("BG Color", reinterpret_cast<float*>(&bgColor), ImGuiColorEditFlags_NoInputs);
                ImGui::SliderInt("Padding", &padding, 0, 10);
            }

            ImGui::Separator();
            ImGui::Text("Text Layout");

            ImGui::Checkbox("Force Single Line", &forceSingleLine);
            if (!forceSingleLine)
            {
                ImGui::Checkbox("Enable Text Wrapping", &useTextWrapping);
                if (useTextWrapping)
                {
                    ImGui::SliderInt("Wrap Width", &textWidthLimit, 50, 800, "%d px");
                }
            }

            ImGui::Checkbox("Center Text", &centerText);
            if (!centerText)
            {
                const char* hAlignItems[] = {"Left", "Center", "Right"};
                ImGui::Combo("H-Align", &textAlignment, hAlignItems, IM_ARRAYSIZE(hAlignItems));

                const char* vAlignItems[] = {"Top", "Middle", "Bottom"};
                ImGui::Combo("V-Align", &textVAlignment, vAlignItems, IM_ARRAYSIZE(vAlignItems));
            }

            if (ImGui::Button("Add Text Layer"))
            {
                if (layerManager_)
                {
                    // Create TextRenderConfig for the new approach
                    auto toPixel = [](const ImVec4& color) -> Pixel
                    {
                        return {static_cast<unsigned char>(color.x * 255), static_cast<unsigned char>(color.y * 255),
                                static_cast<unsigned char>(color.z * 255), static_cast<unsigned char>(color.w * 255)};
                    };

                    TextRenderConfig config(add_text_layer_buffer_, toPixel(textColor), textScale);

                    // Set background
                    config.useBackground = useBackground;
                    config.backgroundColor = toPixel(bgColor);
                    config.padding = padding;

                    // Set wrap mode based on UI choices
                    if (forceSingleLine)
                    {
                        config.wrapMode = TextWrapMode::NONE;
                        if (useTextWrapping)
                        {
                            config.maxWidth = textWidthLimit; // Truncate at this width
                        }
                    }
                    else if (useTextWrapping)
                    {
                        config.wrapMode = TextWrapMode::AUTO_WIDTH;
                        config.maxWidth = textWidthLimit;
                    }
                    else
                    {
                        config.wrapMode = TextWrapMode::AUTO_CANVAS; // Natural line breaks or single line
                    }

                    // Set alignment
                    if (centerText)
                    {
                        config.horizontalAlign = TextAlignment::CENTER;
                        config.verticalAlign = TextAlignment::MIDDLE;
                    }
                    else
                    {
                        config.horizontalAlign = static_cast<TextAlignment>(textAlignment);
                        config.verticalAlign = static_cast<TextAlignment>(textVAlignment + 3);
                    }

                    // Render the text using the new system
                    auto textImage = TextRenderer::renderText(config);

                    if (textImage)
                    {
                        Layer newText;
                        newText.id = Layer::getNextId();
                        newText.type = LayerType::TEXT;
                        newText.img = textImage;                      // Store the generated text image
                        newText.textContent = add_text_layer_buffer_; // Keep text for reference
                        newText.name = add_text_layer_buffer_;
                        newText.setPosition(100, 100);
                        newText.textOverlay.enabled = false; // Disable overlay for text layers to avoid duplicate text

                        layerManager_->addLayer(newText);
                    }
                }
                // Optionally clear buffer or reset
                strncpy(add_text_layer_buffer_, "Write here", 11);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    // Render profiler window
    if (show_profiler_)
    {
        ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()), ImGuiCond_FirstUseEver);
        // Set intelligent default size based on content and screen size
        const ImGuiIO& io = ImGui::GetIO();
        const float defaultWidth = std::min(800.0f, io.DisplaySize.x * 0.8f);
        const float defaultHeight = std::min(600.0f, io.DisplaySize.y * 0.8f);
        ImGui::SetNextWindowSize(ImVec2(defaultWidth, defaultHeight), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Profiler", &show_profiler_, ImGuiWindowFlags_None))
        {
            const ImGuiIO& io = ImGui::GetIO();
            const auto frameDuration = 1.0f / io.Framerate;
            const auto frameDurationMicros =
                static_cast<int64_t>(frameDuration * 1000000.0f); // Convert to microseconds
            ImGui::TextColored(getProfileColorFromDuration(frameDurationMicros),
                               "Application average %.3f ms/frame (%.1f FPS)", frameDuration * 1000.0f, io.Framerate);

            // Add reset button
            ImGui::Separator();
            if (ImGui::Button("Reset Statistics"))
            {
                Profiler::getInstance().resetStatistics();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("All statistics use a moving window (last 150 samples).\n"
                                  "Min/Max values reflect the range within this window,\n"
                                  "not all-time values. Reset clears all collected data.");
            }

            ImGui::Separator();

            // Use statistics instead of just durations
            auto allStats = Profiler::getInstance().getAllTimerStatistics();

            // Sort by current duration for display
            std::vector<std::pair<std::string, Profiler::TimerStatistics>> sortedStats;
            sortedStats.reserve(allStats.size());

            for (const auto& pair : allStats)
            {
                sortedStats.emplace_back(pair.first, pair.second);
            }

            std::sort(sortedStats.begin(), sortedStats.end(),
                      [](const auto& a, const auto& b) { return a.second.current > b.second.current; });

            if (!sortedStats.empty())
            {
                // Use scrollable table with flexible columns that auto-resize to content
                if (ImGui::BeginTable("ProfilerStats", 4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                                          | ImGuiTableFlags_ScrollY
                                          | ImGuiTableFlags_SizingFixedFit)) // Auto-fit content
                {
                    ImGui::TableSetupColumn("Timer", ImGuiTableColumnFlags_WidthStretch,
                                            0.0f); // Timer name gets remaining space
                    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 0.0f); // Auto-size to content
                    ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed, 0.0f); // Auto-size to content
                    ImGui::TableSetupColumn("Min/Max (Window)", ImGuiTableColumnFlags_WidthFixed,
                                            0.0f); // Auto-size to content
                    ImGui::TableHeadersRow();

                    for (const auto& pair : sortedStats)
                    {
                        const auto& stats = pair.second;
                        ImGui::TableNextRow();

                        // Timer name
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", pair.first.c_str());

                        // Current duration
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(getProfileColorFromDuration(stats.current.count()), "%s",
                                           Profiler::formatDuration(stats.current).c_str());

                        // Average duration
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(getProfileColorFromDuration(stats.average.count()), "%s",
                                           Profiler::formatDuration(stats.average).c_str());

                        // Min/Max range (from moving window)
                        ImGui::TableSetColumnIndex(3);
                        if (stats.sampleCount > 0)
                        {
                            // Use simplified formatting without Hz for min/max to keep it compact
                            auto formatSimple = [](std::chrono::microseconds duration) -> std::string
                            {
                                const int64_t micros = duration.count();
                                if (micros < 1000)
                                {
                                    return std::to_string(micros) + "µs";
                                }
                                else if (micros < 1000000)
                                {
                                    return std::to_string(static_cast<int>(micros / 1000)) + "ms";
                                }
                                else
                                {
                                    return std::to_string(static_cast<int>(micros / 1000000)) + "s";
                                }
                            };

                            ImGui::Text("%s / %s", formatSimple(stats.minimum).c_str(),
                                        formatSimple(stats.maximum).c_str());
                        }
                        else
                        {
                            ImGui::Text("N/A");
                        }
                    }

                    ImGui::EndTable();
                }
            }
            else
            {
                ImGui::Text("No profiling data available");
            }
        }
        ImGui::End();
    }

    // Render device configuration window with tabs
    if (show_device_config_)
    {
        paintDeviceConfigurationTabs();
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
            const float buttonWidth = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 4.0f;

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
                            newLayer.id = Layer::getNextId();
                            newLayer.type = LayerType::IMAGE;
                            newLayer.name = item;
                            newLayer.img = std::move(newImage);
                            newLayer.img->setTextureId(0);
                            newLayer.setPosition(100, 100);
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
                        newLayer.id = Layer::getNextId();
                        newLayer.type = LayerType::GIF;
                        newLayer.name = item;
                        newLayer.gif = origGif;
                        newLayer.setPosition(100, 100);
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

    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()), ImGuiCond_FirstUseEver);

    // Calculate available space
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float availableHeight = viewport->WorkSize.y - ImGui::GetFrameHeight() - 10; // 10px padding from bottom

    // Set window size constraints
    ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 200), // Minimum size
                                        ImVec2(-1,
                                               availableHeight) // Maximum size (limited by available space)
    );

    ImGui::Begin("Device Configuration", &show_device_config_,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);


    if (ImGui::BeginTabBar("DeviceTabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable))
    {
        auto managedWebcams = cameraManager_->getWebcams();
        unsigned int tabIndex{0u};
        // Render tabs for existing devices
        for (auto& webcam : managedWebcams)
        {
            std::string tabName = webcam->getName();
            ImGuiTabItemFlags flags = 0;

            // Check if we need to programmatically select this tab
            if (requestedTab_ == static_cast<int>(tabIndex))
            {
                flags |= ImGuiTabItemFlags_SetSelected;
                requestedTab_ = -1; // Clear the request
                active_device_tab_ = tabIndex;
            }

            // Add webcam type indicator
            tabName += webcam->getType() == WebcamType::PHYSICAL_INPUT ? " (IN)" : " (OUT)";

            // Create tab without close button
            if (ImGui::BeginTabItem((tabName + "###tab" + std::to_string(tabIndex++)).c_str(), nullptr, flags))
            {
                paintWebcam_->setWebcam(webcam);
                paintWebcam_->paintDevice();
                last_device_tab_index_ = tabIndex; // Save last active tab
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}


void UI::handleKeyboard()
{
    // Ignore keyboard shortcuts if ImGui has an active item
    // This is to prevent the user from accidentally switching tabs while typing in the camera settings
    if (!ImGui::IsAnyItemActive())
    {
        const auto& managedWebcams = cameraManager_->getWebcams();
        const int size = managedWebcams.size();

        // Ctrl+1-9: Switch to specific tab
        for (int i = 0; i < 9 && i < size; ++i)
        {
            const auto key = static_cast<ImGuiKey>(ImGuiKey_1 + i);
            if (ImGui::IsKeyPressed(key) && active_device_tab_ != i)
            {
                requestedTab_ = i;
                managedWebcams[i]->setCurrentlySelected(true);
                for (const auto& webcam : managedWebcams)
                {
                    if (webcam->getDevicePath() != managedWebcams[i]->getDevicePath())
                    {
                        webcam->setCurrentlySelected(false);
                    }
                }
                return;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            auto* selectedLayer = mediaBrowserUI_->getSelectedLayer();
            if (selectedLayer != nullptr)
            {
                // Remove the selected layer
                layerManager_->removeLayer(selectedLayer->id);
            }
        }
    }
}

// Converts an int64 duration (microSecs) to a color ranging from green to red
ImVec4 getProfileColorFromDuration(int64_t duration)
{
    constexpr int64_t GreenThreshold = 10000; // 10ms
    constexpr int64_t RedThreshold = 40000;   // 40ms

    // Clamp and normalize duration thresholds
    float t = NAN;
    if (duration <= GreenThreshold)
    {
        t = 0.0f;
    }
    else if (duration >= RedThreshold)
    {
        t = 1.0f;
    }
    else
    {
        t = static_cast<float>(duration - GreenThreshold) / static_cast<float>(RedThreshold - GreenThreshold);
    }

    // Interpolate green to red
    const float r = common::lerp(0.0f, 1.0f, t);
    const float g = common::lerp(1.0f, 0.0f, t);
    const float b = 0.2f;
    const float a = 1.0f;

    return {r, g, b, a};
}

// Helper function to find the topmost layer under mouse position
Layer* UI::findLayerUnderMouse(const std::vector<Layer>& layers, const ImVec2& mousePos)
{
    // Iterate from topmost to bottom (reverse order)
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i)
    {
        const Layer& layer = layers[i];
        const float lx = layer.x;
        const float ly = layer.y;
        float lw = 0;
        float lh = 0;

        if (layer.type == LayerType::IMAGE && layer.img)
        {
            lw = static_cast<float>(layer.img->info.width);
            lh = static_cast<float>(layer.img->info.height);
        }
        else if (layer.type == LayerType::GIF && layer.gif)
        {
            // Use the first frame's dimensions for GIFs
            if (layer.gif->frames().empty())
            {
                continue; // Skip empty GIFs
            }
            lw = static_cast<float>(layer.gif->frames()[0]->info.width);
            lh = static_cast<float>(layer.gif->frames()[0]->info.height);
        }
        else if (layer.type == LayerType::TEXT)
        {
            if (layer.img)
            {
                lw = static_cast<float>(layer.img->info.width);
                lh = static_cast<float>(layer.img->info.height);
            }
        }

        if (mousePos.x >= lx && mousePos.x <= lx + lw && mousePos.y >= ly && mousePos.y <= ly + lh)
        {
            return const_cast<Layer*>(&layer);
        }
    }
    return nullptr;
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
    const ImVec2 mousePos = ImGui::GetIO().MousePos;

    // On mouse click, select the topmost layer under the mouse
    if (ImGui::IsMouseClicked(0))
    {
        Layer* clickedLayer = findLayerUnderMouse(layers, mousePos);
        if (clickedLayer != nullptr)
        {
            // Select this layer, deselect others
            for (auto& l : layers)
            {
                l.setSelected(false);
            }
            clickedLayer->setSelected(true);
        }
        else
        {
            // If no layer was found, deselect all layers
            for (auto& l : layers)
            {
                l.setSelected(false);
            }
        }
    }

    // On double-click, open the media browser if it's closed
    if (ImGui::IsMouseDoubleClicked(0))
    {
        Layer* doubleClickedLayer = findLayerUnderMouse(layers, mousePos);
        if (doubleClickedLayer != nullptr)
        {
            // Double-click detected on a layer - open media browser if closed
            if (!mediaBrowserVisible_)
            {
                mediaBrowserVisible_ = true;
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
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    // Only allow dragging if mouse is dragging, layer is selected, and layer is not locked
    if (ImGui::IsMouseDragging(0) && !selectedLayer.locked)
    {
        selectedLayer.moveBy(delta.x, delta.y);
    }
}

