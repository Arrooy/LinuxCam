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

    // DlibShapeDetector initialization (landmarks)
    std::string dlib_shape_model = models_folder + "shape_predictor_68_face_landmarks.dat";
    dlibShapeDetector_ = std::make_unique<DlibShapeDetector>(dlib_shape_model);

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

    // MediaPipe Face Landmarks initialization
    std::string mediapipe_landmarks_model = models_folder + "MediaPipeFaceLandmarkDetector.onnx";
    // std::string mediapipe_landmarks_model = models_folder + "face_landmark_barracuda.onnx";
    mediaPipeLandmarks_ = std::make_shared<MediaPipeFaceLandmarks>(mediapipe_landmarks_model);

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

    // Load target faceswap image once
    std::string target_path = Config::getInstance().getMediaFolderPath() + "a.jpeg";
    target_img_ = ImageLoader::loadImageFromFile(target_path);
    if (!target_img_)
    {
        common::log_error("Failed to load image at initialization");
    }

    // process(target_img_);
    // target_img_->saveToDisk("../a_inf.ppm");
    // return false;

    // PFLD Landmarks initialization
    std::string pfld_model = models_folder + "pfld-106-v3.onnx";
    pfldDetector_ = std::make_shared<PFLDDetector>(pfld_model);

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
    mediaManager_->shutdown();

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
    auto raw = image->deepCopy();
 
    std::vector<Face> dlib_faces;
    if (faceDetector_ != nullptr)
    {
        dlib_faces = faceDetector_->detect(image);
    }

    std::vector<Face> scrfd_faces;
    if (scrfdDetector_ != nullptr && scrfdDetector_->isReady())
    {
        scrfd_faces = scrfdDetector_->detect(image);
        for(const auto& face : scrfd_faces)
        {
            face.paintBoundingBox(image, Pixel(200,200,200));
            face.paintAllFaceLandmarks(image, false, Pixel(200,200,200), 1.5f);
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
            face.paintAllFaceLandmarks(image, false, Pixel(255,0,0), 1.5f);
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

    // if (mediaPipeLandmarks_ && mediaPipeLandmarks_->isReady() && !scrfd_faces.empty())
    // {
    //     FaceBoundingBox bbx = scrfd_faces[0].getBoundingBox();
    //     // Lets add some padding of the image.
    //     // bbx.rect.addPadding(20.0f, 60.0f, 20.0f, 20.0f);
    //     auto face_image = raw->crop(bbx.rect); // TODO care cose it could be smaller than 192x192
    //     if (!face_image)
    //     {
    //         common::log_error("Failed to crop face image for MediaPipe landmarks detection");
    //     }
    //     else
    //     {
    //         common::log_info("Crop image: %ldx%ld", face_image->info.width, face_image->info.height);
    //         face_image->saveToDisk("face_image_crop.ppm");

    //         // 1. Draw five-point landmarks used for alignment on the original image
    //         auto five_pts_2d = scrfd_faces[0].getFivePointLandmarksArcFaceOrder2D();
    //         // for (size_t i = 0; i < five_pts.size(); ++i)
    //         // {
    //         //     image->ppx(five_pts[i].x, five_pts[i].y, Pixel(0, 255, 255));
    //         //     common::log_info("Five-point landmark %zu: (%.1f, %.1f)", i, (float) five_pts[i].x,
    //         //                      (float) five_pts[i].y);
    //         // }

    //         // 2. Affine align
    //         auto [aligned_image, affine] =
    //             image_utils::affine_face_transform(*raw, five_pts_2d, image_utils::template_192, 192, true);
    //         if (!aligned_image)
    //         {
    //             common::log_error("Failed to wrap face image for MediaPipe landmarks detection");
    //             return;
    //         }
    //         auto test = aligned_image->deepCopy();
    //         test->drawBorder(Pixel(255, 100, 50), 2);
    //         image->pasteAt(*test, 640, 0, true);
    //         auto result = mediaPipeLandmarks_->detect(aligned_image);

    //         if (result.score > 0.5)
    //         {
    //             // 4. Map landmarks back to original image
    //             double invM[6];
    //             if (!math_utils::invert_affine(affine.data(), invM))
    //             {
    //                 common::log_error("Failed to invert affine for MediaPipe unalignment");
    //                 return;
    //             }

    //             double w = aligned_image->info.width;
    //             double h = aligned_image->info.height;
    //             std::vector<std::pair<double, double>> aligned_pts;
    //             std::vector<float> aligned_z;
    //             for (const auto& landmark : result.landmarks)
    //             {
    //                 aligned_pts.emplace_back(landmark[0] * w, landmark[1] * h);
    //                 aligned_z.push_back(landmark[2]);
    //             }
    //             auto unaligned_pts = image_utils::transform_points_affine(aligned_pts, invM);
    //             for (size_t i = 0; i < unaligned_pts.size(); ++i)
    //             {
    //                 double x = static_cast<double>(unaligned_pts[i].first);
    //                 double y = static_cast<double>(unaligned_pts[i].second);
    //                 float z = aligned_z[i];
    //                 if (x < 0 || x >= image->info.width || y < 0 || y >= image->info.height)
    //                 {
    //                     common::log_warn("MediaPipe landmark out of bounds: (%f, %f, %f)", x, y, z);
    //                     continue;
    //                 }
    //                 image->ppx(x, y, Pixel(0, 0, 255));
    //                 image_utils::paintCircle(image, math_utils::Point3D(x, y, z), 1.5f, Pixel(0, 0, 255));
    //             }
    //             auto layer = layerManager_->getBaseLayer();
    //             if (layer)
    //             {
    //                 layer->dirty = true;
    //             }
    //         }
    //         else
    //         {
    //             common::log_warn("MediaPipe landmarks detection score too low: %f", result.score);
    //         }
    //     }
    // }

    if (mediaPipeLandmarks_ && mediaPipeLandmarks_->isReady() && !scrfd_faces.empty())
    {
        auto face = scrfd_faces[0];

        auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
        auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);

        math_utils::Point<double> eye_center = {(left_eye.x + right_eye.x) / 2.0,
                                                (left_eye.y + right_eye.y) / 2.0};
        double bbox_scale_factor = 1.7; // Scale factor for bounding box size
        double dx = right_eye.x - left_eye.x;
        double dy = right_eye.y - left_eye.y;
        double angleRad = -std::atan2(dy, dx); // rotate to horizontal
        double eye_dist = std::sqrt(dx * dx + dy * dy);

        // Use bounding box from SCRFD
        auto bbox = face.getBoundingBox().rect;

        // Store original image dimensions
        unsigned long orig_width = raw->info.width;
        unsigned long orig_height = raw->info.height;

        // Compute center of the bounding box in original coordinates
        math_utils::Point<double> face_center_original = {
            bbox.l + bbox.width() / 2.0,
            bbox.t + bbox.height() / 2.0
        };

        auto aligned_face = raw->deepCopy();

        // Rotate the whole image
        auto translation_offset = aligned_face->rotate(angleRad, eye_center);

        common::log_info("Original image size: %lu x %lu", orig_width, orig_height);
        common::log_info("Rotated image size: %lu x %lu", aligned_face->info.width, aligned_face->info.height);
        common::log_info("Translation offset: (%.1f, %.1f)", translation_offset.x, translation_offset.y);

        auto test_rot = aligned_face->deepCopy();
        test_rot->scaleInPlace(0.2f, ScalingAlgorithm::AREA_AVERAGING);
        image->pasteAt(*test_rot, 640, 0, true);

        // Calculate where the eye center should be in the rotated image
        // We need to simulate the same transformation that the rotation function does
        double cosA = std::cos(angleRad);
        double sinA = std::sin(angleRad);
        
        // Calculate the corners of the original image relative to the eye center
        double corners[4][2] = {
            { -eye_center.x, -eye_center.y },  // top-left
            { orig_width - 1 - eye_center.x, -eye_center.y },  // top-right
            { -eye_center.x, orig_height - 1 - eye_center.y },  // bottom-left
            { orig_width - 1 - eye_center.x, orig_height - 1 - eye_center.y }  // bottom-right
        };
        
        // Find the bounding box of rotated corners
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (int i = 0; i < 4; ++i) {
            double x = corners[i][0] * cosA - corners[i][1] * sinA;
            double y = corners[i][0] * sinA + corners[i][1] * cosA;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
        
        // The eye center in the rotated image should be at the origin of the rotated coordinate system
        // adjusted by the translation to fit all pixels
        math_utils::Point<double> eye_center_rotated = {
            0.0 - minX,  // eye center becomes origin, then shift by -minX
            0.0 - minY   // eye center becomes origin, then shift by -minY
        };

        // Now calculate where the face center should be in the rotated image
        // First, get face center relative to eye center in original image
        double face_dx = face_center_original.x - eye_center.x;
        double face_dy = face_center_original.y - eye_center.y;
        
        // Rotate this relative vector
        math_utils::Point<double> face_center_rotated_relative = {
            cosA * face_dx - sinA * face_dy,
            sinA * face_dx + cosA * face_dy
        };
        
        // Add to the rotated eye center position
        math_utils::Point<double> final_face_center = {
            face_center_rotated_relative.x + eye_center_rotated.x,
            face_center_rotated_relative.y + eye_center_rotated.y
        };

        // Calculate box size based on eye distance for more robust sizing
        double base_box_size = eye_dist * 3.0; // Base size relative to eye distance
        double bbox_box_size = std::max(bbox.width(), bbox.height()) * bbox_scale_factor;
        double box_size = std::max(base_box_size, bbox_box_size);

        // Ensure the box fits within the rotated image bounds
        double max_box_size = std::min(aligned_face->info.width, aligned_face->info.height) * 0.9;
        box_size = std::min(box_size, max_box_size);

        // Calculate crop rectangle bounds
        double half_box = box_size / 2.0;
        double crop_left = final_face_center.x - half_box;
        double crop_top = final_face_center.y - half_box;
        double crop_right = final_face_center.x + half_box;
        double crop_bottom = final_face_center.y + half_box;

        // Ensure crop rectangle is within image bounds
        if (crop_left < 0 || crop_top < 0 || 
            crop_right > aligned_face->info.width || crop_bottom > aligned_face->info.height)
        {
            // Adjust the center to keep the crop within bounds
            if (crop_left < 0) {
                final_face_center.x = half_box;
            } else if (crop_right > aligned_face->info.width) {
                final_face_center.x = aligned_face->info.width - half_box;
            }
            
            if (crop_top < 0) {
                final_face_center.y = half_box;
            } else if (crop_bottom > aligned_face->info.height) {
                final_face_center.y = aligned_face->info.height - half_box;
            }
            
            // Recalculate crop bounds
            crop_left = final_face_center.x - half_box;
            crop_top = final_face_center.y - half_box;
            
            common::log_warn("Adjusted crop center to fit within image bounds: (%.1f, %.1f)", 
                            final_face_center.x, final_face_center.y);
        }

        // Create crop rectangle
        math_utils::Point<float> left_corner {
            static_cast<float>(crop_left),
            static_cast<float>(crop_top)
        };

        math_utils::Rect<float> crop_rect = {
            left_corner,
            static_cast<float>(box_size),
            static_cast<float>(box_size)
        };
        
        // Log all the coordinate transformations for debugging
        common::log_info("Eye center original: (%.1f, %.1f)", eye_center.x, eye_center.y);
        common::log_info("Calculated minX, minY: (%.1f, %.1f)", minX, minY);
        common::log_info("Eye center rotated: (%.1f, %.1f)", eye_center_rotated.x, eye_center_rotated.y);
        common::log_info("Face center original: (%.1f, %.1f)", face_center_original.x, face_center_original.y);
        common::log_info("Face center relative to eye: (%.1f, %.1f)", face_dx, face_dy);
        common::log_info("Face center rotated relative: (%.1f, %.1f)", face_center_rotated_relative.x, face_center_rotated_relative.y);
        common::log_info("Final face center: (%.1f, %.1f)", final_face_center.x, final_face_center.y);
        common::log_info("Box size: %.1f", box_size);
        common::log_info("Crop rect: [%.1f, %.1f, %.1f, %.1f]", crop_rect.l, crop_rect.t, crop_rect.width(), crop_rect.height());
        common::log_info("Crop bounds: left=%.1f, top=%.1f, right=%.1f, bottom=%.1f", 
                        crop_left, crop_top, crop_left + box_size, crop_top + box_size);

        aligned_face = aligned_face->crop(crop_rect);
        if (!aligned_face)
        {
            common::log_error("Failed to crop aligned face image for MediaPipe landmarks detection");
            return;
        }

        image->pasteAt(*aligned_face, 0, 480, true);
        common::log_info("Aligned face size: %ldx%ld", aligned_face->info.width, aligned_face->info.height);

        auto result = mediaPipeLandmarks_->detect(aligned_face);
        if (result.score > 0.5)
        {
            // Draw predicted landmarks on aligned image
            for (size_t i = 0; i < result.landmarks.size(); ++i)
            {
                double x = result.landmarks[i][0] * aligned_face->info.width;
                double y = result.landmarks[i][1] * aligned_face->info.height;
                if (x < 0 || x >= aligned_face->info.width || y < 0 || y >= aligned_face->info.height)
                {
                    continue;
                }

                aligned_face->ppx(x, y, Pixel(0, 0, 255));
                image_utils::paintCircle(aligned_face, math_utils::Point3D(x, y, 0), 1.0f, Pixel(0, 0, 255));
            }
            // Show aligned image with landmarks
            image->pasteAt(*aligned_face, aligned_face->info.width, 480, true);
        }
        else
        {
            common::log_warn("MediaPipe landmarks detection score too low: %f", result.score);
        }
    }
    // PFLD Landmarks detection (using SCRFD face)
    // if (pfldDetector_ && pfldDetector_->isReady() && !scrfd_faces.empty())
    // {
    //     // Use the first detected face for demo
    //     Face& face = scrfd_faces[0];
    //     pfldDetector_->detect(raw, face);
    //     // Draw landmarks on the image
    //     face.paintAllFaceLandmarks(image, false, Pixel(0, 255, 0), 1.5f);
    // }
    
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
        common::log_error("Failed to capture image from webcam");
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
        common::log_error("Failed to save raw webcam image to %s", raw_filename.c_str());
    }
    else
    {
        common::log_info("Saved raw webcam image: %s", raw_filename.c_str());
    }

    // Process image (in-place)
    process(image);

    // Save processed image as PPM
    std::string processed_filename = std::string("webcam_processed_") + timestamp + ".ppm";
    if (!image->saveToDisk(processed_filename))
    {
        common::log_error("Failed to save processed webcam image to %s", processed_filename.c_str());
    }
    else
    {
        common::log_info("Saved processed webcam image: %s", processed_filename.c_str());
    }
}
