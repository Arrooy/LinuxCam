#include "FunnyFace/UI/PaintWebcam.h"
#include "FunnyFace/webcam.h"
#include "FunnyFace/inputWebcam.h"
#include "FunnyFace/v4l2loopbackWritter.h"
#include "FunnyFace/common.h"
#include "imgui.h"

using namespace funnyface;

void PaintWebcam::setWebcam(std::shared_ptr<Webcam> webcam)
{
    webcam_ = webcam;
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

    // Device Information
    if (ImGui::CollapsingHeader("Device Information"))
    {
        ImGui::Text("Name: %s", webcam_->getName().c_str());
        ImGui::Text("Path: %s", webcam_->getDevicePath().c_str());
        CameraCapabilities capabilities = webcam_->getCapabilities();
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
        Format usedFormat = webcam_->getSelectedFormat();
        FrameSize selectedFrameSize = usedFormat.sizes[usedFormat.selectedFrameSize];
        ImGui::Text("Format: %s", usedFormat.description.c_str());
        ImGui::Text("Resolution: %ux%u", selectedFrameSize.width, selectedFrameSize.height);
        ImGui::Text("Pixel format %u", usedFormat.pixelformat);
        ImGui::Text("Image format %s", fromImageFormatToString(usedFormat.format).c_str());

        // Show subsampling for output devices
        if (webcam_->getType() == WebcamType::VirtualOutput)
        {
            const char* subsampling_options[] = {"4:4:4", "4:2:2", "4:2:0", "GRAY", "4:4:0", "4:1:1"};

            // Get camera name as key for tracking selections
            std::string camera_key = webcam_->getDevicePath();

            // Initialize if not exists
            if (selected_subsampling_.find(camera_key) == selected_subsampling_.end())
            {
                selected_subsampling_[camera_key] = static_cast<int>(webcam_->getChrominanceSubsampling());
            }

            int current_subsampling = selected_subsampling_[camera_key];

            ImGui::Text("Current: %s", subsampling_options[static_cast<int>(webcam_->getChrominanceSubsampling())]);
            if (ImGui::Combo("Subsampling", &current_subsampling, subsampling_options,
                             IM_ARRAYSIZE(subsampling_options)))
            {
                selected_subsampling_[camera_key] = current_subsampling;
                common::log_warn("User changed subsampling to %s", subsampling_options[current_subsampling]);
            }
        }
    }

    // Available Formats (for input devices)
    if (webcam_->getType() == WebcamType::PhysicalInput
        && ImGui::CollapsingHeader("Available Formats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        std::string camera_key = webcam_->getDevicePath();

        // Initialize if not exists
        if (selected_format_indices_.find(camera_key) == selected_format_indices_.end())
        {
            auto current_format = webcam_->getSelectedFormat();
            selected_format_indices_[camera_key] = 0;
            for (auto& format : webcam_->getCapabilities().formats)
            {
                if (current_format.pixelformat == format.pixelformat)
                {
                    break;
                }
                selected_format_indices_[camera_key]++;
            }
            selected_size_indices_[camera_key] = current_format.selectedFrameSize;
        }

        int& selected_format = selected_format_indices_[camera_key];
        int& selected_size = selected_size_indices_[camera_key];

        CameraCapabilities capabilities = webcam_->getCapabilities();
        for (std::size_t fmt_idx = 0; fmt_idx < capabilities.formats.size(); fmt_idx++)
        {
            const auto& format = capabilities.formats[fmt_idx];

            if (ImGui::TreeNode(("Format " + std::to_string(fmt_idx) + ": " + format.description).c_str()))
            {
                ImGui::Text("Pixel Format: 0x%08X", format.pixelformat);

                if (ImGui::TreeNode("Available Sizes"))
                {
                    for (std::size_t size_idx = 0; size_idx < format.sizes.size(); size_idx++)
                    {
                        const auto& size = format.sizes[size_idx];
                        bool is_current = (selected_format == static_cast<int>(fmt_idx)
                                           && selected_size == static_cast<int>(size_idx));

                        ImGui::PushID(fmt_idx * 1000 + size_idx);
                        if (ImGui::Selectable((std::to_string(size_idx) + " - " + std::to_string(size.width) + "x"
                                               + std::to_string(size.height))
                                                  .c_str(),
                                              is_current))
                        {
                            if (is_current)
                            {
                                selected_format = -1;
                                selected_size = -1;
                            }
                            else
                            {
                                common::log_warn("User selected format %d, size %dx%d", fmt_idx, size.width,
                                                 size.height);
                                selected_format = fmt_idx;
                                selected_size = size_idx;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }

        if (selected_format >= 0 && selected_size >= 0)
        {
            ImGui::Separator();
            const auto& sel_format = capabilities.formats[selected_format];
            const auto& sel_size = sel_format.sizes[selected_size];
            ImGui::Text("Selected: %s - %ux%u", sel_format.description.c_str(), sel_size.width, sel_size.height);
        }
    }

    // Apply Changes Button
    ImGui::Separator();
    if (ImGui::Button("Apply Changes"))
    {
        common::log_info("PaintWebcam::paintGeneralizedDeviceConfig - Applying device configuration changes for %s",
                         webcam_->getDevicePath().c_str());

        std::string camera_key = webcam_->getDevicePath();
        bool success = false;

        if (webcam_->getType() == WebcamType::PhysicalInput)
        {
            // Apply format/size changes for input camera
            if (selected_format_indices_.find(camera_key) != selected_format_indices_.end()
                && selected_format_indices_[camera_key] >= 0 && selected_size_indices_[camera_key] >= 0)
            {
                auto inputCam = std::dynamic_pointer_cast<InputWebcam>(webcam_);
                if (inputCam)
                {
                    if (!inputCam->reconfigureFormat(selected_format_indices_[camera_key],
                                                     selected_size_indices_[camera_key]))
                    {
                        common::log_error("Apply Changes: Failed to reconfigure format.");
                        success = false;
                    }
                    else
                    {
                        success = true;
                        // Reset selections after successful apply
                        selected_format_indices_[camera_key] = -1;
                        selected_size_indices_[camera_key] = -1;
                    }
                }
            }
            else
            {
                common::log_warn("No format/size selected for input camera %s", camera_key.c_str());
            }
        }
        else if (webcam_->getType() == WebcamType::VirtualOutput)
        {
            // Apply subsampling changes for output camera
            if (selected_subsampling_.find(camera_key) != selected_subsampling_.end())
            {
                auto outputCam = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
                if (outputCam)
                {
                    TJSAMP new_subsampling = static_cast<TJSAMP>(selected_subsampling_[camera_key]);
                    success = outputCam->reconfigureSubsampling(new_subsampling);
                }
            }
        }

        if (success)
        {
            common::log_info("Successfully applied configuration changes to %s", camera_key.c_str());
        }
        else
        {
            common::log_error("Failed to apply configuration changes to %s", camera_key.c_str());
        }
    }

    ImGui::SameLine();
    if (webcam_->getType() == WebcamType::VirtualOutput)
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to apply subsampling changes");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to apply format and resolution changes");
    }
    ImGui::Separator();

    // Enable capturing
    bool enabled = webcam_->isRunning();
    if (ImGui::Checkbox("Device active", &enabled))
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
        if (ImGui::Checkbox("Device selected ?", &selected))
        {
            webcam_->setCurrentlySelected(selected);
        }
    }
}
