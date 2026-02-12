#include "LinuxFace/ui.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <glad/glad.h>
#include <mutex>
#include <thread>

#include "LinuxFace/Image/text_renderer.h"
#include "LinuxFace/UI/paintWebcam.h"
#include "LinuxFace/application.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"

// Web search client
#include "LinuxFace/Image/urlImageDownloader.h"
#include "LinuxFace/webscraping/pexelsAPI.h"
#include "config.hpp"

using namespace linuxface;

ImVec4 getProfileColorFromDuration(int64_t duration);
ImVec4 getProfileColorFromDurationWithRange(int64_t duration, const Profiler::ColorRange& range);

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

void UI::shutdown()
{
    if (!ready_)
    {
        common::logError("UI is not initialized, cannot shutdown.");
        return;
    }

    // Clean up any GL textures created for Pexels thumbnails
    cleanupPexelsTextures();

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
    {
        Profiler::ScopedProfilerSpan span("UI", "paintMainWindow");
        paintMainWindow();
    }
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
            ImGui::MenuItem("Toggle Media Settings", nullptr, &mediaBrowserVisible_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Load media..."))
        {
            ImGui::Text("Media categories");
            ImGui::Separator();
            ImGui::Spacing();

            renderCollapsingHeader("Images", mediaManager_->getImageNames(), "image");
            renderCollapsingHeader("GIFs", mediaManager_->getGifNames(), "gif");
            renderCollapsingHeader("Videos", mediaManager_->getVideoNames(), "video");
            ImGui::EndMenu();
        }

        // Pexels search menu (text box + search button)
        if (ImGui::BeginMenu("Search Images"))
        {
            static char search_buf[128] = "";
            static int numResults = 6;

            ImGui::InputText("Query", search_buf, sizeof(search_buf));

            // Select desired Pexels source variant (default: Original)
            static int selectedPexelsSrc = static_cast<int>(PexelsSrc::ORIGINAL);
            const auto& srcItems = PexelsSrcHelper::names();

            ImGui::Separator();
            ImGui::PushItemWidth(140);
            ImGui::Combo("Source", &selectedPexelsSrc, srcItems.data(), static_cast<int>(srcItems.size()));
            ImGui::PopItemWidth();
            ImGui::Separator();
            if (ImGui::Button("Search") && search_buf[0] != '\0')
            {
                try
                {
                    const std::string apiKey = Config::getInstance().getEnv("PEXELS_API_KEY", "");
                    if (apiKey.empty())
                    {
                        common::logWarn("PEXELS_API_KEY not set — cannot perform search");
                    }
                    else
                    {
                        // Perform search (quick, network IO) and then download thumbnails asynchronously
                        PexelsAPI client(apiKey);
                        auto results = client.search(std::string(search_buf), numResults);

                        // Clear previous results and show results window
                        {
                            const std::lock_guard<std::mutex> lock(pexelsMutex_);
                            pexelsResults_.clear();
                            showPexelsResults_ = true;
                            pexelsLoading_ = true;
                        }

                        // Background thread: download each image and store full + scaled thumb
                        int selectedPexelsSrcLocal =
                            selectedPexelsSrc; // copy static into automatic storage for capture
                        std::thread(
                            [this, results, selectedPexelsSrcLocal]() mutable
                            {
                                UrlImageDownloader downloader; // reuse decoder internally
                                const PexelsSrc desiredSrc = static_cast<PexelsSrc>(selectedPexelsSrcLocal);
                                for (const auto& r : results)
                                {
                                    try
                                    {
                                        // Choose URL for the requested variant
                                        std::string srcUrl = r.getUrlFor(desiredSrc);
                                        if (srcUrl.empty())
                                        {
                                            common::logWarn(
                                                "Pexels: requested variant not available for this photo, skipping");
                                            continue;
                                        }

                                        std::unique_ptr<Image> downloaded;
                                        if (!downloader.downloadToImage(srcUrl, downloaded, 8))
                                        {
                                            common::logWarn("Pexels: failed to download %s", srcUrl.c_str());
                                            continue;
                                        }

                                        // Create shared_ptr for storage
                                        // Keep full-size image (move unique_ptr into a shared_ptr via shared_ptr
                                        // constructor)
                                        std::shared_ptr<Image> full = std::move(downloaded->deepCopy());

                                        // Create thumbnail preserving aspect ratio (fit into pexelThumbW_ x pexelThumbH_)
                                        std::shared_ptr<Image> thumb;
                                        if (downloaded && downloaded->info.width > 0 && downloaded->info.height > 0)
                                        {
                                            const double maxW = static_cast<double>(pexelThumbW_);
                                            const double maxH = static_cast<double>(pexelThumbH_);
                                            const double srcW = static_cast<double>(downloaded->info.width);
                                            const double srcH = static_cast<double>(downloaded->info.height);
                                            double scale = std::min(maxW / srcW, maxH / srcH);
                                            // avoid upscaling small images
                                            scale = std::min(1.0, scale);
                                            auto thumbUnique = downloaded->scale(scale);
                                            if (thumbUnique)
                                            {
                                                thumb = std::move(thumbUnique);
                                            }
                                        }

                                        PexelsItem item;
                                        item.full = full;
                                        item.thumb = thumb ? thumb : full; // fallback
                                        item.texId = 0;
                                        // store original src map key used so we can display source in UI if needed
                                        item.url = srcUrl;

                                        const std::lock_guard<std::mutex> lock(pexelsMutex_);
                                        pexelsResults_.push_back(std::move(item));
                                    }
                                    catch (const std::exception& ex)
                                    {
                                        common::logWarn("Pexels download exception: %s", ex.what());
                                    }
                                }

                                // Mark loading finished
                                {
                                    const std::lock_guard<std::mutex> lock(pexelsMutex_);
                                    pexelsLoading_ = false;
                                }
                            })
                            .detach();

                        if (results.empty())
                        {
                            common::logInfo("Pexels: no results for '%s'", search_buf);
                        }
                    }
                }
                catch (const std::exception& ex)
                {
                    common::logError("Pexels search failed: %s", ex.what());
                }
            }

            ImGui::SliderInt("Results", &numResults, 1, 30);
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

            ImGui::SliderInt("Font Scale", &textScale, 1, 25, "%dx");
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
        const float defaultHeight = std::min(500.0f, io.DisplaySize.y * 0.8f);
        ImGui::SetNextWindowSize(ImVec2(defaultWidth, defaultHeight), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Profiler", &show_profiler_, ImGuiWindowFlags_None))
        {
            const ImGuiIO& io = ImGui::GetIO();
            const auto frameDuration = 1.0f / io.Framerate;
            const auto frameDurationMicros =
                static_cast<int64_t>(frameDuration * 1000000.0f); // Convert to microseconds
            Profiler::ColorRange fpsRange(std::chrono::microseconds(16000), std::chrono::microseconds(25000));
            ImGui::TextColored(getProfileColorFromDurationWithRange(frameDurationMicros, fpsRange),
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
                                  "not all-time values. Reset clears all collected data.\n"
                                  "Color ranges define green/red thresholds for visual feedback.");
            }

            ImGui::Separator();

            // Reserve vertical space and split area between call tree and stats table so they can scroll independently
            const float totalAvailY = ImGui::GetContentRegionAvail().y;
            // Allocate roughly 40% to the call tree, but clamp to reasonable bounds
            const float callTreeHeight = std::clamp(totalAvailY * 0.40f, 120.0f, 400.0f);

            // Display hierarchical call tree inside a scrollable child region
            if (ImGui::CollapsingHeader("Call Tree Hierarchy"))
            {
                ImGui::BeginChild("ProfilerCallTreeChild", ImVec2(0.0f, callTreeHeight), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);

                const auto& callTree = Profiler::getInstance().getCurrentCallTree();

                if (!callTree.children.empty() || callTree.name != "empty")
                {
                    // Helper function to render a call tree node recursively
                    std::function<void(const Profiler::CallTreeNode&, int)> renderCallTreeNode =
                        [&](const Profiler::CallTreeNode& node, int depth)
                    {
                        // Create a unique ID for this node
                        std::string nodeId =
                            node.name + "##" + std::to_string(depth) + "_" + std::to_string(&node - &callTree);

                        // Format the display text
                        char buffer[256];
                        if (node.inclusive_ms > 0)
                        {
                            snprintf(buffer, sizeof(buffer), "%s (Inc: %.2f ms, Exc: %.2f ms)", node.name.c_str(),
                                     node.inclusive_ms / 1000.0f, node.exclusive_ms / 1000.0f);
                        }
                        else
                        {
                            snprintf(buffer, sizeof(buffer), "%s", node.name.c_str());
                        }

                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                                   | ImGuiTreeNodeFlags_DefaultOpen;
                        if (node.children.empty())
                        {
                            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        }

                        bool isOpen = ImGui::TreeNodeEx(nodeId.c_str(), flags, "%s", buffer);

                        if (isOpen && !node.children.empty())
                        {
                            for (const auto& child : node.children)
                            {
                                renderCallTreeNode(child, depth + 1);
                            }
                            ImGui::TreePop();
                        }
                    };

                    // Render the call tree starting from root
                    renderCallTreeNode(callTree, 0);
                }
                else
                {
                    ImGui::Text("No call tree data available");
                }

                ImGui::EndChild();

                ImGui::Separator();
                if (ImGui::Button("Reset Call Tree"))
                {
                    Profiler::getInstance().resetCallTree();
                }
                ImGui::SameLine();
                if (ImGui::Button("Capture Next Loop"))
                {
                    if (application_)
                    {
                        application_->requestLoopCapture();
                    }
                    else
                    {
                        common::logWarn("Application not connected to UI - cannot capture loop");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Build Call Tree (All)"))
                {
                    Profiler::getInstance().forceRebuildCallTree();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Reset: Clear all call tree data\n"
                                      "Capture Next Loop: Profile only the next application loop iteration\n"
                                      "Build Call Tree (All): Build tree from all accumulated events\n\n"
                                      "Inc = Inclusive time (total time spent in function including children)\n"
                                      "Exc = Exclusive time (time spent only in this function, excluding children)");
                }
            }
            auto allStats = Profiler::getInstance().getAllTimerStatistics();

            // Sort by current duration for display
            std::vector<std::pair<std::string, Profiler::TimerStatistics>> sortedStats;
            sortedStats.reserve(allStats.size());

            for (const auto& pair : allStats)
            {
                sortedStats.emplace_back(pair.first, pair.second);
            }

            std::sort(sortedStats.begin(), sortedStats.end(),
                      [](const auto& a, const auto& b) { return a.second.average > b.second.average; });

            if (!sortedStats.empty())
            {
                // Use scrollable table with flexible columns that auto-resize to content
                // Constrain table height to leave space for recap
                const float tableHeight = std::min(300.0f, ImGui::GetContentRegionAvail().y - 120.0f);
                if (ImGui::BeginTable("ProfilerStats", 4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                                          | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                                      ImVec2(0.0f, tableHeight))) // Auto-fit content
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
                        const auto& timerKey = pair.first;

                        // Get custom color range for this timer
                        const auto colorRange = Profiler::getInstance().getColorRange(timerKey);

                        ImGui::TableNextRow();

                        // Timer name
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", timerKey.c_str());

                        // Current duration with custom color range
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(getProfileColorFromDurationWithRange(stats.current.count(), colorRange),
                                           "%s", Profiler::formatDuration(stats.current).c_str());

                        // Average duration with custom color range
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(getProfileColorFromDurationWithRange(stats.average.count(), colorRange),
                                           "%s", Profiler::formatDuration(stats.average).c_str());

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
            if (ImGui::CollapsingHeader("Profiler Coloring"))
            {
                static char sourceBuffer[128] = "";
                static char nameBuffer[128] = "";
                static float manualGreenMs = 10.0f;
                static float manualRedMs = 40.0f;
                static int selectedExistingTimer = -1;

                // Get current statistics for dropdown
                auto currentStats = Profiler::getInstance().getAllTimerStatistics();

                // Create list of existing timer keys
                std::vector<std::string> existingKeys;
                existingKeys.reserve(currentStats.size());
                for (const auto& pair : currentStats)
                {
                    existingKeys.push_back(pair.first);
                }

                // Sort keys alphabetically for better UX
                std::sort(existingKeys.begin(), existingKeys.end());

                // Dropdown for existing timers
                if (!existingKeys.empty())
                {
                    ImGui::Text("Select from existing timers:");
                    // ImGui::PushItemWidth(300);

                    // Create array of const char* for ImGui::Combo
                    std::vector<const char*> keysCStr;
                    keysCStr.reserve(existingKeys.size());
                    for (const auto& key : existingKeys)
                    {
                        keysCStr.push_back(key.c_str());
                    }

                    if (ImGui::Combo("##ExistingTimers", &selectedExistingTimer, keysCStr.data(),
                                     static_cast<int>(keysCStr.size())))
                    {
                        // Timer selected from dropdown - load its current configuration
                        if (selectedExistingTimer >= 0 && selectedExistingTimer < static_cast<int>(existingKeys.size()))
                        {
                            const std::string& selectedKey = existingKeys[selectedExistingTimer];
                            auto colorRange = Profiler::getInstance().getColorRange(selectedKey);

                            // Update manual configuration with selected timer's values
                            manualGreenMs = colorRange.greenThreshold.count() / 1000.0f;
                            manualRedMs = colorRange.redThreshold.count() / 1000.0f;

                            // Parse the key to populate source/name fields
                            size_t separatorPos = selectedKey.find("::");
                            if (separatorPos != std::string::npos)
                            {
                                std::string source = selectedKey.substr(0, separatorPos);
                                std::string name = selectedKey.substr(separatorPos + 2);

                                strncpy(sourceBuffer, source.c_str(), sizeof(sourceBuffer) - 1);
                                sourceBuffer[sizeof(sourceBuffer) - 1] = '\0';
                                strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer) - 1);
                                nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                            }
                        }
                    }
                    // ImGui::PopItemWidth();
                }
                if (selectedExistingTimer >= 0 && selectedExistingTimer < static_cast<int>(existingKeys.size()))
                {
                    ImGui::Spacing();

                    // Color range inputs
                    // ImGui::PushItemWidth(120);
                    ImGui::InputFloat("Green Threshold (ms)", &manualGreenMs, 0.1f, 1.0f, "%.1f");
                    ImGui::InputFloat("Red Threshold (ms)", &manualRedMs, 0.1f, 1.0f, "%.1f");
                    // ImGui::PopItemWidth();

                    ImGui::Spacing();
                    if (ImGui::Button("Reset Color Ranges"))
                    {
                        Profiler::getInstance().resetColorRanges();
                    }
                    ImGui::SameLine();

                    static float confirmationTimer = 0.0f;

                    if (ImGui::Button("Apply Configuration"))
                    {
                        // Validate inputs
                        manualGreenMs = std::max(0.1f, manualGreenMs);
                        manualRedMs = std::max(manualGreenMs + 0.1f, manualRedMs);

                        std::string timerKey = std::string(sourceBuffer) + "::" + std::string(nameBuffer);


                        if (!timerKey.empty() && timerKey != "::")
                        {
                            auto greenMicros = std::chrono::microseconds(static_cast<int64_t>(manualGreenMs * 1000));
                            auto redMicros = std::chrono::microseconds(static_cast<int64_t>(manualRedMs * 1000));

                            Profiler::getInstance().setColorRange(sourceBuffer, nameBuffer, greenMicros, redMicros);
                            confirmationTimer = ImGui::GetTime() + 2.0f; // Show confirmation for 2 seconds
                        }
                    }

                    // Show timed confirmation message
                    if (ImGui::GetTime() < confirmationTimer)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Applied!");
                    }
                }
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

    // Render Pexels search results window (grid of thumbnails)
    {
        const std::lock_guard<std::mutex> lock(pexelsMutex_);
        if (showPexelsResults_)
        {
            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Search Results", &showPexelsResults_, ImGuiWindowFlags_None))
            {
                if (pexelsLoading_)
                {
                    ImGui::Text("Downloading images...");
                    ImGui::Separator();
                }

                const float thumbW = static_cast<float>(pexelThumbW_);
                const float thumbH = static_cast<float>(pexelThumbH_);
                const float padding = 6.0f;

                // Let ImGui position thumbnails with a simple wrapping layout and 6px padding.
                // This preserves aspect ratio for thumbnails and avoids a strict column grid.
                const float paddingInner = padding; // 6.0f
                const float availXInner = ImGui::GetContentRegionAvail().x;
                float currentLineX = 0.0f;

                for (size_t i = 0; i < pexelsResults_.size(); ++i)
                {
                    auto& item = pexelsResults_[i];

                    // Ensure thumbnail GL texture exists (create on main thread)
                    if (item.thumb && item.texId == 0)
                        item.texId = createGLTextureFromImage(item.thumb);

                    // Determine display size (use actual thumbnail size to keep aspect)
                    float displayW = thumbW;
                    float displayH = thumbH;
                    if (item.thumb)
                    {
                        displayW = static_cast<float>(item.thumb->info.width);
                        displayH = static_cast<float>(item.thumb->info.height);
                    }

                    // Clamp to available width so drawing always stays inside the window
                    displayW = std::min(displayW, availXInner);

                    // Place next item on the same line only when it still fits.
                    if (i > 0)
                    {
                        const float nextWidth = currentLineX + paddingInner + displayW;
                        if (nextWidth <= availXInner)
                        {
                            ImGui::SameLine(0.0f, paddingInner);
                            currentLineX += paddingInner;
                        }
                        else
                        {
                            currentLineX = 0.0f;
                        }
                    }

                    ImGui::PushID(static_cast<int>(i));

                    if (item.texId != 0 && item.thumb)
                    {
                        ImGui::Image((ImTextureID)(intptr_t)item.texId, ImVec2(displayW, displayH));
                    }
                    else
                    {
                        ImGui::Dummy(ImVec2(thumbW, thumbH));
                    }

                    // Click handler (click on the thumbnail cell)
                    if (ImGui::IsItemClicked())
                    {
                        if (item.full && application_)
                        {
                            auto copy = item.full->deepCopy();
                            if (copy)
                            {
                                common::logInfo("Using image from Pexels search for face swap: %s", item.url.c_str());
                                application_->setTargetImage(std::move(copy));
                            }
                        }
                    }

                    ImGui::PopID();

                    // Track consumed width in the current row
                    currentLineX += displayW;
                }
            }
            ImGui::End();
        }
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
                        common::logWarn(
                            "UI - Application pointer not connected, adding target image as a layer instead");
                    }
                }
                ImGui::EndGroup();
            }
            else if (type == "video")
            {
                ImGui::SameLine();
                ImGui::BeginGroup();
                if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
                {
                    auto origVideo = mediaManager_->getVideo(item);
                    if (origVideo && layerManager_)
                    {
                        Layer newLayer;
                        newLayer.id = Layer::getNextId();
                        newLayer.type = LayerType::VIDEO;
                        newLayer.name = item;
                        newLayer.video = origVideo;
                        newLayer.setPosition(100, 100);
                        newLayer.videoFrameIndex = 0;
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
            switch (webcam->getType())
            {
                case WebcamType::VIRTUAL_INPUT:
                    tabName = "(Vir) " + tabName;
                    break;
                case WebcamType::VIRTUAL_OUTPUT:
                    tabName += "(Out) " + tabName;
                    break;
                default:
                case WebcamType::PHYSICAL_INPUT:
                    tabName += "(Phy) " + tabName;
                    break;
            }

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

// Converts an int64 duration (microSecs) to a color ranging from green to red using custom thresholds
ImVec4 getProfileColorFromDurationWithRange(int64_t duration, const Profiler::ColorRange& range)
{
    const int64_t greenThreshold = range.greenThreshold.count();
    const int64_t redThreshold = range.redThreshold.count();

    // Clamp and normalize duration thresholds
    float t = NAN;
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
    const float r = common::lerp(0.0f, 1.0f, t);
    const float g = common::lerp(1.0f, 0.0f, t);
    const float b = 0.2f;
    const float a = 1.0f;

    return {r, g, b, a};
}

// Converts an int64 duration (microSecs) to a color ranging from green to red using default thresholds
ImVec4 getProfileColorFromDuration(int64_t duration)
{
    // Use default color range (10ms green, 40ms red)
    return getProfileColorFromDurationWithRange(duration, Profiler::ColorRange());
}

// Create a GL texture from an Image (must be called from the main / GL thread)
GLuint UI::createGLTextureFromImage(const std::shared_ptr<Image>& img)
{
    if (!img || img->empty() || img->info.width == 0 || img->info.height == 0)
    {
        return 0;
    }

    GLenum format = (img->info.pixelSizeBytes == 4) ? GL_RGBA : GL_RGB;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, static_cast<GLsizei>(img->info.width),
                 static_cast<GLsizei>(img->info.height), 0, format, GL_UNSIGNED_BYTE, img->data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}


void UI::cleanupPexelsTextures()
{
    const std::lock_guard<std::mutex> lock(pexelsMutex_);
    for (auto& it : pexelsResults_)
    {
        if (it.texId != 0)
        {
            GLuint t = it.texId;
            glDeleteTextures(1, &t);
            it.texId = 0;
        }
    }
    pexelsResults_.clear();
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
        else if (layer.type == LayerType::VIDEO && layer.video)
        {
            // Use video dimensions
            lw = static_cast<float>(layer.video->getMetadata().width);
            lh = static_cast<float>(layer.video->getMetadata().height);
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

