#include "LinuxFace/application.h"

#include <algorithm>
#include <climits>
#include <csignal>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/Image/text_renderer.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/common.h"
#include "LinuxFace/depthImage.h"
#include "LinuxFace/dlibDetectors.h"
#include "LinuxFace/inputWebcam.h"
#include "LinuxFace/onnx/swapPipeline.h"
#include "config.hpp"

using linuxface::Application;
using linuxface::Config;
using linuxface::LayerManager;
using linuxface::Profiler;
using linuxface::UI;


// TODO(arroyoa): test models:
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

std::atomic<bool> gShouldExit{false};

linuxface::math_utils::Point<double>
alignedToOriginalCoords(double xAligned, double yAligned, double cropLeft, double cropTop, double minX, double minY,
                        double angleRad, const linuxface::math_utils::Point<double>& eyeCenter);

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        linuxface::common::logWarn("Received signal %d, exiting...", signal);
        gShouldExit = true;
    }
}

Application::Application()
    : ui_(nullptr)
    , profiler_(Profiler::getInstance())
    , faceDetector_(nullptr)
    , dlibShapeDetector_(nullptr)
    , fsanetDetectorVar_(nullptr)
    , fsanetDetectorConv_(nullptr)
    , modnetDetector_(nullptr)
    , rvmDetector_(nullptr)
    , swapPipeline_(nullptr)
    , adria_img_(nullptr)
    , target_img_(nullptr)
{
}

// Helper to connect window resize to layerManager
void Application::connectWindowResize()
{
    if (!layerManager_)
    {
        return;
    }
    window_.setResizeCallback(
        [this](int /*width*/, int /*height*/)
        {
            if (layerManager_)
            {
                layerManager_->invalidateTextures();
            }
        });
}

Application::~Application()
{
    shutdown();
}

