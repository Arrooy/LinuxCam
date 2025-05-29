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

        //TODO: Make a sliding window fo this graph.
        static float arr[] = {0.6f, 0.1f, 1.0f, 0.5f, 0.92f, 0.1f, 0.2f};
        ImGui::PlotHistogram("Histogram", arr, IM_ARRAYSIZE(arr), 0, NULL, 0.0f, 1.0f, ImVec2(0, 80.0f));

        // Add close button
        if (ImGui::Button("Close"))
        {
            show_profile_window_ = false;
        }
        ImGui::End();
    }
}
