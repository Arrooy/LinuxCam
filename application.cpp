#include "application.h"

#include <iostream>

#include "common.h"
#include "config.hpp"
#include "dlibDetectors.h"

using namespace funnyface;
Application::Application() : profiler_(Profiler::getInstance())
{
}

Application::~Application()
{
    shutdown();
}

bool Application::initialize()
{
    // Initialize window
    if (!window_.initialize())
    {
        common::log_error("Failed to initialize window");
        return false;
    }

    // Initialize UI
    if (!ui_.initialize(window_.getGLFWWindow(), window_.getGLSLVersion()))
    {
        common::log_error("Failed to initialize UI");
        return false;
    }
    cameraManager_ = std::make_unique<CameraManager>();

    // Load configuration for cameraManager
    cameraManager_->setInputDevice(Config::getInstance().getInputCamera());
    cameraManager_->setOutputDevice(Config::getInstance().getOutputCamera());

    // Initialize camera manager
    if (!cameraManager_->initialize())
    {
        common::log_error("Failed to initialize Camera Manager");
        return false;
    }

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << "\n";

    // Initialize image renderer
    if (!imageRender_.initialize())
    {
        common::log_error("Failed to initialize image renderer");
        return false;
    }

    faceDetector_ = std::make_unique<DlibFaceDetector>();

    // Pass pointer instead of reference
    ui_.connect(cameraManager_.get());

    common::log_info("Application initialized successfully");
    return true;
}

void Application::run()
{
    common::log_info("Starting main loop...");

    // Main loop
    while (!window_.shouldClose() && cameraManager_->is_alive())
    {
        update();
        // TODO: FIXME:

        render();
    }

    cameraManager_->shutdown();

    common::log_info("Main loop ended");
}

void Application::shutdown()
{
    // UI and Window destructors will handle cleanup automatically
}

void Application::update()
{
    // Poll events
    window_.pollEvents();

    // TODO: FIXME: This lambda has a Slight performance hit due to heap allocation and type-erasure (especially for
    // small, frequently called callbacks). also: Not inlineable across translation units. GPT suggests using Template
    // based approach (has zero overhead.)
    cameraManager_->update([this](Image& img) { process(img); });

    // Start new UI frame
    ui_.newFrame();

    // Paint UI elements
    ui_.paint();
}

void Application::render()
{
    // Set viewport
    window_.setViewport();

    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render background image first
    int width, height;
    window_.getFramebufferSize(width, height);
    imageRender_.renderBackground(width, height); // TODO: Pending to know why claude send this as param.

    // Render UI
    ui_.render();

    // Swap buffers
    window_.swapBuffers();
}

void Application::process(Image& image)
{
    // if (faceDetector_ptr_ != nullptr)
    // {
    //     profiler_.start("Face detector");
    //     auto faces_rect = faceDetector_ptr_->detect(image);
    //     profiler_.stop("Face detector");

    //     if (faces_rect.size() > 0)
    //     {
    //         profiler_.start("Face painting");
    //         for (const auto& face_rect : faces_rect)
    //         {
    //             Face face(face_rect);
    //             face.paintBoundingBox(image);
    //         }
    //         profiler_.stop("Face painting");
    //     }
    // }
    imageRender_.uploadImage(image);
}