bool Application::initialize()
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    Profiler::getInstance().start("Initialization", "Window Setup");
    // Initialize window
    if (!window_.initialize())
    {
        linuxface::common::logError("Failed to initialize window");
        return false;
    }

    layerManager_ = std::make_shared<LayerManager>();

    // Connect window resize to layerManager texture invalidation
    this->connectWindowResize();
    Profiler::getInstance().stop("Initialization", "Window Setup");

    Profiler::getInstance().start("Initialization", "UI Setup");
    // Initialize UI with LayerManager
    ui_ = std::make_unique<UI>(layerManager_);
    if (!ui_->initialize(window_.getGLFWWindow(), window_.getGLSLVersion()))
    {
        linuxface::common::logError("Failed to initialize UI");
        return false;
    }

    // Display loading screen
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ui_->loadingScreen();
    window_.swapBuffers();
    window_.pollEvents();
    Profiler::getInstance().stop("Initialization", "UI Setup");

    Profiler::getInstance().start("Initialization", "Camera Setup");
    cameraManager_ = std::make_shared<CameraManager>();
    cameraManager_->setLayerManager(layerManager_);

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
            linuxface::common::logError("Failed to setup webcam: %s", wc.name.c_str());
            continue;
        }

        if (!webcam->start())
        {
            linuxface::common::logError("Failed to start webcam: %s", wc.name.c_str());
            continue;
        }
        webcam->setCurrentlySelected(true);
        if (!cameraManager_->addCamera(webcam))
        {
            linuxface::common::logError("Failed to add webcam: %s", wc.name.c_str());
            continue;
        }
    }

    auto availableDevicePaths = cameraManager_->discoverAvailableVideoDevices();
    for (const auto& devicePath : availableDevicePaths)
    {
        std::shared_ptr<InputWebcam> webcam = std::make_shared<InputWebcam>("", devicePath, 0, 0, 2);
        if (!webcam->setupDevice())
        {
            linuxface::common::logError("Failed to setup webcam: %s", devicePath.c_str());
            continue;
        }
        if (!cameraManager_->addCamera(std::move(webcam)))
        {
            linuxface::common::logError("Failed to add webcam: %s", devicePath.c_str());
            return false;
        }
    }
    Profiler::getInstance().stop("Initialization", "Camera Setup");

    // Start loading models in a background thread
    std::thread modelLoader(
        [this]()
        {
            const std::string modelsFolder = Config::getInstance().getModelFolderPath();

            // DlibShapeDetector initialization (landmarks)
            const std::string dlibShapeModel = modelsFolder + "shape_predictor_68_face_landmarks.dat";
            // dlibShapeDetector_ = std::make_unique<DlibShapeDetector>(dlib_shape_model);

            const std::string varOnnxPath = modelsFolder + "fsanet-var.onnx";
            // fsanetDetectorVar_ = std::make_unique<FsanetDetector>(var_onnx_path);

            const std::string convOnnxPath = modelsFolder + "fsanet-1x1.onnx";
            // fsanetDetectorConv_ = std::make_unique<FsanetDetector>(conv_onnx_path); // Seems like 1x1 is very bad

            const std::string scrfd10gBnkpsPath = modelsFolder + "scrfd_10g_bnkps_shape640x640.onnx";

            // 1ms time inference
            const std::string scrfd500mBnkpsPath = modelsFolder + "scrfd_500m_bnkps_shape640x640.onnx";
            scrfdDetector_ = std::make_shared<SCRFDetector>(scrfd500mBnkpsPath);

            const std::string modnetOnnxPath = modelsFolder + "modnet.onnx";
            // modnetDetector_ = std::make_unique<MODNetDetector>(modnet_onnx_path);

            const std::string rvmModel = modelsFolder + "rvm_mobilenetv3_fp32.onnx";
            // rvmDetector_ = std::make_unique<RobustVideoMatting>(rvmModel);

            // ArcFace recognizer initialization
            const std::string arcfaceModel = modelsFolder + "arcface_w600k_r50.onnx";
            // const std::string arcfaceModel = modelsFolder + "ms1mv3_arcface_r100.onnx";
            arcfaceRecognizer_ = std::make_shared<ArcfaceRecognizer>(arcfaceModel);

            // InSwapper initialization
            // const std::string inswapperModel = modelsFolder + "inswapper_128.onnx";
            const std::string inswapperModel = modelsFolder + "inswapper_128_fp16.onnx";
            inswapper_ = std::make_shared<InSwapper>(inswapperModel);

            // MediaPipe Face Landmarks initialization
            const std::string mediapipeLandmarksModel = modelsFolder + "facemeshv2_fast.onnx";
            mediaPipeLandmarks_ = std::make_shared<MediaPipeFaceLandmarks>(mediapipeLandmarksModel);

            const std::string mediapipeLandmarksModelOld = modelsFolder + "MediaPipeFaceLandmarkDetector.onnx";
            mediaPipeLandmarksOld_ = std::make_shared<MediaPipeFaceLandmarks>(mediapipeLandmarksModelOld);

            // Initialize SwapPipeline after all models are loaded
            swapPipeline_ = std::make_unique<SwapPipeline>(inswapper_, arcfaceRecognizer_, scrfdDetector_);
        });
    modelLoader.detach();

    Profiler::getInstance().start("Initialization", "Renderer Setup");
    // Pass pointer instead of reference
    ui_->connect(cameraManager_);

    linuxface::common::logInfo("OpenGL version: %s", glGetString(GL_VERSION));

    // Initialize image renderer
    imageRender_ = std::make_shared<ImageRenderGL>();
    if (!imageRender_ || !imageRender_->initialize())
    {
        linuxface::common::logError("Failed to initialize image renderer");
        return false;
    }

    mediaManager_ = std::make_shared<MediaManager>(imageRender_);
    ui_->connect(mediaManager_);
    Profiler::getInstance().stop("Initialization", "Renderer Setup");

    Profiler::getInstance().start("Initialization", "Target Image Loading");
    // Load target faceswap image once
    const std::string targetPath = "/home/arroyo/Documents/Projectes/LinuxCam/tests/common/single_face.jpeg";
    target_img_ = ImageLoader::loadImageFromFile(targetPath);
    if (!target_img_)
    {
        linuxface::common::logError("Failed to load image at initialization");
    }
    Profiler::getInstance().stop("Initialization", "Target Image Loading");


    // auto image =
    // ImageLoader::loadImageFromFile("/home/arroyo/Documents/Projectes/LinuxCam/experimental/pfld_106_face_landmarks/2.jpg");
    // if (!image)
    // {
    //     linuxface::common::logError("Failed to load image at initialization");
    // }

    // std::vector<Face> scrfdFaces;
    // if (scrfdDetector_ != nullptr && scrfdDetector_->isReady())
    // {
    //     scrfdFaces = scrfdDetector_->detect(image);
    //     int i = 0;
    //     for (const auto& face : scrfdFaces)
    //     {
    //         // face.paintBoundingBox(image, Pixel(200, 200, 200));
    //         // face.paintAllFaceLandmarks(image, false, Pixel(0, 200, 200), 1.5f);
    //         auto crop = image->crop(face.getBoundingBox().rect);
    //         crop->saveToDisk("Crop_" + std::to_string(i++) + ".ppm");
    //     }
    // }

    // if (pfldDetector_ != nullptr && pfldDetector_->isReady() && scrfdFaces.size() >= 1)
    // {
    //     auto input_face = scrfdFaces[0];
    //     pfldDetector_->detect(image, input_face);
    //     input_face.paintAllFaceLandmarks(image, false, Pixel(200, 0, 200), 1.0f);
    //     image->saveToDisk("result_linuxface.ppm");
    // }else{
    //     linuxface::common::logError("PFLD not ready or no faces detected");
    // }

    linuxface::common::logInfo("Application initialized successfully");
    return true;
}

