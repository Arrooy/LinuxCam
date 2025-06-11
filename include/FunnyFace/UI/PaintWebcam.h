#ifndef PAINTWEBCAM_H
#define PAINTWEBCAM_H

#include <map>
#include <memory>
#include <string>

namespace funnyface
{

// Forward declarations
class Webcam;

class PaintWebcam
{
  public:
    PaintWebcam() = default;
    ~PaintWebcam() = default;

    void setWebcam(std::shared_ptr<Webcam> webcam);
    std::shared_ptr<Webcam> getWebcam() const;

    // Paint the generalized device configuration
    void paintDevice();

  private:
    // The webcam being displayed
    std::shared_ptr<Webcam> webcam_;

    // State tracking for UI selections per camera
    std::map<std::string, int> selected_format_indices_;
    std::map<std::string, int> selected_size_indices_;
    std::map<std::string, int> selected_subsampling_;
};

} // namespace funnyface

#endif // PAINTWEBCAM_H
