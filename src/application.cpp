#include "LinuxFace/application.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/common.h"
#include "LinuxFace/depthImage.h"
#include "LinuxFace/dlibDetectors.h"
#include "LinuxFace/inputWebcam.h"
#include "LinuxFace/onnx/swapPipeline.h"
#include "LinuxFace/wflw_test.h"
#include "config.hpp"

#include <csignal>
#include <iostream>
#include <memory>

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

std::atomic<bool> g_should_exit{false};


linuxface::math_utils::Point<double>
AlignedToOriginalCoords(double x_aligned, double y_aligned, double crop_left, double crop_top, double min_x, double min_y,
                        double angle_rad, const linuxface::math_utils::Point<double>& eye_center);

void SignalHandler(int signal)
{
    if (signal == SIGINT)
    {
        linuxface::common::log_warn("Received SIGINT, exiting...");
        g_should_exit = true;
    }
}


Application::Application() : profiler_(Profiler::getInstance()), ui_(nullptr), faceDetector_(nullptr), dlibShapeDetector_(nullptr), fsanetDetectorVar_(nullptr), fsanetDetectorConv_(nullptr), modnetDetector_(nullptr), rvmDetector_(nullptr), swapPipeline_(nullptr), adria_img_(nullptr), target_img_(nullptr)
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
    std::signal(SIGINT, SignalHandler);

    // Initialize window
    if (!window_.initialize())
    {
        linuxface::common::log_error("Failed to initialize window");
        return false;
    }

    layerManager_ = std::make_shared<LayerManager>();

    // Connect window resize to layerManager texture invalidation
    connectWindowResize();

    // Initialize UI with LayerManager
    ui_ = std::make_unique<UI>(layerManager_);
    if (!ui_->initialize(window_.getGLFWWindow(), window_.getGLSLVersion()))
    {
        linuxface::common::log_error("Failed to initialize UI");
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
            linuxface::common::log_error("Failed to setup webcam: %s", wc.name.c_str());
            continue;
        }

        if (!webcam->start())
        {
            linuxface::common::log_error("Failed to start webcam: %s", wc.name.c_str());
            continue;
        }
        webcam->setCurrentlySelected(true);
        if (!cameraManager_->addCamera(std::move(webcam)))
        {
            linuxface::common::log_error("Failed to add webcam: %s", wc.name.c_str());
            continue;
        }
    }

    auto available_device_paths = cameraManager_->discoverAvailableVideoDevices();
    for (const auto& device_path : available_device_paths)
    {
        std::shared_ptr<InputWebcam> webcam = std::make_shared<InputWebcam>("", device_path, 0, 0, 2);
        if (!webcam->setupDevice())
        {
            linuxface::common::log_error("Failed to setup webcam: %s", device_path.c_str());
            continue;
        }
        if (!cameraManager_->addCamera(std::move(webcam)))
        {
            linuxface::common::log_error("Failed to add webcam: %s", device_path.c_str());
            return false;
        }
    }

    // Just here for benchmarking.
    // faceDetector_ = std::make_unique<DlibFaceDetector>();
    std::string models_folder = Config::getInstance().getModelFolderPath();

    // DlibShapeDetector initialization (landmarks)
    std::string dlib_shape_model = models_folder + "shape_predictor_68_face_landmarks.dat";
    // dlibShapeDetector_ = std::make_unique<DlibShapeDetector>(dlib_shape_model);

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
    rvmDetector_ = std::make_unique<RobustVideoMatting>(rvm_model);


    // ArcFace recognizer initialization
    std::string arcface_model = models_folder + "arcface_w600k_r50.onnx";
    arcfaceRecognizer_ = std::make_shared<ArcfaceRecognizer>(arcface_model);

    // InSwapper initialization
    std::string inswapper_model = models_folder + "inswapper_128.onnx";
    inswapper_ = std::make_shared<InSwapper>(inswapper_model);

    // MediaPipe Face Landmarks initialization
    std::string mediapipe_landmarks_model = models_folder + "MediaPipeFaceLandmarkDetector.onnx";
    // std::string mediapipe_landmarks_model = models_folder + "face_landmark_barracuda.onnx";
    // mediaPipeLandmarks_ = std::make_shared<MediaPipeFaceLandmarks>(mediapipe_landmarks_model);

    // Initialize SwapPipeline after all models are loaded
    swapPipeline_ = std::make_unique<SwapPipeline>(inswapper_, arcfaceRecognizer_, scrfdDetector_);
    // Pass pointer instead of reference
    ui_->connect(cameraManager_);

    linuxface::common::log_info("OpenGL version: %s", glGetString(GL_VERSION));

    // Initialize image renderer
    imageRender_ = std::make_shared<ImageRenderGL>();
    if (!imageRender_ || !imageRender_->initialize())
    {
        linuxface::common::log_error("Failed to initialize image renderer");
        return false;
    }

    mediaManager_ = std::make_shared<MediaManager>(imageRender_);
    ui_->connect(mediaManager_);

    // Load target faceswap image once
    std::string target_path = Config::getInstance().getMediaFolderPath() + "../testing.jpeg";
    target_img_ = ImageLoader::loadImageFromFile(target_path);
    if (!target_img_)
    {
        linuxface::common::log_error("Failed to load image at initialization");
    }

    // PFLD Landmarks initialization
    std::string pfld_model = models_folder + "pfld-106-v3.onnx";
    pfldDetector_ = std::make_shared<PFLDDetector>(pfld_model);


    const std::string wflw_base = Config::getInstance().getWFLWFolderPath();
    WFLWLoader loader(wflw_base + "/WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt", 10);

    if (loader.get_num_examples() == 0)
    {
        linuxface::common::log_error("Error: No examples loaded.");
        return false;
    }

    if (loader.load_example(0, example_))
    { // Load the first example
        linuxface::common::log_info("Loaded example from image: %s", example_.image_name.c_str());
        linuxface::common::log_info("Bounding box: (%f, %f) - (%f, %f)", example_.bounding_box.l, example_.bounding_box.t,
                         example_.bounding_box.r, example_.bounding_box.b);
        linuxface::common::log_info("Attributes: %d %d %d %d %d %d", example_.attributes[0], example_.attributes[1],
                         example_.attributes[2], example_.attributes[3], example_.attributes[4],
                         example_.attributes[5]);
        linuxface::common::log_info("Number of landmarks: %zu", example_.landmarks.size());
        // You can further process the landmarks here...
    }

    // std::unique_ptr<Image> testa = ImageLoader::loadImageFromFile(Config::getInstance().getMediaFolderPath() +
    // "image_.jpeg"); testa->saveToDisk("image.ppm"); return false; std::unique_ptr<Image> test =
    // std::make_unique<Image>(); test->copyFrom(*example_.image); process(test);
    // example_.image->saveToDisk("input.ppm");
    // test->saveToDisk("output.ppm");
    // return false;

    linuxface::common::log_info("Application initialized successfully");
    return true;
}