void Application::run()
{
    linuxface::common::logInfo("Starting main loop...");

    // Main loop
    while (!window_.shouldClose() && !gShouldExit)
    {
        // handle periodic cleanup automatically
        profiler_.update();

        Profiler::getInstance().start("MainLoop", "Frame Processing");
        if (update())
        {
            render();
        }
        Profiler::getInstance().stop("MainLoop", "Frame Processing");
    }

    cameraManager_->shutdown();
    mediaManager_->shutdown();

    linuxface::common::logInfo("Main loop ended");

    // Print profiling summary on exit
    profiler_.printSummary();
}

bool Application::update()
{
    Profiler::getInstance().start("Application", "Poll Events");
    // Poll events
    window_.pollEvents();
    Profiler::getInstance().stop("Application", "Poll Events");

    Profiler::getInstance().start("Application", "Update Camera Input");
    // Update camera inputs - this now creates/updates individual layers per camera
    if (!cameraManager_->updateInput())
    {
        return false;
    }
    Profiler::getInstance().stop("Application", "Update Camera Input");

    // Composite all layers for output (if we have output cameras)
    auto& layers = layerManager_->getLayers();
    if (layers.empty())
    {
        return false;
    }

    // Get window/viewport size for composite
    int windowWidth = 0;
    int windowHeight = 0;
    window_.getFramebufferSize(windowWidth, windowHeight);

    if (windowWidth <= 0 || windowHeight <= 0)
    {
        common::logInfo("Window FrameBuffer size is invalid. W,H - %d %d", windowWidth, windowHeight);
        return false;
    }

    Profiler::getInstance().start("Application", "Calculate Composite Bounds");
    float minX, minY, maxX, maxY;
    calculateCompositeBounds(layers, windowWidth, windowHeight, minX, minY, maxX, maxY);
    unsigned int compositeWidth = (minX < maxX) ? static_cast<unsigned int>(maxX - minX) : windowWidth;
    unsigned int compositeHeight = (minY < maxY) ? static_cast<unsigned int>(maxY - minY) : windowHeight;
    compositeWidth = std::min(compositeWidth, static_cast<unsigned int>(windowWidth));
    compositeHeight = std::min(compositeHeight, static_cast<unsigned int>(windowHeight));
    Profiler::getInstance().stop("Application", "Calculate Composite Bounds");

    Profiler::getInstance().start("Application", "Create Composite Image");
    std::unique_ptr<Image> compositeImage;
    bool compositeValid = createCompositeImage(compositeImage, layers, minX, minY, compositeWidth, compositeHeight);

    if (!compositeValid)
    {
        common::logInfo("Composite image is not valid.");
        return false;
    }
    Profiler::getInstance().stop("Application", "Create Composite Image");

    Profiler::getInstance().start("Application", "Process Image");
    process(compositeImage);
    Profiler::getInstance().stop("Application", "Process Image");

    // Flip horizontally the composite image
    // compositeImage->flipHorizontalInPlace();

    Profiler::getInstance().start("Application", "Update Camera Output");
    // Send processed composite to output cameras with cropping
    if (!cameraManager_->updateOutput(compositeImage))
    {
        linuxface::common::logError("Failed to update output cameras");
    }
    Profiler::getInstance().stop("Application", "Update Camera Output");

    Profiler::getInstance().start("Application", "UI Update");
    ui_->handleKeyboard();
    ui_->newFrame();
    ui_->paint();
    Profiler::getInstance().stop("Application", "UI Update");
    return true;
}

void Application::render()
{
    Profiler::getInstance().start("Application", "Set Viewport");
    // Set viewport
    window_.setViewport();
    Profiler::getInstance().stop("Application", "Set Viewport");

    Profiler::getInstance().start("Application", "Clear Screen");
    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Profiler::getInstance().stop("Application", "Clear Screen");

    Profiler::getInstance().start("Application", "Render Layers");
    // Render all layers
    int width = 0;
    int height = 0;
    window_.getFramebufferSize(width, height);
    imageRender_->renderLayers(layerManager_->getLayers(), width, height);
    Profiler::getInstance().stop("Application", "Render Layers");

    Profiler::getInstance().start("Application", "Render UI");
    // Render UI
    ui_->render();
    Profiler::getInstance().stop("Application", "Render UI");

    Profiler::getInstance().start("Application", "Swap Buffers");
    // Swap buffers
    window_.swapBuffers();
    Profiler::getInstance().stop("Application", "Swap Buffers");
}

