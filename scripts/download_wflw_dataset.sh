#!/bin/bash

# WFLW Dataset Download Script for CI/Testing
# Downloads and extracts WFLW dataset for facial landmark testing

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WFLW_DIR="$PROJECT_ROOT/WFLW"

# WFLW dataset URLs (official academic dataset)
WFLW_IMAGES_URL="https://drive.usercontent.google.com/download?id=1hzBd48JIdWTJSsATBEB_eFVvPL1bx6UC&export=download"
WFLW_ANNOTATIONS_URL="https://wywu.github.io/projects/LAB/support/WFLW_annotations.tar.gz"

# Create WFLW directory
mkdir -p "$WFLW_DIR"
cd "$WFLW_DIR"

echo "=== WFLW Dataset Download ==="
echo "Target directory: $WFLW_DIR"

# Function to download with gdown (Google Drive helper)
download_with_gdown() {
    local url="$1"
    local filename="$2"
    # Use gdown only for Google Drive URLs, otherwise wget
    if [[ "$url" == *"drive.google.com"* || "$url" == *"drive.usercontent.google.com"* ]]; then
        if command -v gdown &> /dev/null; then
            echo "Using gdown to download $filename..."
            gdown "$url" -O "$filename"
        elif command -v pipx &> /dev/null && pipx list | grep -q gdown; then
            echo "Using pipx gdown to download $filename..."
            pipx run gdown "$url" -O "$filename"
        else
            echo "gdown not available, using wget..."
            wget -O "$filename" "$url"
        fi
    else
        echo "Using wget to download $filename..."
        wget -O "$filename" "$url"
    fi
}

# Function to download file if it doesn't exist
download_if_missing() {
    local url="$1"
    local filename="$2"
    local extract_dir="$3"
    
    if [ ! -f "$filename" ]; then
        echo "Downloading $filename..."
        download_with_gdown "$url" "$filename"
    else
        echo "$filename already exists, skipping download"
    fi
    
    # Extract if directory doesn't exist
    if [ ! -d "$extract_dir" ]; then
        echo "Extracting $filename to $extract_dir..."
        tar -xzf "$filename"
        echo "Extraction completed"
    else
        echo "$extract_dir already exists, skipping extraction"
    fi
}

# Install gdown if not available (for Google Drive downloads)
if ! command -v gdown &> /dev/null; then
    echo "gdown not found. Installing with pipx..."
    if ! command -v pipx &> /dev/null; then
        echo "pipx not found. Installing pipx..."
        sudo apt-get update && sudo apt-get install -y pipx python3-pip
    fi
    pipx install gdown
fi

# Download dataset files
echo "Downloading WFLW Images..."
download_if_missing "$WFLW_IMAGES_URL" "WFLW_images.tar.gz" "WFLW_images"

echo "Downloading WFLW Annotations..."
download_if_missing "$WFLW_ANNOTATIONS_URL" "WFLW_annotations.tar.gz" "WFLW_annotations"

# Verify dataset structure
echo "=== Dataset Verification ==="
if [ -d "WFLW_images" ] && [ -d "WFLW_annotations" ]; then
    echo "✅ WFLW dataset downloaded successfully"
    
    # Count files for verification
    IMAGE_COUNT=$(find WFLW_images -name "*.jpg" -o -name "*.png" | wc -l)
    ANNOTATION_COUNT=$(find WFLW_annotations -name "*.txt" | wc -l)
    
    echo "📊 Dataset Statistics:"
    echo "  - Images found: $IMAGE_COUNT"
    echo "  - Annotation files: $ANNOTATION_COUNT"
    
    # Check for test annotations specifically
    TEST_ANNOTATIONS="WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt"
    if [ -f "$TEST_ANNOTATIONS" ]; then
        TEST_SAMPLE_COUNT=$(wc -l < "$TEST_ANNOTATIONS")
        echo "  - Test samples: $TEST_SAMPLE_COUNT"
        echo "✅ Test annotations file found: $TEST_ANNOTATIONS"
    else
        echo "⚠️  Warning: Test annotations file not found at $TEST_ANNOTATIONS"
        echo "Available annotation files:"
        find WFLW_annotations -name "*.txt" | head -5
    fi
    
    echo "✅ WFLW dataset ready for testing"
else
    echo "❌ Error: Dataset extraction failed"
    echo "Directory contents:"
    ls -la
    exit 1
fi

echo "=== Download Complete ==="
