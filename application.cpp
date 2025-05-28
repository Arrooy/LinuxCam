
#include "application.h"

#include "common.h"
#include "dlibDetectors.h"
#include <iostream>

using namespace funnyface;
Application::Application()
{
}

Application::~Application()
{
    shutdown();
}

bool Application::initialize(int width, int height, const std::string& title)
{
    // Initialize window
    if (!window_.initialize(width, height, title))
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

    if (!cameraManager_.initialize())
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

    faceDetector_ptr_ = std::make_shared<DlibFaceDetector>();

    common::log_info("Application initialized successfully");
    return true;
}

void Application::run()
{
    common::log_info("Starting main loop...");

    // Main loop
    while (!window_.shouldClose() && cameraManager_.is_alive())
    {
        update();
        // TODO: FIXME:

        render();
    }

    cameraManager_.shutdown();

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
    cameraManager_.update([this](Image& img) { process(img); });

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
    if (faceDetector_ptr_ != nullptr)
    {
        
        const auto& a= std::chrono::high_resolution_clock::now();
        auto faces_rect = faceDetector_ptr_->detect(image);
        const auto& b = std::chrono::high_resolution_clock::now();

        std::string detector_sr = common::format_duration(a, b);
        common::log_info("Paint - Face detection time: %s ", detector_sr.c_str());

        if (faces_rect.size() > 0)
        {
            common::log_info("Paint - Found %d faces at %d,%d,%d,%d", faces_rect.size(), faces_rect[0].l,
                             faces_rect[0].t, faces_rect[0].r, faces_rect[0].b);
            for (const auto& face_rect : faces_rect)
            {
                Face face(face_rect);
                face.paintBoundingBox(image);
            }
        }
    }
    imageRender_.uploadImage(image);

    // // Paint a circle in the middle of the image
    // for (int x = 0; x < image.info.width; x++)
    // {
    //     for (int y = 0; y < image.info.height; y++)
    //     {
    //         int pos = (x + y * image.info.width) * image.info.pixelSizeBytes;
    //         if(static_cast<unsigned long>(pos + image.info.pixelSizeBytes - 1) >= image.size)
    //         {
    //             common::log_warn("Paint - Invalid pos x,y %d&%d - index %d. Image size is %d. Skipping.",x,y, pos,
    //             image.size); continue;
    //         }

    //         image.data[pos] = 0;
    //         image.data[pos + 1] = 0;
    //         image.data[pos + 2] = 0;
    //         image.data[pos + 3] = 0;
    //     }
    // }
}