void Application::process(std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("Process", "Deep Copy Raw Image");
    auto raw = image->deepCopy();
    Profiler::getInstance().stop("Process", "Deep Copy Raw Image");

    std::vector<Face> dlibFaces;
    if (faceDetector_ != nullptr)
    {
        // dlib_faces = faceDetector_->detect(image);
    }

    std::vector<Face> scrfdFaces;
    if (scrfdDetector_ != nullptr && scrfdDetector_->isReady())
    {
        Profiler::getInstance().start("Process", "SCRFD Face Detection");
        scrfdFaces = scrfdDetector_->detect(image);
        Profiler::getInstance().stop("Process", "SCRFD Face Detection");

        Profiler::getInstance().start("Process", "Paint Face Bounding Boxes");
        for (const auto& face : scrfdFaces)
        {
            face.paintBoundingBox(image, Pixel(0, 200, 200));
            face.paintAllFaceLandmarks(image, false, Pixel(0, 200, 200), 1.5f);
        }
        Profiler::getInstance().stop("Process", "Paint Face Bounding Boxes");
    }


    // if (pfldDetector_ != nullptr && pfldDetector_->isReady() && scrfdFaces.size() >= 1)
    // {
    //     auto input_face = scrfdFaces[0];
    //     pfldDetector_->detect(image, input_face);
    //     input_face.paintAllFaceLandmarks(image, false, Pixel(200, 0, 200), 2.0f);
    // }
    // else
    // {
    //     linuxface::common::logError("PFLD not ready or no faces detected");
    // }


    // if (modnetDetector_ && modnetDetector_->isReady())
    // {
    //     std::unique_ptr<Image> matting = image->deepCopy();
    //     modnetDetector_->detect(image, matting);
    //     matting->info.x = image->info.width;
    //     matting->info.y = 0;
    //     image->paste(*matting, true);
    // }

    // Dlib landmark detection using dlib face
    // if (dlibShapeDetector_ && !dlib_faces.empty())
    // {
    //     std::vector<math_utils::Rect<float>> rects;
    //     rects.push_back(dlib_faces[0].getBoundingBox().rect);
    //     auto dlib_landmark_faces = dlibShapeDetector_->detect(image, rects);
    //     for (const auto& face : dlib_landmark_faces)
    //     {
    //         face.paintBoundingBox(image, Pixel(255, 0, 0));
    //         face.paintAllFaceLandmarks(image, false, Pixel(255, 0, 0), 1.5f);
    //     }
    // if (!scrfdFaces.empty())
    // {
    //     if (mediaPipeLandmarks_ && mediaPipeLandmarks_->isReady())
    //     {
    //         auto resultFace = mediaPipeLandmarks_->detect(raw, scrfdFaces[0]);
    //         if (resultFace.isValid())
    //         {
    //             resultFace.paintAllFaceLandmarks(image, false, Pixel(0, 255, 0), 1.0f);
    //         }
    //     }
    // }

    // Delegate MediaPipe face landmark processing to a helper allowing model and color parameterization
    if (!scrfdFaces.empty())
    {

        Profiler::getInstance().start("Process", "MediaPipe Landmarks (Old)");
        processMediaPipeLandmarks(mediaPipeLandmarksOld_, raw, image, scrfdFaces, Pixel(200, 0, 155));
        Profiler::getInstance().stop("Process", "MediaPipe Landmarks (Old)");
    }

    //     // 1. Draw five-point landmarks used for alignment on the original image
    //     auto five_pts_2d = scrfd_faces[0].getFivePointLandmarksArcFaceOrder2D();
    //     // for (size_t i = 0; i < five_pts.size(); ++i)
    //     // {
    //     //     image->ppx(five_pts[i].x, five_pts[i].y, Pixel(0, 255, 255));
    //     //     linuxface::common::logInfo("Five-point landmark %zu: (%.1f, %.1f)", i, (float) five_pts[i].x,
    //     //                      (float) five_pts[i].y);
    //     // }

    //     // 2. Affine align
    //     // auto [aligned_image, affine] =
    //     //     image_utils::similarity_face_transform(*raw, five_pts_2d, image_utils::template_192_alt, 192, true);
    //     auto [aligned_image, affine] = image_utils::similarity_face_transform(
    //         *raw, five_pts_2d, image_utils::template_192_alt, 192, true);

    //     if (!aligned_image)
    //     {
    //         linuxface::common::logError("Failed to wrap face image for MediaPipe landmarks detection");
    //         return;
    //     }
    //     auto test = aligned_image->deepCopy();
    //     test->drawBorder(Pixel(255, 100, 50), 2);
    //     image->pasteAt(*test, image->info.width, 100, true);

    //     auto result = mediaPipeLandmarks_->detect(aligned_image);

    //     if (result.score > 0.5)
    //     {
    //         // 4. Map landmarks back to original image
    //         double invM[6];
    //         if (!math_utils::invert_affine(affine.data(), invM))
    //         {
    //             linuxface::common::logError("Failed to invert affine for MediaPipe unalignment");
    //             return;
    //         }

    //         double w = aligned_image->info.width;
    //         double h = aligned_image->info.height;
    //         std::vector<std::pair<double, double>> aligned_pts;
    //         std::vector<float> aligned_z;
    //         for (const auto& landmark : result.landmarks)
    //         {
    //             aligned_pts.emplace_back(landmark[0] * w, landmark[1] * h);
    //             aligned_z.push_back(landmark[2]);
    //         }
    //         auto unaligned_pts = image_utils::transform_points_affine(aligned_pts, invM);
    //         for (size_t i = 0; i < unaligned_pts.size(); ++i)
    //         {
    //             double x = static_cast<double>(unaligned_pts[i].first);
    //             double y = static_cast<double>(unaligned_pts[i].second);
    //             float z = aligned_z[i];
    //             if (x < 0 || x >= image->info.width || y < 0 || y >= image->info.height)
    //             {
    //                 linuxface::common::logWarn("MediaPipe landmark out of bounds: (%f, %f, %f)", x, y, z);
    //                 continue;
    //             }
    //             image->ppx(x, y, Pixel(0, 0, 255));
    //             image_utils::paintCircle(image, math_utils::Point3D(x, y, z), 1.5f, Pixel(0, 0, 255));
    //         }
    //         auto layer = layerManager_->getBaseLayer();
    //         if (layer)
    //         {
    //             layer->dirty = true;
    //         }
    //     }
    //     else
    //     {
    //         linuxface::common::logWarn("MediaPipe landmarks detection score too low: %f", result.score);
    //     }
    // }

    // if (mediaPipeLandmarks_ && mediaPipeLandmarks_->isReady() && !scrfd_faces.empty())
    // {
    //     auto face = scrfd_faces[0];

    //     auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
    //     auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);

    //     math_utils::Point<double> eye_center = {(left_eye.x + right_eye.x) / 2.0, (left_eye.y + right_eye.y) / 2.0};
    //     double bbox_scale_factor = 1.5; // Scale factor for bounding box size
    //     double dx = right_eye.x - left_eye.x;
    //     double dy = right_eye.y - left_eye.y;
    //     double angleRad = -std::atan2(dy, dx); // rotate to horizontal
    //     double eye_dist = std::sqrt(dx * dx + dy * dy);

    //     // Use bounding box from SCRFD
    //     auto bbox = face.getBoundingBox().rect;

    //     // Store original image dimensions
    //     unsigned long orig_width = raw->info.width;
    //     unsigned long orig_height = raw->info.height;

    //     // Compute center of the bounding box in original coordinates
    //     math_utils::Point<double> face_center_original = {bbox.l + bbox.width() / 2.0, bbox.t + bbox.height() / 2.0};

    //     auto aligned_face = raw->deepCopy();

    //     // Rotate the whole image
    //     auto translation_offset = aligned_face->rotate(angleRad, eye_center);

    //     // Calculate where the eye center should be in the rotated image
    //     // We need to simulate the same transformation that the rotation function does
    //     double cosA = std::cos(angleRad);
    //     double sinA = std::sin(angleRad);

    //     // Calculate the corners of the original image relative to the eye center
    //     double corners[4][2] = {
    //         {-eye_center.x,                 -eye_center.y                 }, // top-left
    //         {orig_width - 1 - eye_center.x, -eye_center.y                 }, // top-right
    //         {-eye_center.x,                 orig_height - 1 - eye_center.y}, // bottom-left
    //         {orig_width - 1 - eye_center.x, orig_height - 1 - eye_center.y}  // bottom-right
    //     };

    //     // Find the bounding box of rotated corners
    //     double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    //     for (int i = 0; i < 4; ++i)
    //     {
    //         double x = corners[i][0] * cosA - corners[i][1] * sinA;
    //         double y = corners[i][0] * sinA + corners[i][1] * cosA;
    //         minX = std::min(minX, x);
    //         minY = std::min(minY, y);
    //         maxX = std::max(maxX, x);
    //         maxY = std::max(maxY, y);
    //     }


    //     // Now calculate where the face center should be in the rotated image
    //     // First, get face center relative to eye center in original image
    //     double face_dx = face_center_original.x - eye_center.x;
    //     double face_dy = face_center_original.y - eye_center.y;

    //     // Rotate this relative vector
    //     math_utils::Point<double> face_center_rotated_relative = {cosA * face_dx - sinA * face_dy,
    //                                                               sinA * face_dx + cosA * face_dy};

    //     // Add to the rotated eye center position
    //     math_utils::Point<double> final_face_center = {face_center_rotated_relative.x - minX,
    //                                                    face_center_rotated_relative.y - minY};

    //     // Calculate box size based on eye distance for more robust sizing
    //     double base_box_size = eye_dist * 3.0; // Base size relative to eye distance
    //     double bbox_box_size = std::max(bbox.width(), bbox.height()) * bbox_scale_factor;
    //     double box_size = bbox_box_size;

    //     // Ensure the box fits within the rotated image bounds
    //     double max_box_size = std::min(aligned_face->info.width, aligned_face->info.height) * 0.9;
    //     box_size = std::min(box_size, max_box_size);

    //     // Calculate crop rectangle bounds
    //     double half_box = box_size / 2.0;
    //     double crop_left = final_face_center.x - half_box;
    //     double crop_top = final_face_center.y - half_box;
    //     double crop_right = final_face_center.x + half_box;
    //     double crop_bottom = final_face_center.y + half_box;

    //     // Create crop rectangle
    //     math_utils::Point<float> left_corner{static_cast<float>(crop_left), static_cast<float>(crop_top)};

    //     math_utils::Rect<float> crop_rect = {left_corner, static_cast<float>(box_size),
    //     static_cast<float>(box_size)};

    //     aligned_face = aligned_face->crop(crop_rect);
    //     if (!aligned_face)
    //     {
    //         linuxface::common::logError("Failed to crop aligned face image for MediaPipe landmarks detection");
    //         return;
    //     }

    //     auto height = image->info.height;
    //     image->pasteAt(*aligned_face, 0, height, true);

    //     auto result = mediaPipeLandmarks_->detect(aligned_face);
    //     if (result.score > 0.5)
    //     {
    //         for (size_t i = 0; i < result.landmarks.size(); ++i)
    //         {
    //             double x_aligned = result.landmarks[i][0] * aligned_face->info.width;
    //             double y_aligned = result.landmarks[i][1] * aligned_face->info.height;

    //             auto pt = AlignedToOriginalCoords(x_aligned, y_aligned, crop_left, crop_top, minX, minY, angleRad,
    //                                               eye_center);

    //             if (pt.x >= 0 && pt.x < raw->info.width && pt.y >= 0 && pt.y < raw->info.height)
    //             {
    //                 image->ppx(pt.x, pt.y, Pixel(0, 255, 0));
    //                 image_utils::paintCircle(image, math_utils::Point3D(pt.x, pt.y, 0), 1.0f, Pixel(0, 255, 0));
    //             }
    //         }
    //     }
    //     else
    //     {
    //         linuxface::common::logWarn("MediaPipe landmarks detection score too low: %f", result.score);
    //     }
    // }


    // bool swap_success = false;
    // if (swapPipeline_ && target_img_)
    // {
    //     swap_success = swapPipeline_->run(image, target_img_, scrfdFaces);
    // }

    // if (rvmDetector_ && rvmDetector_->isReady())
    // {
    //     // If resize occurs, rvm is not able to handle that, we just reset it.
    //     if (!rvmDetector_->isImageCompatible(image))
    //     {
    //         rvmDetector_.reset();
    //         rvmDetector_ = std::make_unique<RobustVideoMatting>(Config::getInstance().getModelFolderPath()
    //                                                             + "rvm_mobilenetv3_fp32.onnx");
    //     }

    //     Profiler::getInstance().start("RVM", "App deep copy");
    //     std::unique_ptr<Image> matting = image->deepCopy();
    //     // std::unique_ptr<Image> foreground = image->deepCopy();
    //     Profiler::getInstance().stop("RVM", "App deep copy");

    //     rvmDetector_->detect(image, image, matting);
    //     Profiler::getInstance().start("RVM", "App paste");

    //     // // use matting layer with foreground layer
    //     if (fake_background_ && !image->isCompatible(*fake_background_))
    //     {
    //         fake_background_ = fake_background_->scale(image->info.width, image->info.height);
    //         linuxface::common::logInfo("Scaling test image to %dx%d", image->info.width, image->info.height);
    //     }
    //     image->changeBackgroundImage(*matting, *fake_background_);
    //     // foreground->info.x = 0;
    //     // foreground->info.y = image->info.height;
    //     // image->paste(*foreground, true);

    //     Profiler::getInstance().stop("RVM", "App paste");
    // }
    // for (auto& face : scrfd_faces)
    // {
    //     if (fsanetDetectorVar_ && fsanetDetectorVar_->isReady())
    //     {
    //         fsanetDetectorVar_->detect(image, face);
    //         face.paintPoseAxis(image, 60, 3);
    //     }
    //     if (fsanetDetectorConv_ && fsanetDetectorConv_->isReady())
    //     {
    //         fsanetDetectorConv_->detect(image, face);
    //         face.paintPoseAxis(image, 30, 3, true);
    //     }
    //     face.paintBoundingBox(image, Pixel(0, 255, 0));
    // }
}

