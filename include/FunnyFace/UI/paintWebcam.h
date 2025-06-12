#ifndef PAINTWEBCAM_H
#define PAINTWEBCAM_H

#include <map>
#include <memory>
#include <string>
#include <vector>

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
    void setNewDeviceModalWebcam(std::shared_ptr<Webcam> webcam);
    std::shared_ptr<Webcam> getWebcam() const;

    // Paint the generalized device configuration
    void paintDevice();
    /**
     * Paint the modal for adding a new device. This is a modal that is displayed when the user clicks on the "+" tab.
     * @param temp_webcams A vector of webcams that are available to the user.
     * This is used to populate the dropdown menu for selecting the webcam.
     * @return True if the modal should continue to display, false otherwise
     */
    bool paintAddDeviceModal(std::vector<std::shared_ptr<Webcam>> temp_webcams);

  private:
    void paintPhysicalInput();
    void paintVirtualOutput();


    // The webcam being displayed
    std::shared_ptr<Webcam> webcam_;
    // The webcam currently selected in select new device
    std::shared_ptr<Webcam> webcam_new_device_;

    // State tracking for UI selections per camera
    std::map<std::string, int> selected_format_indices_;
    std::map<std::string, int> selected_size_indices_;
    std::map<std::string, int> selected_fps_indices_;
    std::map<std::string, int> selected_subsampling_;
    int selected_quality_value_{100};
};

} // namespace funnyface

#endif // PAINTWEBCAM_H
