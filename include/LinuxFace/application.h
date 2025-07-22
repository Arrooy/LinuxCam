#ifndef APP_H
#define APP_H

#include <memory>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/mediaManager.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/cameraManager.h"
#include "LinuxFace/detectors.h"
#include "LinuxFace/imageLoader.h"

#include "LinuxFace/imageLoader.h"
#include "LinuxFace/imageRenderGL.h"
#include "LinuxFace/onnx/MODNet.h"
#include "LinuxFace/onnx/arcfaceRecognizer.h"
#include "LinuxFace/onnx/fsanet.h"
#include "LinuxFace/onnx/inswapper.h"
#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/rvm.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/onnx/swapPipeline.h"
#include "LinuxFace/profiler.h"
#include "LinuxFace/ui.h"
#include "LinuxFace/window.h"
namespace linuxface
{

class Application
{
  public:
    Application();
    ~Application();

    // Initialize the application
    bool initialize();

    // Run the main application loop
    void run();

    // Shutdown the application
    void shutdown();

  private:
    // Connect window resize to layerManager texture invalidation
    void connectWindowResize();
    Window window_;
    std::unique_ptr<UI> ui_;

    std::shared_ptr<CameraManager> cameraManager_;
    std::shared_ptr<ImageRenderGL> imageRender_;
    std::shared_ptr<LayerManager> layerManager_;

    Profiler& profiler_;

    std::unique_ptr<FaceDetector> faceDetector_;
    std::unique_ptr<ShapeDetector> dlibShapeDetector_;
    std::unique_ptr<FsanetDetector> fsanetDetectorVar_;
    std::unique_ptr<FsanetDetector> fsanetDetectorConv_;
    std::shared_ptr<SCRFDetector> scrfdDetector_;
    std::unique_ptr<MODNetDetector> modnetDetector_;
    std::unique_ptr<RobustVideoMatting> rvmDetector_;
    std::shared_ptr<ArcfaceRecognizer> arcfaceRecognizer_;
    std::shared_ptr<InSwapper> inswapper_;
    std::unique_ptr<SwapPipeline> swapPipeline_;

    std::shared_ptr<MediaPipeFaceLandmarks> mediaPipeLandmarks_;
    std::shared_ptr<PFLDDetector> pfldDetector_;

    std::shared_ptr<MediaManager> mediaManager_;

    std::unique_ptr<Image> adria_img_;
    std::unique_ptr<Image> target_img_;
    std::unique_ptr<Image> b_img_;

    // Main loop methods
    bool update();
    void process(std::unique_ptr<Image>& image);
    void render();
    void captureAndSaveWebcamImageWithTimestamp();
};

} // namespace linuxface

#endif // APP_H