void Application::shutdown()
{
    // UI and Window destructors will handle cleanup automatically
}

linuxface::math_utils::Point<double>
alignedToOriginalCoords(double xAligned, double yAligned, double cropLeft, double cropTop, double minX, double minY,
                        double angleRad, const linuxface::math_utils::Point<double>& eyeCenter)
{
    // Step 1: undo crop
    const double xRotated = xAligned + cropLeft;
    const double yRotated = yAligned + cropTop;

    // Step 2: get absolute rotated coordinates
    const double xRel = xRotated + minX;
    const double yRel = yRotated + minY;

    // Step 3: un-rotate around eye center
    const double cosA = std::cos(-angleRad);
    const double sinA = std::sin(-angleRad);

    const double xOrig = cosA * xRel - sinA * yRel + eyeCenter.x;
    const double yOrig = sinA * xRel + cosA * yRel + eyeCenter.y;

    return {xOrig, yOrig};
}

void Application::calculateCompositeBounds(const std::vector<Layer>& layers, int windowWidth, int windowHeight,
                                           float& minX, float& minY, float& maxX, float& maxY)
{
    minX = std::numeric_limits<float>::max();
    minY = std::numeric_limits<float>::max();
    maxX = std::numeric_limits<float>::min();
    maxY = std::numeric_limits<float>::min();

    for (const auto& layer : layers)
    {
        // Skip preview output layers (identified by name since they have empty cameraDevicePath)
        if (layer.name == "Output Preview")
        {
            continue;
        }

        float layerMinX = layer.x;
        float layerMinY = layer.y;
        float layerMaxX = layer.x;
        float layerMaxY = layer.y;

        if (layer.type == LayerType::IMAGE && layer.img)
        {
            layerMaxX += static_cast<float>(layer.img->info.width);
            layerMaxY += static_cast<float>(layer.img->info.height);
        }
        else if (layer.type == LayerType::GIF && layer.gif && !layer.gif->frames().empty())
        {
            auto& frame = layer.gif->frames()[0]; // Use first frame for bounds calculation
            layerMaxX += static_cast<float>(frame->info.width);
            layerMaxY += static_cast<float>(frame->info.height);
        }
        else if (layer.type == LayerType::VIDEO && layer.video)
        {
            layerMaxX += static_cast<float>(layer.video->getMetadata().width);
            layerMaxY += static_cast<float>(layer.video->getMetadata().height);
        }

        minX = std::min(minX, layerMinX);
        minY = std::min(minY, layerMinY);
        maxX = std::max(maxX, layerMaxX);
        maxY = std::max(maxY, layerMaxY);
    }

    // If no valid layers, use window size
    if (minX == std::numeric_limits<float>::max())
    {
        minX = 0.0f;
        minY = 0.0f;
        maxX = static_cast<float>(windowWidth);
        maxY = static_cast<float>(windowHeight);
    }
}

