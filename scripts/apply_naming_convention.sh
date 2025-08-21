#!/bin/bash

# Script to systematically apply member variable naming convention
# This ensures compilation consistency by applying changes in the right order

set -e  # Exit on error

echo "=== Systematic Member Variable Naming Convention Application ==="

# Function to check if we can compile
check_compilation() {
    echo "Checking compilation..."
    cd build
    if make -j1 >/dev/null 2>&1; then
        echo "✓ Compilation successful"
        cd ..
        return 0
    else
        echo "✗ Compilation failed"
        cd ..
        return 1
    fi
}

# Function to apply clang-tidy to a group of files
apply_clang_tidy_group() {
    local description="$1"
    shift
    local files=("$@")
    
    echo "=== Processing: $description ==="
    
    for file in "${files[@]}"; do
        if [[ -f "$file" ]]; then
            echo "Processing: $file"
            # Apply clang-format first for consistent base
            clang-format -i --style=file "$file"
            # Apply clang-tidy fixes
            clang-tidy -p build --fix --fix-errors --config-file=.clang-tidy "$file" 2>/dev/null || echo "Warning: clang-tidy issues with $file"
            # Final format pass
            clang-format -i --style=file "$file"
        else
            echo "Warning: File not found: $file"
        fi
    done
    
    echo "Checking compilation after $description..."
    if ! check_compilation; then
        echo "ERROR: Compilation failed after processing $description"
        echo "You may need to manually fix issues in the processed files"
        return 1
    fi
    
    echo "✓ $description completed successfully"
    echo ""
}

# Make sure we're in the project root
cd "$(dirname "$0")/.."

# Initial compilation check
echo "Initial compilation check..."
if ! check_compilation; then
    echo "ERROR: Project doesn't compile in its current state"
    echo "Please fix compilation issues before running this script"
    exit 1
fi

# Step 1: Core data structures (Image, math utilities)
apply_clang_tidy_group "Core Data Structures" \
    "include/LinuxFace/math_utils.h" \
    "include/LinuxFace/common.h" \
    "include/LinuxFace/Image/image.h" \
    "include/LinuxFace/Image/pixel_conversion.h" \
    "include/LinuxFace/Image/tensor_padding.h" \
    "src/Image/image.cpp"

# Step 2: Base classes and interfaces
apply_clang_tidy_group "Base Classes and Interfaces" \
    "include/LinuxFace/detectors.h" \
    "include/LinuxFace/codec.h" \
    "include/LinuxFace/webcam.h" \
    "src/Webcam/webcam.cpp"

# Step 3: ONNX detector base class
apply_clang_tidy_group "ONNX Base Classes" \
    "include/LinuxFace/onnx/onnxDetector.h" \
    "src/onnx/onnxDetector.cpp"

# Step 4: Specific ONNX implementations (one by one to catch issues early)
onnx_files=(
    "include/LinuxFace/onnx/scrfd.h,src/onnx/scrfd.cpp"
    "include/LinuxFace/onnx/pfld.h,src/onnx/pfld.cpp"
    "include/LinuxFace/onnx/MODNet.h,src/onnx/MODNet.cpp"
    "include/LinuxFace/onnx/fsanet.h,src/onnx/fsanet.cpp"
    "include/LinuxFace/onnx/inswapper.h,src/onnx/inswapper.cpp"
    "include/LinuxFace/onnx/mediaPipe_FaceLandmarks.h,src/onnx/mediaPipe_FaceLandmarks.cpp"
    "include/LinuxFace/onnx/rvm.h,src/onnx/rvm.cpp"
)

for file_pair in "${onnx_files[@]}"; do
    IFS=',' read -r header source <<< "$file_pair"
    model_name=$(basename "$header" .h)
    apply_clang_tidy_group "ONNX Model: $model_name" "$header" "$source"
done

# Step 5: Remaining components
apply_clang_tidy_group "Image Processing Components" \
    "include/LinuxFace/Image/gif.h" \
    "include/LinuxFace/Image/image_utils.h" \
    "include/LinuxFace/Image/mediaManager.h" \
    "include/LinuxFace/Image/text_draw.h" \
    "include/LinuxFace/Image/text_renderer.h" \
    "src/Image/gif.cpp" \
    "src/Image/mediaManager.cpp" \
    "src/Image/text_renderer.cpp"

apply_clang_tidy_group "UI Components" \
    "include/LinuxFace/UI/layerManager.h" \
    "include/LinuxFace/UI/mediaBrowserUi.h" \
    "include/LinuxFace/UI/paintWebcam.h" \
    "include/LinuxFace/ui.h" \
    "src/UI/layerManager.cpp" \
    "src/UI/mediaBrowserUi.cpp" \
    "src/UI/paintWebcam.cpp" \
    "src/ui.cpp"

# Step 6: Application and main components
apply_clang_tidy_group "Application Core" \
    "include/LinuxFace/application.h" \
    "include/LinuxFace/cameraManager.h" \
    "include/LinuxFace/window.h" \
    "include/LinuxFace/profiler.h" \
    "src/application.cpp" \
    "src/cameraManager.cpp" \
    "src/window.cpp" \
    "src/profiler.cpp" \
    "src/main.cpp"

echo "=== All naming convention changes applied successfully! ==="
echo "Final compilation check..."
if check_compilation; then
    echo "✓ Everything compiled successfully!"
    echo "You can now commit these changes."
else
    echo "✗ Final compilation failed"
    echo "Please check the output above for any remaining issues"
    exit 1
fi
