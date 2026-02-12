#ifndef APP_H
#define APP_H

#include <memory>
#include <thread>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/mediaManager.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/cameraManager.h"
#include "LinuxFace/detectors.h"
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/imageRenderGL.h"
#include "LinuxFace/onnx/MODNet.h"
#include "LinuxFace/onnx/arcfaceRecognizer.h"
#include "LinuxFace/onnx/faceSegmentation.h"
#include "LinuxFace/onnx/fsanet.h"
#include "LinuxFace/onnx/inswapper.h"
#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/rvm.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/onnx/swapPipeline.h"
#include "LinuxFace/profiler.h"
#include "LinuxFace/ui.h"
#include "LinuxFace/web/IStreamTransport.h"
#include "LinuxFace/web/wsInputDevice.h"
#include "LinuxFace/window.h"

// Web-scraping API clients
#include "LinuxFace/webscraping/pexelsAPI.h"

namespace linuxface
{

class Application : public std::enable_shared_from_this<Application>
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

    // Set target image directly from an in-memory Image (preferred when available)
    void setTargetImage(std::unique_ptr<Image> image);

    // Trigger single-loop profiler capture on next frame
    void requestLoopCapture() { captureNextLoop_ = true; }

    // Check if running in headless mode
    bool isHeadless() const { return headlessMode_; }

  private:
    void connectWindowResize();
    void calculateCompositeBounds(const std::vector<Layer>& layers, int windowWidth, int windowHeight, float& minX,
                                  float& minY, float& maxX, float& maxY);
    bool createCompositeImage(const std::vector<Layer>& layers, float minX, float minY, unsigned int compositeWidth,
                              unsigned int compositeHeight);

    Window window_;
    std::unique_ptr<UI> ui_;

    std::shared_ptr<CameraManager> cameraManager_;
    std::shared_ptr<ImageRenderGL> imageRender_;
    std::shared_ptr<LayerManager> layerManager_;

    Profiler& profiler_;

    std::unique_ptr<FaceDetector> faceDetector_;
    std::unique_ptr<FsanetDetector> fsanetDetectorVar_;
    std::unique_ptr<FsanetDetector> fsanetDetectorConv_;
    std::shared_ptr<SCRFDetector> scrfdDetector_;
    std::unique_ptr<MODNetDetector> modnetDetector_;
    std::unique_ptr<RobustVideoMatting> rvmDetector_;
    std::shared_ptr<ArcfaceRecognizer> arcfaceRecognizer_;
    std::shared_ptr<InSwapper> inswapper_;
    std::unique_ptr<SwapPipeline> swapPipeline_;

    std::shared_ptr<MediaPipeFaceLandmarks> mediaPipeLandmarks_;
    std::shared_ptr<MediaPipeFaceLandmarks> mediaPipeLandmarksOld_;
    std::shared_ptr<PFLDDetector> pfldDetector_;
    std::shared_ptr<FaceSegmentationDetector> faceSegmentationDetector_;

    std::shared_ptr<MediaManager> mediaManager_;

    std::unique_ptr<Image> adria_img_;
    std::unique_ptr<Image> target_img_;

    // Reusable composite buffer to avoid allocations every frame
    std::unique_ptr<Image> compositeBuffer_;
    unsigned int lastCompositeWidth_{0};
    unsigned int lastCompositeHeight_{0};

    // WebSocket input device for browser-based video streaming
    std::shared_ptr<wsInputDevice> wsInputDevice_;
    std::thread webServerThread_;

    // Stream transports - support multiple simultaneously
    std::vector<std::shared_ptr<web::IStreamTransport>> streamTransports_;

    // Single-loop profiler capture flag
    std::atomic<bool> captureNextLoop_{false};

    // Headless mode flag (no GUI available)
    bool headlessMode_{false};

    // Pexels API client
    std::unique_ptr<PexelsAPI> pexelsApi_;

    // Main loop methods
    bool update();
    void process(std::unique_ptr<Image>& image);
    void render();
    void stopWebServer();
    bool initializeWebSocket();
    void handleTargetImageUpdate(const std::vector<uint8_t>& imageData);
};

} // namespace linuxface

#endif // APP_H