bool Application::createCompositeImage(std::unique_ptr<Image>& compositeImage, const std::vector<Layer>& layers,
                                       float minX, float minY, unsigned int compositeWidth,
                                       unsigned int compositeHeight)
{
    bool compositeValid{false};
    const Pixel transparentPixel{0, 0, 0, 0};
    compositeImage = std::make_unique<Image>(transparentPixel, compositeWidth, compositeHeight);

    // Composite all layers onto the canvas
    for (const auto& layer : layers)
    {
        // Skip preview output layers (identified by name since they have empty cameraDevicePath)
        if (layer.name == "Output Preview")
        {
            continue;
        }

        if (layer.type == LayerType::IMAGE && layer.img)
        {
            compositeImage->pasteAt(*layer.img, static_cast<long>(layer.x - minX), static_cast<long>(layer.y - minY),
                                    false);
            compositeValid = true;
        }
        else if (layer.type == LayerType::GIF && layer.gif && !layer.gif->frames().empty())
        {
            auto& frame = layer.gif->frames()[layer.gifFrameIndex % layer.gif->frames().size()];
            compositeImage->pasteAt(*frame, static_cast<long>(layer.x - minX), static_cast<long>(layer.y - minY),
                                    false);
            compositeValid = true;
        }
        else if (layer.type == LayerType::VIDEO && layer.video && layer.currentVideoFrame)
        {
            compositeImage->pasteAt(*layer.currentVideoFrame, static_cast<long>(layer.x - minX),
                                    static_cast<long>(layer.y - minY), false);
            compositeValid = true;
        }
    }
    return compositeValid;
}

