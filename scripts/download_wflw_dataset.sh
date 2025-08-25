#!/bin/bash

# WFLW Dataset Download Script for CI/Testing
# Downloads and extracts WFLW dataset for facial landmark testing

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WFLW_BASE_DIR="$PROJECT_ROOT/WFLW"

# WFLW dataset Google Drive IDs and file information
IMAGES_ID="1hzBd48JIdWTJSsATBEB_eFVvPL1bx6UC"
ANNOTATIONS_ID="1r_ciJ1M0BSRTwndIBt42GlPFRv6CvvEP"

# File and directory names
IMAGES_FILE="WFLW_images.tar.gz"
ANNOTATIONS_FILE="WFLW_annotations.tar.gz"
IMAGES_DIR="WFLW_images"
ANNOTATIONS_DIR="WFLW_annotations"

echo "=== WFLW Dataset Download ==="
echo "Target directory: $WFLW_BASE_DIR"

# Function to download file with retry logic
download_with_retry() {
    local url="$1"
    local filename="$2"
    local max_attempts=3
    local attempt=1
    
    while [ $attempt -le $max_attempts ]; do
        echo "Download attempt $attempt of $max_attempts for $filename..."
        
        if wget -O "$filename" "$url" --timeout=30 --tries=3; then
            echo "Download successful: $filename"
            return 0
        fi
        
        echo "Download attempt $attempt failed"
        attempt=$((attempt + 1))
        sleep 5
    done
    
    echo "ERROR: Failed to download $filename after $max_attempts attempts"
    return 1
}

# Function to download from Google Drive using gdown
download_gdrive_file() {
    local gdrive_id="$1"
    local filename="$2"
    local max_attempts=3
    local attempt=1
    
    while [ $attempt -le $max_attempts ]; do
        echo "Google Drive download attempt $attempt of $max_attempts for $filename..."
        
        if command -v gdown &> /dev/null; then
            if gdown "$gdrive_id" -O "$filename" --fuzzy; then
                echo "Google Drive download successful: $filename"
                return 0
            fi
        else
            echo "ERROR: gdown not available for Google Drive download"
            return 1
        fi
        
        echo "Google Drive download attempt $attempt failed"
        attempt=$((attempt + 1))
        sleep 5
    done
    
    echo "ERROR: Failed to download $filename from Google Drive after $max_attempts attempts"
    return 1
}

# Function to download file if it doesn't exist
download_if_missing() {
    local filename="$1"
    local extract_dir="$2"
    local download_type="$3"
    local source_param="$4"
    
    if [ ! -f "$filename" ]; then
        echo "Downloading $filename..."
        
        if [ "$download_type" = "gdrive" ]; then
            if ! download_gdrive_file "$source_param" "$filename"; then
                echo "ERROR: Download failed for $filename"
                return 1
            fi
        elif [ "$download_type" = "wget" ]; then
            if ! download_with_retry "$source_param" "$filename"; then
                echo "ERROR: Download failed for $filename"
                return 1
            fi
        else
            echo "ERROR: Unknown download type: $download_type"
            return 1
        fi
    else
        echo "$filename already exists, skipping download"
    fi
    
    # Verify file was downloaded and has content
    if [ ! -f "$filename" ] || [ ! -s "$filename" ]; then
        echo "ERROR: Downloaded file $filename is missing or empty"
        return 1
    fi
    
    # Extract if directory doesn't exist
    if [ ! -d "$extract_dir" ]; then
        echo "Extracting $filename to $extract_dir..."
        if ! tar -xzf "$filename"; then
            echo "ERROR: Failed to extract $filename"
            return 1
        fi
        echo "Extraction completed successfully"
    else
        echo "$extract_dir already exists, skipping extraction"
    fi
    
    return 0
}

# Main download logic
main() {
    echo "Starting WFLW dataset download process..."
    
    # Create WFLW directory if it doesn't exist
    mkdir -p "$WFLW_BASE_DIR"
    cd "$WFLW_BASE_DIR" || {
        echo "ERROR: Cannot access WFLW directory"
        exit 1
    }
    
    # Download and extract WFLW annotations
    if ! download_if_missing "$ANNOTATIONS_FILE" "$ANNOTATIONS_DIR" "gdrive" "$ANNOTATIONS_ID"; then
        echo "ERROR: Failed to download/extract WFLW annotations"
        exit 1
    fi
    
    # Download and extract WFLW images
    if ! download_if_missing "$IMAGES_FILE" "$IMAGES_DIR" "gdrive" "$IMAGES_ID"; then
        echo "ERROR: Failed to download/extract WFLW images"
        exit 1
    fi
    
    # Verify dataset integrity
    if ! verify_dataset; then
        echo "ERROR: Dataset verification failed"
        exit 1
    fi
    
    echo "WFLW dataset download and setup completed successfully!"
    return 0
}

# Verify dataset structure after download
verify_dataset() {
    echo "=== Dataset Verification ==="
    if [ -d "WFLW_images" ] && [ -d "WFLW_annotations" ]; then
        echo "WFLW dataset downloaded successfully"
        
        # Count files for verification
        IMAGE_COUNT=$(find WFLW_images -name "*.jpg" -o -name "*.png" 2>/dev/null | wc -l)
        ANNOTATION_COUNT=$(find WFLW_annotations -name "*.txt" 2>/dev/null | wc -l)
        
        echo "Dataset Statistics:"
        echo "  - Images found: $IMAGE_COUNT"
        echo "  - Annotation files: $ANNOTATION_COUNT"
        
        # Check for test annotations specifically
        TEST_ANNOTATIONS="WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt"
        if [ -f "$TEST_ANNOTATIONS" ]; then
            TEST_SAMPLE_COUNT=$(wc -l < "$TEST_ANNOTATIONS" 2>/dev/null || echo "0")
            echo "  - Test samples: $TEST_SAMPLE_COUNT"
            echo "Test annotations file found: $TEST_ANNOTATIONS"
        else
            echo "WARNING: Test annotations file not found at $TEST_ANNOTATIONS"
            echo "Available annotation files:"
            find WFLW_annotations -name "*.txt" 2>/dev/null | head -5 || echo "No annotation files found"
            return 1
        fi
        
        echo "WFLW dataset ready for testing"
        return 0
    else
        echo "ERROR: Dataset extraction failed"
        echo "Directory contents:"
        ls -la
        return 1
    fi
}

# Run the main download process
echo "Starting WFLW dataset download..."

if ! command -v gdown &> /dev/null; then
    echo "ERROR: gdown is required but not installed"
    echo "Please install gdown: pip install gdown"
    exit 1
fi

if main && verify_dataset; then
    echo "=== Download Complete Successfully ==="
    exit 0
else
    echo "=== Download Failed ==="
    exit 1
fi
