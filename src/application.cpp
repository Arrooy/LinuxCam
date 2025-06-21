#include "LinuxFace/application.h"

#include <csignal>
#include <iostream>
#include <memory>

#include "LinuxFace/common.h"
#include "LinuxFace/depthImage.h"
#include "LinuxFace/dlibDetectors.h"
#include "LinuxFace/inputWebcam.h"
#include "config.hpp"
using namespace linuxface;


// TODO: test models:
// pipnet68/xx
// Maybe nanodet_m.onnx (Fast yolo)
// mobile_hair_seg_hairmattenetv1_224x224
// Mirar test_lite_facefusion_pipeline
// 2dfan4.onnx (landmarks 68)
// emotion face-emotion-recognition-enet_b0_8_best_afew.onnx
// Matting BGMv2_mobilenetv2-512x512-full.onnx

// precise depth https://github.com/yvanyin/metric3d
// fast depth https://github.com/ibaiGorordo/ONNX-FastACVNet-Depth-Estimation


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
            continue;
        }

        if (!webcam->start())
        {
            common::log_error("Failed to start webcam: %s", wc.name.c_str());
            continue;
        }
        webcam->setCurrentlySelected(true);
        if (!cameraManager_->addCamera(std::move(webcam)))
        {
            common::log_error("Failed to add webcam: %s", wc.name.c_str());
            continue;
        }
    }

    auto available_device_paths = cameraManager_->discoverAvailableVideoDevices();
    for (const auto& device_path : available_device_paths)
    {
        std::shared_ptr<InputWebcam> webcam = std::make_shared<InputWebcam>("", device_path, 0, 0, 2);
        if (!webcam->setupDevice())
        {
            common::log_error("Failed to setup webcam: %s", device_path.c_str());
            continue;
        }
        if (!cameraManager_->addCamera(std::move(webcam)))
        {
            common::log_error("Failed to add webcam: %s", device_path.c_str());
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

    // Just here for benchmarking.
    // faceDetector_ = std::make_unique<DlibFaceDetector>();
    std::string models_folder = Config::getInstance().getModelFolderPath();
    std::string var_onnx_path = models_folder + "fsanet-var.onnx";
    // fsanetDetectorVar_ = std::make_unique<FsanetDetector>(var_onnx_path);

    std::string conv_onnx_path = models_folder + "fsanet-1x1.onnx";
    // fsanetDetectorConv_ = std::make_unique<FsanetDetector>(conv_onnx_path); // Seems like 1x1 is very bad

    std::string scrfd_10g_bnkps_path = models_folder + "scrfd_10g_bnkps_shape640x640.onnx";

    // 1ms time inference
    std::string scrfd_500m_bnkps_path = models_folder + "scrfd_500m_bnkps_shape640x640.onnx";
    // scrfdDetector_ = std::make_unique<SCRFDetector>(scrfd_500m_bnkps_path);

    std::string modnet_onnx_path = models_folder + "modnet.onnx";
    // modnetDetector_ = std::make_unique<MODNetDetector>(modnet_onnx_path);

    std::string rvm_model = models_folder + "rvm_mobilenetv3_fp32.onnx";

    rvmDetector_ = std::make_unique<RobustVideoMatting>(rvm_model);

    std::string media_folder = Config::getInstance().getMediaFolderPath();
    testImg_ = ImageLoader::loadImageFromFile(media_folder + "example.jpg");

    // Pass pointer instead of reference
    ui_.connect(cameraManager_);

    gif_ = std::make_shared<GifReader>(media_folder + "first.gif");
    // if (!gif_->decodeAllFrames())
    // {
    //     common::log_error("Failed to decode giff frames.");
    //     return false;
    // }
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
        render();
    }

    cameraManager_->shutdown();

    common::log_info("Main loop ended");
}


