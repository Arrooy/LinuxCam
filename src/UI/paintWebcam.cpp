#include "LinuxFace/UI/paintWebcam.h"

#include "imgui.h"

#include <utility>

#include "LinuxFace/common.h"
#include "LinuxFace/inputWebcam.h"
#include "LinuxFace/v4l2loopbackWritter.h"
#include "LinuxFace/webcam.h"

using namespace linuxface;
const char* subsamplingOptions[] = {"4:4:4", "4:2:2", "4:2:0", "GRAY", "4:4:0", "4:1:1"};

void PaintWebcam::setWebcam(std::shared_ptr<Webcam> webcam)
{
    webcam_ = std::move(webcam);
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

    const std::string& cameraKey = webcam_->getDevicePath();

    // Device Information
    if (ImGui::CollapsingHeader("Device Information"))
    {
        ImGui::Text("Name: %s", webcam_->getName().c_str());
        ImGui::Text("Path: %s", cameraKey.c_str());
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
        const Format usedFormat = webcam_->getSelectedFormat();
        const FrameSize selectedFrameSize = usedFormat.sizes[usedFormat.selectedFrameSize];
        ImGui::Text("FPS: %u", selectedFrameSize.getFps(selectedFrameSize.selectedFPS));
        ImGui::Text("Resolution: %ux%u", selectedFrameSize.width, selectedFrameSize.height);
        ImGui::Text("System format description: %s", usedFormat.description.c_str());
        ImGui::Text("Pixel format %u", usedFormat.pixelformat);
        ImGui::Text("Encoding Algorithm %s", fromImageFormatToString(usedFormat.format).c_str());
    if (webcam_->getType() == WebcamType::VIRTUAL_OUTPUT)
        {
            // Cast to V4L2LoopbackWriter to access V4L2-specific methods
            auto v4l2Writer = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
            if (v4l2Writer)
            {
                ImGui::Text("Chrominance Subsampling: %s",
                            subsamplingOptions[static_cast<int>(v4l2Writer->getChrominanceSubsampling())]);
                ImGui::Text("JPEG Quality: %d", v4l2Writer->getQuality());
            }
        }
    }

    if (webcam_->getType() == WebcamType::PHYSICAL_INPUT)
    {
        paintPhysicalInput();
    }
    else if (webcam_->getType() == WebcamType::VIRTUAL_OUTPUT)
    {
        paintVirtualOutput();
    }else if (webcam_->getType() == WebcamType::VIRTUAL_INPUT)
    {
        // TODO: finish this
        // paintVirtualInput();
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

    if (webcam_->getType() == WebcamType::PHYSICAL_INPUT)
    {
        ImGui::SameLine();
        ImGui::Separator();
        // Select/deselect device for processing
        bool selected = webcam_->isCurrentlySelected();
        if (ImGui::Checkbox("Selected Device?", &selected))
        {
            webcam_->setCurrentlySelected(selected);
        }
        
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When unchecked, device keeps running but frames are not processed.\nUncheck 'Active Device' to fully release the hardware.");
        }
    }
}

