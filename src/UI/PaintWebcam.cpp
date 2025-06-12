#include "FunnyFace/UI/PaintWebcam.h"

#include "FunnyFace/common.h"
#include "FunnyFace/inputWebcam.h"
#include "FunnyFace/v4l2loopbackWritter.h"
#include "FunnyFace/webcam.h"
#include "imgui.h"

using namespace funnyface;
const char* subsampling_options[] = {"4:4:4", "4:2:2", "4:2:0", "GRAY", "4:4:0", "4:1:1"};

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

    const std::string& camera_key = webcam_->getDevicePath();

    // Device Information
    if (ImGui::CollapsingHeader("Device Information"))
    {
        ImGui::Text("Name: %s", webcam_->getName().c_str());
        ImGui::Text("Path: %s", camera_key.c_str());
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
        ImGui::Text("FPS: %d", selectedFrameSize.fps[selectedFrameSize.selectedFPS]);
        ImGui::Text("Resolution: %ux%u", selectedFrameSize.width, selectedFrameSize.height);
        ImGui::Text("System format description: %s", usedFormat.description.c_str());
        ImGui::Text("Pixel format %u", usedFormat.pixelformat);
        ImGui::Text("Encoding Algorithm %s", fromImageFormatToString(usedFormat.format).c_str());
        if (webcam_->getType() == WebcamType::VirtualOutput)
        {
            ImGui::Text("Chrominance Subsampling: %s",
                        subsampling_options[static_cast<int>(webcam_->getChrominanceSubsampling())]);
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
        // Initialize if not exists. Search for the current format
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
            selected_fps_indices_[camera_key] = current_format.sizes[current_format.selectedFrameSize].selectedFPS;
        }

        int& selected_format = selected_format_indices_[camera_key];
        int& selected_size = selected_size_indices_[camera_key];
        int& selected_fps = selected_fps_indices_[camera_key];

        CameraCapabilities capabilities = webcam_->getCapabilities();
        for (std::size_t fmt_idx = 0; fmt_idx < capabilities.formats.size(); fmt_idx++)
        {
            ImGuiTreeNodeFlags flags = 0;
            // Lets open all treenodes of the selected format, size and fps
            if (selected_format == static_cast<int>(fmt_idx))
            {
                // flags |= ImGuiTreeNodeFlags_Selected;
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            const auto& format = capabilities.formats[fmt_idx];
            if (ImGui::TreeNodeEx(("Format: " + format.description).c_str(), flags))
            {
                // TODO: FIXME: Make sure device format selection works.

                for (std::size_t size_idx = 0; size_idx < format.sizes.size(); size_idx++)
                {
                    flags = 0;
                    if (selected_format == static_cast<int>(fmt_idx) && selected_size == static_cast<int>(size_idx))
                    {
                        flags |= ImGuiTreeNodeFlags_Selected;
                        flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    }
                    const auto& size = format.sizes[size_idx];
                    std::string size_text = std::to_string(size.width) + "x" + std::to_string(size.height);
                    if (ImGui::TreeNodeEx(size_text.c_str(), flags))
                    {
                        for (std::size_t fps_idx = 0; fps_idx < size.fps.size(); fps_idx++)
                        {
                            const auto& fps = size.fps[fps_idx];
                            // common::log_error("User selected  fps amount %d in indx %d", fps, fps_idx);
                            bool is_current = (selected_format == static_cast<int>(fmt_idx)
                                               && selected_size == static_cast<int>(size_idx)
                                               && selected_fps == static_cast<int>(fps_idx));

                            ImGui::PushID(fmt_idx * 5000 + size_idx * 500 + fps_idx);
                            std::string fps_text = std::to_string(fps) + "fps";
                            if (ImGui::Selectable(fps_text.c_str(), is_current))
                            {
                                if (is_current)
                                {
                                    // Unselect the current format and size
                                    selected_format = -1;
                                    selected_size = -1;
                                    selected_fps = -1;
                                }
                                else
                                {
                                    common::log_warn(
                                        "User selected format index %d, size %dx%d (index %d) of index fps %d", fmt_idx,
                                        size.width, size.height, size_idx, fps_idx);
                                    selected_format = fmt_idx;
                                    selected_size = size_idx;
                                    selected_fps = fps_idx;
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

        if (selected_format >= 0 && selected_size >= 0 && selected_fps >= 0)
        {
            ImGui::Separator();
            const auto& sel_format = capabilities.formats[selected_format];
            const auto& sel_size = sel_format.sizes[selected_size];
            const auto& sel_fps = sel_size.fps[selected_fps];
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Selection: %s - %ux%u (%d FPS)",
                               sel_format.description.c_str(), sel_size.width, sel_size.height, sel_fps);
        }

        bool applyChangesDisabled = selected_format_indices_.find(camera_key) == selected_format_indices_.end()
                                    || selected_format_indices_[camera_key] < 0
                                    || selected_size_indices_[camera_key] < 0 || selected_fps_indices_[camera_key] < 0;

        if (applyChangesDisabled)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Apply Changes"))
        {
            common::log_info("PaintWebcam::paintGeneralizedDeviceConfig - Applying device configuration changes for %s",
                             camera_key.c_str());

            // Apply format/size changes for input camera
            auto inputCam = std::dynamic_pointer_cast<InputWebcam>(webcam_);
            if (inputCam)
            {
                if (!inputCam->reconfigureFormat(selected_format_indices_[camera_key],
                                                 selected_size_indices_[camera_key], 0))
                {
                    // Reset selections after failure apply
                    selected_format_indices_[camera_key] = -1;
                    selected_size_indices_[camera_key] = -1;
                    selected_fps_indices_[camera_key] = -1;
                }
            }
        }

        if (applyChangesDisabled)
        {
            ImGui::EndDisabled();
        }
    }
}

void PaintWebcam::paintVirtualOutput()
{
    const std::string& camera_key = webcam_->getDevicePath();


    // Initialize if not exists
    if (selected_subsampling_.find(camera_key) == selected_subsampling_.end())
    {
        selected_subsampling_[camera_key] = static_cast<int>(webcam_->getChrominanceSubsampling());
    }


    if (ImGui::CollapsingHeader("Available Options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int current_subsampling = selected_subsampling_[camera_key];

        // Show subsampling and quality options
        if (ImGui::Combo("Subsampling", &current_subsampling, subsampling_options, IM_ARRAYSIZE(subsampling_options)))
        {
            selected_subsampling_[camera_key] = current_subsampling;
            common::log_warn("User changed subsampling to %s", subsampling_options[current_subsampling]);
        }
        auto flags_for_sliders = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput
                                 | ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_NoSpeedTweaks;

        ImGui::SliderInt("Image Quality", &selected_quality_value_, 0, 100, "%d", flags_for_sliders);

        bool applyChangesDisabled = selected_subsampling_.find(camera_key) == selected_subsampling_.end();

        if (applyChangesDisabled)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Apply Changes"))
        {
            // Apply subsampling changes for output camera
            auto outputCam = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
            if (outputCam)
            {
                TJSAMP new_subsampling = static_cast<TJSAMP>(selected_subsampling_[camera_key]);
                if (!outputCam->reconfigure(new_subsampling, selected_quality_value_))
                {
                    selected_subsampling_[camera_key] = -1;
                }
            }
        }

        if (applyChangesDisabled)
        {
            ImGui::EndDisabled();
        }
    }
}
