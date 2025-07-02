#include "LinuxFace/application.h"

#include <csignal>
#include <iostream>
#include <memory>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/common.h"
#include "LinuxFace/depthImage.h"
#include "LinuxFace/dlibDetectors.h"
#include "LinuxFace/inputWebcam.h"
#include "LinuxFace/onnx/swapPipeline.h"
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

// inswapper 128 https://huggingface.co/ezioruan/inswapper_128.onnx/tree/main
// inswapper 128
// https://drive.usercontent.google.com/download?id=1krOLgjW2tAPaqV-Bw4YALz0xT5zlb5HF&export=download&authuser=0 PLEASE
// VERIFY Checksum md5sum ./inswapper_128.onnx a3a155b90354160350efd66fed6b3d80  ./inswapper_128.onnx

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

    // Initialize window
    if (!window_.initialize())
    {
        common::log_error("Failed to initialize window");
        return false;
    }

    layerManager_ = std::make_shared<LayerManager>();

    // Initialize UI with LayerManager
    ui_ = std::make_unique<UI>(layerManager_);
    if (!ui_->initialize(window_.getGLFWWindow(), window_.getGLSLVersion()))
    {
        common::log_error("Failed to initialize UI");
        return false;
    }

    // Display loading screen
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ui_->loadingScreen();
    window_.swapBuffers();
    window_.pollEvents();

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
    scrfdDetector_ = std::make_shared<SCRFDetector>(scrfd_500m_bnkps_path);

    std::string modnet_onnx_path = models_folder + "modnet.onnx";
    // modnetDetector_ = std::make_unique<MODNetDetector>(modnet_onnx_path);

    std::string rvm_model = models_folder + "rvm_mobilenetv3_fp32.onnx";
    // rvmDetector_ = std::make_unique<RobustVideoMatting>(rvm_model);


    // ArcFace recognizer initialization
    std::string arcface_model = models_folder + "arcface_w600k_r50.onnx";
    arcfaceRecognizer_ = std::make_shared<ArcfaceRecognizer>(arcface_model);

    // InSwapper initialization
    std::string inswapper_model = models_folder + "inswapper_128.onnx";
    inswapper_ = std::make_shared<InSwapper>(inswapper_model);

    // Initialize SwapPipeline after all models are loaded
    swapPipeline_ = std::make_unique<SwapPipeline>(inswapper_, arcfaceRecognizer_, scrfdDetector_);
    // Pass pointer instead of reference
    ui_->connect(cameraManager_);

    common::log_info("OpenGL version: %s", glGetString(GL_VERSION));

    // Initialize image renderer
    imageRender_ = std::make_shared<ImageRenderGL>();
    if (!imageRender_ || !imageRender_->initialize())
    {
        common::log_error("Failed to initialize image renderer");
        return false;
    }

    mediaManager_ = std::make_shared<MediaManager>(imageRender_);
    ui_->connect(mediaManager_);

    // Load a.jpeg and b.jpeg once
    std::string target_path = Config::getInstance().getMediaFolderPath() + "a.jpeg";
    target_img_ = ImageLoader::loadImageFromFile(target_path);
    if (!target_img_)
    {
        common::log_error("Failed to load image at initialization");
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
        if (update())
        {
            render();
            // break; // For testing purposes, remove this line in production
        }
    }

    cameraManager_->shutdown();

    common::log_info("Main loop ended");
}

bool Application::update()
{
    // Poll events
    window_.pollEvents();

    std::unique_ptr<Image> image;
    if (!cameraManager_->updateInput(image))
    {
        return false;
    }
    image->info.filename = std::string("WebcamStream-") + std::to_string(image->info.width) + "x"
                           + std::to_string(image->info.height) + ".jpg";

    process(image);

    // Update or create the base layer after processing
    Layer* baseLayer = layerManager_->getBaseLayer();
    if (baseLayer && baseLayer->type == LayerType::Image)
    {
        // Update the image in the base layer
        baseLayer->img = std::move(image);
    }
    else
    {
        // Create the base layer if it doesn't exist
        Layer newBaseLayer;
        newBaseLayer.id = Layer::next_id++;
        newBaseLayer.type = LayerType::Image;
        newBaseLayer.name = "base";
        newBaseLayer.isBaseLayer = true;
        newBaseLayer.selected = true;
        newBaseLayer.img = std::move(image);
        newBaseLayer.dirty = true;
        if (newBaseLayer.img)
        {
            newBaseLayer.img->info.layer = 0;
        }
        layerManager_->addLayer(newBaseLayer);
    }
    // Output the current base image if available
    if (baseLayer && baseLayer->img)
    {
        // Make a deep copy for output as unique_ptr
        std::unique_ptr<Image> tempImage = baseLayer->img->deepCopy();

        auto& layers = layerManager_->getLayers();
        for (size_t i = 0; i < layers.size(); ++i)
        {
            Layer& layer = layers[i];
            if (layer.isBaseLayer)
            {
                // Skip the base layer itself
                continue;
            }
            if (layer.type == LayerType::Image && layer.img)
            {
                // Paste each layer image onto the base image at the layer's position
                tempImage->pasteAt(*layer.img, static_cast<long>(layer.x), static_cast<long>(layer.y), false);
            }
            else if (layer.type == LayerType::Gif && layer.gif && !layer.gif->frames().empty())
            {
                // Paste the current GIF frame onto the base image at the layer's position
                auto& frame = layer.gif->frames()[layer.gifFrameIndex % layer.gif->frames().size()];
                tempImage->pasteAt(*frame, static_cast<long>(layer.x), static_cast<long>(layer.y), false);
            }
        }
        if (!cameraManager_->updateOutput(tempImage))
        {
            common::log_error("Failed to update output cameras");
        }
    }
    ui_->handleKeyboard();

    // Start new UI frame
    ui_->newFrame();

    // Paint UI elements
    ui_->paint();
    return true;
}

void Application::render()
{
    // Set viewport
    window_.setViewport();

    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render all layers
    int width, height;
    window_.getFramebufferSize(width, height);
    imageRender_->renderLayers(layerManager_->getLayers(), width, height);

    // Render UI
    ui_->render();

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
        // if (scrfdDetector_->isReady())
        // {
        //     scrfd_faces = scrfdDetector_->detect(image);
        // }
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
        // // use matting layer with foreground layer
        // if (testImg_ && !image->isCompatible(*testImg_))
        // {
        //     testImg_ = testImg_->scale(image->info.width, image->info.height);
        //     common::log_info("Scaling test image to %dx%d", image->info.width, image->info.height);
        // }
        // foreground->changeBackgroundImage(*matting, *testImg_);
        // foreground->info.x = 0;
        // foreground->info.y = image->info.height;
        // image->paste(*foreground, true);

        Profiler::getInstance().stop("RVM", "App paste");
    }

    // Paint results:
    for (const auto& face : dlib_faces)
    {
        face.paintBoundingBox(image, Pixel(255, 0, 0));
    }

    bool swap_success = false;
    if (swapPipeline_ && target_img_)
    {
        swap_success = swapPipeline_->run(image, target_img_);
        image->pasteAt(*image, image->info.width, 0, true);
        if (swap_success && layerManager_)
        {
            auto layer = layerManager_->getBaseLayer();
            if (layer)
            {
                layer->dirty = true;
            }
        }
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
}

void Application::shutdown()
{
    // UI and Window destructors will handle cleanup automatically
}