void PaintWebcam::paintPhysicalInput()
{
    const std::string& cameraKey = webcam_->getDevicePath();
    // Show available Formats
    if (ImGui::CollapsingHeader("Available Formats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto currentFormat = webcam_->getSelectedFormat();
        const CameraCapabilities capabilities = webcam_->getCapabilities();

        // Calculate current indices from webcam state
        int currentFormatIdx = -1;
        for (std::size_t i = 0; i < capabilities.formats.size(); i++)
        {
            if (currentFormat.pixelformat == capabilities.formats[i].pixelformat)
            {
                currentFormatIdx = static_cast<int>(i);
                break;
            }
        }

        const int currentSizeIdx = currentFormat.selectedFrameSize;
        const int currentFpsIdx = currentFormat.sizes[currentFormat.selectedFrameSize].selectedFPS;

        for (std::size_t fmtIdx = 0; fmtIdx < capabilities.formats.size(); fmtIdx++)
        {
            ImGuiTreeNodeFlags flags = 0;
            // Open treenode of the currently selected format
            if (currentFormatIdx == static_cast<int>(fmtIdx))
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            const auto& format = capabilities.formats[fmtIdx];
            if (ImGui::TreeNodeEx(("Format: " + format.description).c_str(), flags))
            {
                for (std::size_t sizeIdx = 0; sizeIdx < format.sizes.size(); sizeIdx++)
                {
                    flags = 0;
                    // Auto-expand currently active size for better visibility
                    if (currentFormatIdx == static_cast<int>(fmtIdx) && currentSizeIdx == static_cast<int>(sizeIdx))
                    {
                        flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    }
                    const auto& size = format.sizes[sizeIdx];
                    const std::string sizeText = std::to_string(size.width) + "x" + std::to_string(size.height);
                    if (ImGui::TreeNodeEx(sizeText.c_str(), flags))
                    {
                        for (std::size_t fpsIdx = 0; fpsIdx < size.fps.size(); fpsIdx++)
                        {
                            const auto& fps = size.getFps(fpsIdx);
                            // common::logError("User selected  fps amount %d in indx %d", fps, fps_idx);
                            const bool isCurrent = (currentFormatIdx == static_cast<int>(fmtIdx)
                                                    && currentSizeIdx == static_cast<int>(sizeIdx)
                                                    && currentFpsIdx == static_cast<int>(fpsIdx));

                            ImGui::PushID(fmtIdx * 5000 + sizeIdx * 500 + fpsIdx);
                            const std::string fpsText = std::to_string(fps) + "fps";
                            if (ImGui::Selectable(fpsText.c_str(), isCurrent))
                            {
                                if (!isCurrent) // Only apply if it's a
                                                // different selection
                                {
                                    common::logInfo("Applying format change: format %d, size %dx%d (index %d), fps %d",
                                                     fmtIdx, size.width, size.height, sizeIdx, fpsIdx);

                                    // Apply changes immediately for input camera
                                    auto inputCam = std::dynamic_pointer_cast<InputWebcam>(webcam_);
                                    if (inputCam)
                                    {
                                        if (!inputCam->reconfigureFormat(fmtIdx, sizeIdx, fpsIdx))
                                        {
                                            common::logError("Failed to apply camera format changes");
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
        if (currentFormatIdx >= 0 && currentSizeIdx >= 0 && currentFpsIdx >= 0)
        {
            ImGui::Separator();
            const auto& selFormat = capabilities.formats[currentFormatIdx];
            const auto& selSize = selFormat.sizes[currentSizeIdx];
            const auto& selFps = selSize.getFps(currentFpsIdx);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Current: %s - %ux%u (%d FPS)",
                               selFormat.description.c_str(), selSize.width, selSize.height, selFps);
        }
    }
}

void PaintWebcam::paintVirtualOutput()
{
    const std::string& cameraKey = webcam_->getDevicePath();

    // Initialize if not exists
    if (selected_subsampling_.find(cameraKey) == selected_subsampling_.end())
    {
        // Cast to V4L2LoopbackWriter to access V4L2-specific methods
        auto v4l2Writer = std::dynamic_pointer_cast<V4L2LoopbackWriter>(webcam_);
        if (v4l2Writer)
        {
            selected_subsampling_[cameraKey] = static_cast<int>(v4l2Writer->getChrominanceSubsampling());
        }
        else
        {
            selected_subsampling_[cameraKey] = 0; // Default value
        }
    }

    if (ImGui::CollapsingHeader("Available Options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int currentSubsampling = selected_subsampling_[cameraKey];

        // Show subsampling and quality options
        if (ImGui::Combo("Subsampling", &currentSubsampling, subsamplingOptions, IM_ARRAYSIZE(subsamplingOptions)))
        {
            selected_subsampling_[cameraKey] = currentSubsampling;
            common::logWarn("User changed subsampling to %s", subsamplingOptions[currentSubsampling]);
        }
        auto flagsForSliders = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput
                               | ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_NoSpeedTweaks;

        ImGui::SliderInt("Image Quality", &selected_quality_value_, 0, 100, "%d", flagsForSliders);

        const bool applyChangesDisabled = selected_subsampling_.find(cameraKey) == selected_subsampling_.end();

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
                auto newSubsampling = static_cast<TJSAMP>(selected_subsampling_[cameraKey]);
                if (!outputCam->reconfigure(newSubsampling, selected_quality_value_))
                {
                    selected_subsampling_[cameraKey] = -1;
                }
            }
        }

        if (applyChangesDisabled)
        {
            ImGui::EndDisabled();
        }
    }
}
