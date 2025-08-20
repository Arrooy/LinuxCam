#include "LinuxFace/UI/paintWebcam.h"

#include "imgui.h"

#include <utility>

#include "LinuxFace/common.h"
#include "LinuxFace/inputWebcam.h"
#include "LinuxFace/v4l2loopbackWritter.h"
#include "LinuxFace/webcam.h"

using namespace linuxface;
const char* subsampling_options[] = {"4:4:4", "4:2:2", "4:2:0", "GRAY", "4:4:0", "4:1:1"};

void PaintWebcam::setWebcam(std::shared_ptr<Webcam> webcam)
{
    webcam_ = std::move(webcam);
}

void PaintWebcam::setNewDeviceModalWebcam(std::shared_ptr<Webcam> webcam)
{
    webcam_new_device_ = std::move(webcam);
}

std::shared_ptr<Webcam> PaintWebcam::getWebcam() const
{
    return webcam_;
}

void PaintWebcam::paintDevice()
{
    if (!webcam_)
    {
        ImGui::Text("No webcam selected");
        return;
    }

    const std::string& camera_key = webcam_->getDevicePath();

    // Device Information
    if (ImGui::CollapsingHeader("Device Information"))
    {
        ImGui::Text("Name: %s", webcam_->getName().c_str());
        ImGui::Text("Path: %s", camera_key.c_str());
        const CameraCapabilities capabilities = webcam_->getCapabilities();
        if (!capabilities.driver.empty())
        {
            ImGui::Text("Driver: %s", capabilities.driver.c_str());
            ImGui::Text("Card: %s", capabilities.card.c_str());
            ImGui::Text("Bus Info: %s", capabilities.bus_info.c_str());
        }
    }

    // Current Settings
    if (ImGui::CollapsingHeader("Current Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Format used_format = webcam_->getSelectedFormat();
        const FrameSize selected_frame_size = used_format.sizes[used_format.selectedFrameSize];
        ImGui::Text("FPS: %u", selected_frame_size.getFps(selected_frame_size.selectedFPS));
        ImGui::Text("Resolution: %ux%u", selected_frame_size.width, selected_frame_size.height);
        ImGui::Text("System format description: %s", used_format.description.c_str());
        ImGui::Text("Pixel format %u", used_format.pixelformat);
        ImGui::Text("Encoding Algorithm %s", fromImageFormatToString(used_format.format).c_str());
        if (webcam_->getType() == WebcamType::VirtualOutput)
        {
            // Cast to V4L2LoopbackWriter to access V4L2-specific methods
            auto v4l2_writer = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
            if (v4l2_writer)
            {
                ImGui::Text("Chrominance Subsampling: %s",
                            subsampling_options[static_cast<int>(v4l2_writer->getChrominanceSubsampling())]);
                ImGui::Text("JPEG Quality: %d", v4l2_writer->getQuality());
            }
        }
    }

    if (webcam_->getType() == WebcamType::PhysicalInput)
    {
        paintPhysicalInput();
    }
    else if (webcam_->getType() == WebcamType::VirtualOutput)
    {
        paintVirtualOutput();
    }

    ImGui::Separator();

    // Enable capturing
    bool enabled = webcam_->isRunning();
    if (ImGui::Checkbox("Active Device", &enabled))
    {
        if (enabled)
        {
            webcam_->start();
        }
        else
        {
            webcam_->stop();
        }
    }

    if (webcam_->getType() == WebcamType::PhysicalInput)
    {
        ImGui::SameLine();
        ImGui::Separator();
        // Enable capturing
        bool selected = webcam_->isCurrentlySelected();
        if (ImGui::Checkbox("Selected Device?", &selected))
        {
            webcam_->setCurrentlySelected(selected);
        }
    }
}

void PaintWebcam::paintPhysicalInput()
{
    const std::string& camera_key = webcam_->getDevicePath();
    // Show available Formats
    if (ImGui::CollapsingHeader("Available Formats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto current_format = webcam_->getSelectedFormat();
        CameraCapabilities capabilities = webcam_->getCapabilities();

        // Calculate current indices from webcam state
        int current_format_idx = -1;
        for (std::size_t i = 0; i < capabilities.formats.size(); i++)
        {
            if (current_format.pixelformat == capabilities.formats[i].pixelformat)
            {
                current_format_idx = static_cast<int>(i);
                break;
            }
        }

        const int current_size_idx = current_format.selectedFrameSize;
        const int current_fps_idx = current_format.sizes[current_format.selectedFrameSize].selectedFPS;

        for (std::size_t fmt_idx = 0; fmt_idx < capabilities.formats.size(); fmt_idx++)
        {
            ImGuiTreeNodeFlags flags = 0;
            // Open treenode of the currently selected format
            if (current_format_idx == static_cast<int>(fmt_idx))
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            const auto& format = capabilities.formats[fmt_idx];
            if (ImGui::TreeNodeEx(("Format: " + format.description).c_str(), flags))
            {
                for (std::size_t size_idx = 0; size_idx < format.sizes.size(); size_idx++)
                {
                    flags = 0;
                    // Auto-expand currently active size for better visibility
                    if (current_format_idx == static_cast<int>(fmt_idx) && current_size_idx == static_cast<int>(size_idx))
                    {
                        flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    }
                    const auto& size = format.sizes[size_idx];
                    const std::string size_text = std::to_string(size.width) + "x" + std::to_string(size.height);
                    if (ImGui::TreeNodeEx(size_text.c_str(), flags))
                    {
                        for (std::size_t fps_idx = 0; fps_idx < size.fps.size(); fps_idx++)
                        {
                            const auto& fps = size.getFps(fps_idx);
                            // common::logError("User selected  fps amount %d in indx %d", fps, fps_idx);
                            const bool is_current = (current_format_idx == static_cast<int>(fmt_idx)
                                                    && current_size_idx == static_cast<int>(size_idx)
                                                    && current_fps_idx == static_cast<int>(fps_idx));

                            ImGui::PushID(fmt_idx * 5000 + size_idx * 500 + fps_idx);
                            const std::string fps_text = std::to_string(fps) + "fps";
                            if (ImGui::Selectable(fps_text.c_str(), is_current))
                            {
                                if (!is_current) // Only apply if it's a
                                                // different selection
                                {
                                    common::logInfo("Applying format change: format %d, "
                                                    "size %dx%d (index %d), fps %d",
                                                    fmt_idx, size.width, size.height, size_idx, fps_idx);

                                    // Apply changes immediately for input camera
                                    auto input_cam = std::dynamic_pointer_cast<InputWebcam>(webcam_);
                                    if (input_cam)
                                    {
                                        if (!input_cam->reconfigureFormat(fmt_idx, size_idx, fps_idx))
                                        {
                                            common::logError("Failed to apply camera format "
                                                             "changes");
                                        }
                                    }
                                }
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }

        // Show current selection info
        if (current_format_idx >= 0 && current_size_idx >= 0 && current_fps_idx >= 0)
        {
            ImGui::Separator();
            const auto& sel_format = capabilities.formats[current_format_idx];
            const auto& sel_size = sel_format.sizes[current_size_idx];
            const auto& sel_fps = sel_size.getFps(current_fps_idx);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Current: %s - %ux%u (%d FPS)",
                               sel_format.description.c_str(), sel_size.width, sel_size.height, sel_fps);
        }
    }
}

void PaintWebcam::paintVirtualOutput()
{
    const std::string& camera_key = webcam_->getDevicePath();

    // Initialize if not exists
    if (selected_subsampling_.find(camera_key) == selected_subsampling_.end())
    {
        // Cast to V4L2LoopbackWriter to access V4L2-specific methods
        auto v4l2_writer = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
        if (v4l2_writer)
        {
            selected_subsampling_[camera_key] = static_cast<int>(v4l2_writer->getChrominanceSubsampling());
        }
        else
        {
            selected_subsampling_[camera_key] = 0; // Default value
        }
    }

    if (ImGui::CollapsingHeader("Available Options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int current_subsampling = selected_subsampling_[camera_key];

        // Show subsampling and quality options
        if (ImGui::Combo("Subsampling", &current_subsampling, subsampling_options, IM_ARRAYSIZE(subsampling_options)))
        {
            selected_subsampling_[camera_key] = current_subsampling;
            common::logWarn("User changed subsampling to %s", subsampling_options[current_subsampling]);
        }
        auto flags_for_sliders = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput
                               | ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_NoSpeedTweaks;

        ImGui::SliderInt("Image Quality", &selected_quality_value_, 0, 100, "%d", flags_for_sliders);

        const bool apply_changes_disabled = selected_subsampling_.find(camera_key) == selected_subsampling_.end();

        if (apply_changes_disabled)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Apply Changes"))
        {
            // Apply subsampling changes for output camera
            auto output_cam = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
            if (output_cam)
            {
                const auto new_subsampling = static_cast<TJSAMP>(selected_subsampling_[camera_key]);
                if (!output_cam->reconfigure(new_subsampling, selected_quality_value_))
                {
                    selected_subsampling_[camera_key] = -1;
                }
            }
        }

        if (apply_changes_disabled)
        {
            ImGui::EndDisabled();
        }
    }
}

bool PaintWebcam::paintAddDeviceModal(const std::vector<std::shared_ptr<Webcam>>& temp_webcams)
{
    if (temp_webcams.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No video devices found in /dev/video*");
        return true;
    }

    // Locate the PopupModal
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Appearing);

    bool modal_open = true;
    if (ImGui::BeginPopupModal("Add New Device", &modal_open,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        const std::string combo_text = webcam_new_device_ ? webcam_new_device_->getDevicePath() : "Select device...";
        if (ImGui::BeginCombo("Video Device", combo_text.c_str()))
        {
            for (const auto& temp_webcam : temp_webcams)
            {
                const bool is_selected =
                    !webcam_new_device_ ? false : (temp_webcam->getDevicePath() == webcam_new_device_->getDevicePath());

                if (ImGui::Selectable(temp_webcam->getDevicePath().c_str(), is_selected))
                {
                    webcam_new_device_ = temp_webcam;
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (webcam_new_device_)
        {
            // TODO(arroyo): Load and show device info using other methods
            ImGui::Text("Device Name: %s", webcam_new_device_->getName().c_str());
        }
        ImGui::Separator();

        if (!webcam_new_device_)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Add Device"))
        {
        }

        if (!webcam_new_device_)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            // Close modal and reset states
            modal_open = false;
            common::logInfo("UI::paintAddDeviceModal - User cancelled adding device");
            ImGui::CloseCurrentPopup();
        }


        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Check if modal was closed by Cancel, X button or ESC key
    if (!modal_open)
    {
        if (webcam_new_device_)
        {
            webcam_new_device_.reset();
        }
        return false;
    }
    return true;
}