void Application::update()
{
    // Poll events
    window_.pollEvents();

    std::unique_ptr<Image> image;
    if (cameraManager_->updateInput(image))
    {
        if (gif_->isOpen() && gif_->hasNext())
        {
            auto& gif_image = gif_->next();
            if (gif_image != nullptr)
            {
                image->paste(*gif_image);
            }
        }
        process(image);
        // TODO: Since this is very time consuming, we could make it a thread...
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
    imageRender_.renderBackground(width, height);

    // Render UI
    ui_.render();

    // Swap buffers
    window_.swapBuffers();
}

void Application::process(std::unique_ptr<Image>& image)
{
    std::vector<Face> dlib_faces;
    if (faceDetector_ != nullptr)
    {
        dlib_faces = faceDetector_->detect(image);
    }

    std::vector<Face> scrfd_faces;
    if (scrfdDetector_ != nullptr)
    {
        if (scrfdDetector_->isReady())
        {
            scrfd_faces = scrfdDetector_->detect(image);
        }
    }

    if (modnetDetector_ && modnetDetector_->isReady())
    {
        std::unique_ptr<Image> matting = image->deepCopy();
        modnetDetector_->detect(image, matting);
        matting->info.x = image->info.width;
        matting->info.y = 0;
        image->paste(*matting, true);
    }

    if (rvmDetector_ && rvmDetector_->isReady())
    {
        // If resize occurs, rvm is not able to handle that, we just reset it.
        if (!rvmDetector_->isImageCompatible(image))
        {
            rvmDetector_.reset();
            rvmDetector_ = std::make_unique<RobustVideoMatting>(Config::getInstance().getModelFolderPath()
                                                                + "rvm_mobilenetv3_fp32.onnx");
        }

        Profiler::getInstance().start("RVM", "App deep copy");
        std::unique_ptr<Image> matting = image->deepCopy();
        std::unique_ptr<Image> foreground = image->deepCopy();
        Profiler::getInstance().stop("RVM", "App deep copy");

        rvmDetector_->detect(image, foreground, matting);
        Profiler::getInstance().start("RVM", "App paste");
        // use matting layer with foreground layer
        if (testImg_ && !image->isCompatible(*testImg_))
        {
            testImg_ = testImg_->scale(image->info.width, image->info.height);
            common::log_info("Scaling test image to %dx%d", image->info.width, image->info.height);
        }
        foreground->changeBackgroundImage(*matting, *testImg_);
        foreground->info.x = 0;
        foreground->info.y = image->info.height;
        image->paste(*foreground, true);

        Profiler::getInstance().stop("RVM", "App paste");
    }

    // Paint results:
    for (const auto& face : dlib_faces)
    {
        face.paintBoundingBox(image, Pixel(255, 0, 0));
    }

    for (auto& face : scrfd_faces)
    {
        if (fsanetDetectorVar_ && fsanetDetectorVar_->isReady())
        {
            fsanetDetectorVar_->detect(image, face);
            face.paintPoseAxis(image, 60, 3);
        }
        if (fsanetDetectorConv_ && fsanetDetectorConv_->isReady())
        {
            fsanetDetectorConv_->detect(image, face);
            face.paintPoseAxis(image, 30, 3, true);
        }
        face.paintBoundingBox(image, Pixel(0, 255, 0));
    }
    imageRender_.uploadImage(image);
}

void Application::shutdown()
{
    // UI and Window destructors will handle cleanup automatically
}

void Application::runSingleShot()
{
    // Poll events
    window_.pollEvents();

    // Load test image using the improved interface
    testImg_ = ImageLoader::loadImageFromFile("/home/arroyo/Documents/Projectes/LinuxFace/example.jpg");
    if (testImg_ == nullptr)
    {
        return;
    }

    // do test here, modify testImg

    imageRender_.uploadImage(testImg_);

    // Start new UI frame
    ui_.newFrame();

    // Paint UI elements
    ui_.paint();
    render();

    while (!window_.shouldClose() && !g_should_exit)
    {
        sleep(1);
    }
}
