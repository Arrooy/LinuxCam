#include "FunnyFace/application.h"

#include <csignal>
#include <iostream>
#include <memory>

#include "FunnyFace/common.h"
#include "FunnyFace/dlibDetectors.h"
#include "FunnyFace/inputWebcam.h"
#include "config.hpp"
using namespace funnyface;


std::atomic<bool> g_should_exit{false};

void signalHandler(int signal)
{
    if (signal == SIGINT)
    {
        common::log_warn("Received SIGINT, exiting...");
        g_should_exit = true;
    }
}

Application::Application() : profiler_(Profiler::getInstance())
{
}

Application::~Application()
{
    shutdown();
}

bool Application::initialize()
{
    std::signal(SIGINT, signalHandler);

    cameraManager_ = std::make_shared<CameraManager>();

    auto webcams = Config::getInstance().getWebcams();
    for (const auto& wc : webcams)
    {
        std::shared_ptr<Webcam> webcam;

        if (wc.is_input)
        {
            webcam = std::make_shared<InputWebcam>(wc.name, wc.device_path, wc.width, wc.height, wc.buffer_count);
        }
        else
        {
            webcam = std::make_shared<V4L2LoopbackWriter>(wc.name, wc.device_path, wc.width, wc.height, wc.subsampling);
        }
        if (!webcam->setupDevice())
        {
            common::log_error("Failed to setup webcam: %s", wc.name.c_str());
            return false;
        }

        if (!webcam->start())
        {
            common::log_error("Failed to start webcam: %s", wc.name.c_str());
            return false;
        }

        if (!cameraManager_->addCamera(std::move(webcam)))
        {
            common::log_error("Failed to add webcam: %s", wc.name.c_str());
            return false;
        }
    }

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

    faceDetector_ = std::make_unique<DlibFaceDetector>();

    // Pass pointer instead of reference
    ui_.connect(cameraManager_);


    gif_ = std::make_shared<GifReader>("/home/arroyo/Documents/Projectes/FunnyFace/first.gif");
    if (!gif_->decodeAllFrames())
    {
        common::log_error("Failed to decode giff frames.");
        return false;
    }
    ui_.connect(gif_);

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << "\n";

    // Initialize image renderer
    if (!imageRender_.initialize())
    {
        common::log_error("Failed to initialize image renderer");
        return false;
    }

    common::log_info("Application initialized successfully");
    return true;
}

void Application::run()
{
    common::log_info("Starting main loop...");

    // Main loop
    while (!window_.shouldClose() && !g_should_exit)
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

    std::unique_ptr<Image> image;
    if (cameraManager_->updateInput(image))
    {
        if (gif_->isOpen())
        {
            auto& gif_image = gif_->next();
            image->paste(*gif_image);
        }
        process(image);
        if (!cameraManager_->updateOutput(image))
        {
            common::log_error("Failed to update output cameras");
        }
    }

    ui_.handleKeyboard();

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

void Application::process(std::unique_ptr<Image>& image)
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
    Profiler::getInstance().start("1", "Processing time");
    imageRender_.uploadImage(image);
    Profiler::getInstance().stop("1", "Processing time");
}