bool Application::processMediaPipeLandmarks(std::shared_ptr<MediaPipeFaceLandmarks> landmarks,
                                            std::unique_ptr<Image>& raw, std::unique_ptr<Image>& image,
                                            const std::vector<Face>& scrfdFaces, const Pixel& color)
{
    if (!landmarks || !landmarks->isReady() || scrfdFaces.empty())
    {
        return false;
    }

    // 1. Draw five-point landmarks used for alignment on the original image
    auto five_pts_2d = scrfdFaces[0].getFivePointLandmarksArcFaceOrder2D();

    // 2. Get MediaPipe model's expected input dimensions
    int modelWidth = landmarks->getInputWidth();

    // 3. Choose the appropriate template based on model dimensions
    auto templatePoints = image_utils::TEMPLATE_192_ALT;
    if (modelWidth == 256)
    {
        templatePoints = image_utils::TEMPLATE_256;
    }
    else if (modelWidth != 192)
    {
        // For other sizes, scale the 192 template
        double localTemplate[5][2];
        double scaleFactor = static_cast<double>(modelWidth) / 192.0;
        for (int i = 0; i < 5; ++i)
        {
            localTemplate[i][0] = image_utils::TEMPLATE_192_ALT[i][0] * scaleFactor;
            localTemplate[i][1] = image_utils::TEMPLATE_192_ALT[i][1] * scaleFactor;
        }
        templatePoints = localTemplate;
        common::logWarn("Using scaled template for model size %d, consider adding a dedicated template", modelWidth);
    }

    // 4. Affine align using model's expected dimensions
    auto [aligned_image, affine] =
        image_utils::similarityFaceTransform(*raw, five_pts_2d, templatePoints, modelWidth, true);

    if (!aligned_image)
    {
        linuxface::common::logError("Failed to wrap face image for MediaPipe landmarks detection");
        return false;
    }
    if(modelWidth == 192)
    {
        aligned_image->saveToDisk("aligned_192.ppm");
    }else if(modelWidth == 256)
    {
        aligned_image->saveToDisk("aligned_256.ppm");
    }
    auto result = landmarks->detectAligned(aligned_image);

    if (result.score <= 0.5)
    {
        linuxface::common::logWarn("MediaPipe landmarks detection score too low: %f", result.score);
        return false;
    }

    // 5. Map landmarks back to original image
    double invM[6];
    if (!math_utils::invertAffine(affine.data(), invM))
    {
        linuxface::common::logError("Failed to invert affine for MediaPipe unalignment");
        return false;
    }

    // NOTE: aligned_image width/height usage was commented out in original code; keep legacy behaviour
    double w = 1; // aligned_image->info.width;
    double h = 1; // aligned_image->info.height;
    if (modelWidth == 192)
    {
        w = aligned_image->info.width;
        h = aligned_image->info.height;
    }

    std::vector<std::pair<double, double>> aligned_pts;
    std::vector<float> aligned_z;
    for (const auto& landmark : result.landmarks)
    {
        aligned_pts.emplace_back(landmark[0] * w, landmark[1] * h);
        aligned_z.push_back(landmark[2]);
    }
    auto unaligned_pts = image_utils::transformPointsAffine(aligned_pts, invM);
    for (size_t i = 0; i < unaligned_pts.size(); ++i)
    {
        double x = static_cast<double>(unaligned_pts[i].first);
        double y = static_cast<double>(unaligned_pts[i].second);
        float z = aligned_z[i];
        if (x < 0 || x >= image->info.width || y < 0 || y >= image->info.height)
        {
            continue;
        }
        if (i == 1 && modelWidth == 256)
        {
            // Draw the landmark coordinates for index 1 as a test
            std::ostringstream ss;
            ss << "(" << static_cast<int>(std::round(x)) << "," << static_cast<int>(std::round(y)) << ","
               << static_cast<int>(std::round(z)) << ")";
            const std::string coordText = ss.str();
            // drawTextWithBackground(Image&, x, y, text, textColor, bgColor, scale, center, padding)
            linuxface::TextRenderer::drawTextWithBackground(*image, static_cast<int>(x) + 10, static_cast<int>(y) + 10,
                                                            coordText, Pixel(255, 255, 255), Pixel(0, 0, 0), 2, false,
                                                            0);
        }
        image->ppx(x, y, color);
    }

    // Mark layers dirty so textures get updated
    if (layerManager_)
    {
        layerManager_->markDirty();
    }
    return true;
}