void Application::run()
{
    linuxface::common::log_info("Starting main loop...");

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
    mediaManager_->shutdown();

    linuxface::common::log_info("Main loop ended");
}

bool Application::update()
{
    // Poll events
    window_.pollEvents();

    std::unique_ptr<Image> image;
    auto image_ready = cameraManager_->updateInput(image);
    if (image_ready)
    {
        image->info.filename = std::string("WebcamStream-") + std::to_string(image->info.width) + "x"
                               + std::to_string(image->info.height) + ".jpg";

        process(image);

        // Update or create the base layer after processing
        Layer* baseLayer = layerManager_->getBaseLayer();
        if (baseLayer != nullptr && baseLayer->type == LayerType::Image)
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
        if (baseLayer != nullptr && baseLayer->img)
        {
            // Make a deep copy for output as unique_ptr
            std::unique_ptr<Image> tempImage = baseLayer->img->deepCopy();

            auto& layers = layerManager_->getLayers();
            for (auto& layer : layers)
            {
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
                linuxface::common::log_error("Failed to update output cameras");
            }
        }
        static bool saving = false;
        // Check for space key press to capture and save webcam image
        if (window_.isKeyPressed(GLFW_KEY_SPACE) && !saving)
        {
            saving = true;
            captureAndSaveWebcamImageWithTimestamp();
        }
        else
        {
            saving = false;
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
    int width = 0;
    int height = 0;
    window_.getFramebufferSize(width, height);
    imageRender_->renderLayers(layerManager_->getLayers(), width, height);

    // Render UI
    ui_->render();

    // Swap buffers
    window_.swapBuffers();
}

void Application::process(std::unique_ptr<Image>& image /*image*/)
{
    auto raw = image->deepCopy();

    std::vector<Face> dlib_faces;
    if (faceDetector_ != nullptr)
    {
        // dlib_faces = faceDetector_->detect(image);
    }

    std::vector<Face> scrfd_faces;
    if (scrfdDetector_ != nullptr && scrfdDetector_->isReady())
    {
        scrfd_faces = scrfdDetector_->detect(image);
        for (const auto& face : scrfd_faces)
        {
            face.paintBoundingBox(image, Pixel(200, 200, 200));
            face.paintAllFaceLandmarks(image, false, Pixel(0, 200, 200), 1.5f);
        }
    }

    // Dlib landmark detection using dlib face
    if (dlibShapeDetector_ && !dlib_faces.empty())
    {
        std::vector<math_utils::Rect<float>> rects;
        rects.push_back(dlib_faces[0].getBoundingBox().rect);
        auto dlib_landmark_faces = dlibShapeDetector_->detect(image, rects);
        for (const auto& face : dlib_landmark_faces)
        {
            face.paintBoundingBox(image, Pixel(255, 0, 0));
            face.paintAllFaceLandmarks(image, false, Pixel(255, 0, 0), 1.5f);
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

    // if (mediaPipeLandmarks_ && mediaPipeLandmarks_->isReady() && !scrfd_faces.empty())
    // {
    //     FaceBoundingBox bbx = scrfd_faces[0].getBoundingBox();

    //     // 1. Draw five-point landmarks used for alignment on the original image
    //     auto five_pts_2d = scrfd_faces[0].getFivePointLandmarksArcFaceOrder2D();
    //     // for (size_t i = 0; i < five_pts.size(); ++i)
    //     // {
    //     //     image->ppx(five_pts[i].x, five_pts[i].y, Pixel(0, 255, 255));
    //     //     linuxface::common::log_info("Five-point landmark %zu: (%.1f, %.1f)", i, (float) five_pts[i].x,
    //     //                      (float) five_pts[i].y);
    //     // }

    //     // 2. Affine align
    //     // auto [aligned_image, affine] =
    //     //     image_utils::similarity_face_transform(*raw, five_pts_2d, image_utils::template_192_alt, 192, true);
    //     auto [aligned_image, affine] = image_utils::similarity_face_transform(
    //         *raw, five_pts_2d, image_utils::template_192_alt, 192, true);

    //     if (!aligned_image)
    //     {
    //         linuxface::common::log_error("Failed to wrap face image for MediaPipe landmarks detection");
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
    //             linuxface::common::log_error("Failed to invert affine for MediaPipe unalignment");
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
    //                 linuxface::common::log_warn("MediaPipe landmark out of bounds: (%f, %f, %f)", x, y, z);
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
    //         linuxface::common::log_warn("MediaPipe landmarks detection score too low: %f", result.score);
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
    //         linuxface::common::log_error("Failed to crop aligned face image for MediaPipe landmarks detection");
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
    //         linuxface::common::log_warn("MediaPipe landmarks detection score too low: %f", result.score);
    //     }
    // }

    // PFLD Landmarks detection (using SCRFD face)
    if (pfldDetector_ && pfldDetector_->isReady() && !scrfd_faces.empty())
    {
        // Use the first detected face for demo
        Face face = scrfd_faces[0];

        pfldDetector_->detect(raw, face);

        // Draw landmarks on the image
        face.paintAllFaceLandmarks(image, false, Pixel(255, 25, 0), 1.5f);

        // Small test with WFLW
        auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
        auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);

        auto pfld_landmarks = face.getLandmarks();
        // Load ground truth points for WFLW dataset
        auto gt_landmarks = example_.landmarks;

        for (const auto& lm : pfld_landmarks)
        {
            // draw each landmark index onto the face:
            Layer newText;
            newText.id = Layer::next_id++;
            newText.type = LayerType::Text;
            newText.textContent = std::to_string(lm.i);
            newText.name = "pfld" + std::to_string(lm.i);
            newText.x = lm.p.x + 1;
            newText.y = lm.p.y + 1;
            newText.fontSize = 16.0f;
            newText.textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            layerManager_->addLayer(newText);
        }
        double iod = std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));
        double error_sum = 0.0;
        for (unsigned int i = 0; i < pfld_landmarks.size(); ++i)
        {
            double dx = pfld_landmarks[i].p.x - gt_landmarks[i].x;
            double dy = pfld_landmarks[i].p.y - gt_landmarks[i].y;
            error_sum += std::sqrt(dx * dx + dy * dy) / iod;
        }
        double mne = error_sum / pfld_landmarks.size();
        linuxface::common::log_info("Mean Normalized Error: %.4f", mne);

        // Face face2 = scrfd_faces[0];
        // pfldDetector_->detectSimilar(raw, face2);
        // // Draw landmarks on the image
        // face2.paintAllFaceLandmarks(image, false, Pixel(25, 255, 0), 1.5f);

        // Face face3 = scrfd_faces[0];
        // pfldDetector_->detectOpenCv(raw, face3);
        // // Draw landmarks on the image
        // face3.paintAllFaceLandmarks(image, false, Pixel(25, 255, 255), 1.5f);
    }


    // bool swap_success = false;
    // if (swapPipeline_ && target_img_)
    // {
    //     swap_success = swapPipeline_->run(image, target_img_);
    //     if (swap_success && layerManager_)
    //     {
    //         auto layer = layerManager_->getBaseLayer();
    //         if (layer)
    //         {
    //             layer->dirty = true;
    //         }
    //     }
    // }

    bool swap_success = false;
    if (swapPipeline_ && target_img_)
    {
        swap_success = swapPipeline_->run(image, target_img_);
        if (swap_success && layerManager_)
        {
            auto* layer = layerManager_->getBaseLayer();
            if (layer != nullptr)
            {
                layer->dirty = true;
            }
        }
    }

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
    //         linuxface::common::log_info("Scaling test image to %dx%d", image->info.width, image->info.height);
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

// Capture an image from webcam, process it, and save both raw and processed images with timestamp
void Application::captureAndSaveWebcamImageWithTimestamp()
{
    std::unique_ptr<Image> image;
    if (!cameraManager_->updateInput(image) || !image)
    {
        linuxface::common::log_error("Failed to capture image from webcam");
        return;
    }

    // Get timestamp (seconds since epoch, as integer)
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    // Use the raw time_t value for filename (no formatting)
    std::string timestamp = std::to_string(static_cast<long long>(now_time_t));

    // Save raw image as PPM
    std::string raw_filename = std::string("webcam_raw_") + timestamp + ".ppm";
    if (!image->saveToDisk(raw_filename))
    {
        linuxface::common::log_error("Failed to save raw webcam image to %s", raw_filename.c_str());
    }
    else
    {
        linuxface::common::log_info("Saved raw webcam image: %s", raw_filename.c_str());
    }

    // Process image (in-place)
    process(image);

    // Save processed image as PPM
    std::string processed_filename = std::string("webcam_processed_") + timestamp + ".ppm";
    if (!image->saveToDisk(processed_filename))
    {
        linuxface::common::log_error("Failed to save processed webcam image to %s", processed_filename.c_str());
    }
    else
    {
        linuxface::common::log_info("Saved processed webcam image: %s", processed_filename.c_str());
    }
}

linuxface::math_utils::Point<double>
AlignedToOriginalCoords(double x_aligned, double y_aligned, double crop_left, double crop_top, double min_x, double min_y,
                        double angle_rad, const linuxface::math_utils::Point<double>& eye_center)
{
    // Step 1: undo crop
    double x_rotated = x_aligned + crop_left;
    double y_rotated = y_aligned + crop_top;

    // Step 2: get absolute rotated coordinates
    double x_rel = x_rotated + min_x;
    double y_rel = y_rotated + min_y;

    // Step 3: un-rotate around eye center
    double cosA = std::cos(-angle_rad);
    double sinA = std::sin(-angle_rad);

    double x_orig = cosA * x_rel - sinA * y_rel + eye_center.x;
    double y_orig = sinA * x_rel + cosA * y_rel + eye_center.y;

    return {x_orig, y_orig};
}
