#include "ui.h"

#include "common.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using namespace funnyface;

UI::UI() : showDebugWindow_(true)
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
    // Paint all UI windows
    paintMainWindow();

    if (showDebugWindow_)
    {
        paintDebugWindow();
    }
}

void UI::render()
{
    // Render everything
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UI::paintMainWindow()
{
    ImGui::Begin("Hello, world!");

    ImGui::Text("This is some useful text.");
    ImGui::Checkbox("Debug window", &showDebugWindow_);

    // ImGui::SliderFloat("float", &m_floatValue, 0.0f, 1.0f);
    // ImGui::ColorEdit3("clear color", (float*) &m_clearColor);

    // if (ImGui::Button("Button"))
    // {
    //     m_counter++;
    // }
    // ImGui::SameLine();
    // ImGui::Text("counter = %d", m_counter);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

    ImGui::End();
}

void UI::paintDebugWindow()
{
    ImGui::Begin("Another Window", &showDebugWindow_);
    ImGui::Text("Hello from another window!");
    if (ImGui::Button("Close Me"))
    {
        showDebugWindow_ = false;
    }
    ImGui::End();
}
