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

    //TODO: ADRIA ARA: THIS SHOULD BE A SHARED POINTER.
    newMenu(cameraManager_->getInputDevice());
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
            ImGui::MenuItem("Toggle Profile Window", NULL, &show_profile_window_);

            // ImGui::Separator();
            // if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ImGui::Text("Main content goes here...");

    // ImGui::End(); // End main window

    // ImGui::SliderFloat("float", &m_floatValue, 0.0f, 1.0f);
    // ImGui::ColorEdit3("clear color", (float*) &m_clearColor);

    // if (ImGui::Button("Button"))
    // {
    //     m_counter++;
    // }
    // ImGui::SameLine();
    // ImGui::Text("counter = %d", m_counter);
    if (show_profile_window_)
    {
        float menu_bar_height = ImGui::GetFrameHeight();
        ImVec2 window_pos = ImVec2(0, menu_bar_height);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

        ImGui::Begin("Profiling window", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

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
            show_profile_window_ = false;
        }
        ImGui::End();
    }
}

void UI::newMenu(CapturingDevice& device)
{
    ImGui::Begin("V4L2 Device Configuration");

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

        // Subsampling combo
        const char* subsampling_items[] = {"4:4:4", "4:2:2", "4:2:0", "Grayscale", "4:4:0", "4:1:1"};
        int current_subsampling = static_cast<int>(device.subsampling);
        if (ImGui::Combo("Subsampling", &current_subsampling, subsampling_items, IM_ARRAYSIZE(subsampling_items)))
        {
            device.subsampling = static_cast<TJSAMP>(current_subsampling);
        }
    }

    // Available Formats
    if (ImGui::CollapsingHeader("Available Formats"))
    {
        static int selected_format = -1;
        static int selected_size = -1;

        for (int fmt_idx = 0; fmt_idx < device.caps.formats.size(); fmt_idx++)
        {
            const Format& format = device.caps.formats[fmt_idx];

            if (ImGui::TreeNode(("Format " + std::to_string(fmt_idx)).c_str()))
            {
                ImGui::Text("Description: %s", format.description.c_str());
                ImGui::Text("Pixel Format: 0x%08X", format.pixelformat);

                if (ImGui::TreeNode("Available Sizes"))
                {
                    for (int size_idx = 0; size_idx < format.sizes.size(); size_idx++)
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

    // Quick Settings
    if (ImGui::CollapsingHeader("Quick Settings"))
    {
        // Buffer count input
        int buffer_count = static_cast<int>(device.buffer_count);
        if (ImGui::InputInt("Buffer Count", &buffer_count, 1, 10))
        {
            if (buffer_count > 0)
            {
                device.buffer_count = static_cast<unsigned int>(buffer_count);
            }
        }

        // Common resolutions combo
        if (!device.caps.formats.empty())
        {
            static const char* common_resolutions[] = {"640x480",  "800x600",   "1024x768",
                                                       "1280x720", "1920x1080", "Custom"};
            static int current_res = 5; // Default to "Custom"

            if (ImGui::Combo("Common Resolutions", &current_res, common_resolutions, IM_ARRAYSIZE(common_resolutions)))
            {
                switch (current_res)
                {
                    case 0:
                        device.width = 640;
                        device.height = 480;
                        break;
                    case 1:
                        device.width = 800;
                        device.height = 600;
                        break;
                    case 2:
                        device.width = 1024;
                        device.height = 768;
                        break;
                    case 3:
                        device.width = 1280;
                        device.height = 720;
                        break;
                    case 4:
                        device.width = 1920;
                        device.height = 1080;
                        break;
                    default:
                        break; // Custom - no change
                }
            }
        }
    }

    ImGui::End();
}
