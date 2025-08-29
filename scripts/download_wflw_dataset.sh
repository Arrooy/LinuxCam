#!/bin/bash

# WFLW Dataset Download Script for CI/Testing
# Downloads and extracts WFLW dataset for facial landmark testing

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WFLW_BASE_DIR="$PROJECT_ROOT/WFLW"

# WFLW dataset Google Drive IDs and file information
IMAGES_ID="1hzBd48JIdWTJSsATBEB_eFVvPL1bx6UC"
ANNOTATIONS_URL="https://wywu.github.io/projects/LAB/support/WFLW_annotations.tar.gz"

# File and directory names
IMAGES_FILE="WFLW_images.tar.gz"
ANNOTATIONS_FILE="WFLW_annotations.tar.gz"
IMAGES_DIR="WFLW_images"
ANNOTATIONS_DIR="WFLW_annotations"

echo "=== WFLW Dataset Download ==="
echo "Target directory: $WFLW_BASE_DIR"

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

# Function to download using wget with retry logic
download_with_retry() {
    local url="$1"
    local filename="$2"
    local max_attempts=3
    local attempt=1
    
    while [ $attempt -le $max_attempts ]; do
        echo "Wget download attempt $attempt of $max_attempts for $filename..."
        
        if command -v wget &> /dev/null; then
            if wget -O "$filename" "$url"; then
                echo "Wget download successful: $filename"
                return 0
            fi
        else
            echo "ERROR: wget not available for download"
            return 1
        fi
        
        echo "Wget download attempt $attempt failed"
        attempt=$((attempt + 1))
        sleep 5
    done
    
    echo "ERROR: Failed to download $filename from $url after $max_attempts attempts"
    return 1
}

# Function to detect and extract archive files
extract_archive() {
    local filename="$1"
    local extract_dir="$2"
    
    echo "Detecting archive type for $filename..."
    
    # Use the 'file' command to detect the actual file type
    local file_type
    file_type=$(file "$filename" 2>/dev/null)
    
    if [[ "$file_type" == *"Zip archive"* ]]; then
        echo "Detected ZIP archive, extracting with unzip..."
        if command -v unzip &> /dev/null; then
            if unzip -q "$filename"; then
                echo "ZIP extraction successful"
                return 0
            else
                echo "ERROR: ZIP extraction failed"
                return 1
            fi
        else
            echo "ERROR: unzip command not available. Please install unzip: sudo apt-get install unzip"
            return 1
        fi
    elif [[ "$file_type" == *"gzip compressed"* ]] || [[ "$file_type" == *"tar.gz"* ]]; then
        echo "Detected gzip-compressed tar archive, extracting with tar..."
        if tar -xzf "$filename"; then
            echo "Tar.gz extraction successful"
            return 0
        else
            echo "ERROR: Tar.gz extraction failed"
            return 1
        fi
    else
        echo "ERROR: Unknown or unsupported archive format:"
        echo "File type: $file_type"
        return 1
    fi
}

# Function to download file if it doesn't exist
download_if_missing() {
    local filename="$1"
    local extract_dir="$2"
    local download_type="$3"
    local source_param="$4"
    local force_redownload="${5:-false}"
    
    # Remove existing file and extracted directory if force redownload is requested
    if [ "$force_redownload" = "true" ]; then
        if [ -f "$filename" ]; then
            echo "Force redownload requested, removing existing $filename..."
            rm -f "$filename"
        fi
        if [ -d "$extract_dir" ]; then
            echo "Force redownload requested, removing existing $extract_dir..."
            rm -rf "$extract_dir"
        fi
    fi
    
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
        
        # Use the smart extraction function
        if ! extract_archive "$filename" "$extract_dir"; then
            echo "Removing corrupted file and will re-download on next run..."
            rm -f "$filename"
            return 1
        fi
        
        # Verify extraction was successful
        if [ ! -d "$extract_dir" ]; then
            echo "ERROR: Extraction completed but $extract_dir was not created"
            echo "Checking for alternative directory names..."
            # Sometimes archives extract to different directory names
            find . -maxdepth 1 -type d -name "*annotation*" -o -name "*wflw*" -o -name "*test*" -o -name "*image*" 2>/dev/null | head -1 | while read dir; do
                if [ -n "$dir" ] && [ "$dir" != "." ]; then
                    echo "Found extracted directory: $dir"
                    mv "$dir" "$extract_dir" 2>/dev/null || true
                fi
            done
        fi
        
        if [ -d "$extract_dir" ]; then
            echo "Extraction completed successfully"
        else
            echo "ERROR: Could not find expected directory $extract_dir after extraction"
            echo "Current directory contents:"
            ls -la
            echo "Removing corrupted file and will re-download on next run..."
            rm -f "$filename"
            return 1
        fi
    else
        echo "$extract_dir already exists, skipping extraction"
    fi
    
    return 0
}

# Main download logic
main() {
    local force_redownload=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --force|-f)
                force_redownload=true
                echo "Force redownload enabled - will re-download all files"
                shift
                ;;
            --help|-h)
                echo "Usage: $0 [OPTIONS]"
                echo "Options:"
                echo "  --force, -f    Force re-download of all files (removes existing files first)"
                echo "  --help, -h     Show this help message"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
    
    echo "Starting WFLW dataset download process..."
    
    # Create WFLW directory if it doesn't exist
    mkdir -p "$WFLW_BASE_DIR"
    cd "$WFLW_BASE_DIR" || {
        echo "ERROR: Cannot access WFLW directory"
        exit 1
    }
    
    # Download and extract WFLW annotations
    if ! download_if_missing "$ANNOTATIONS_FILE" "$ANNOTATIONS_DIR" "wget" "$ANNOTATIONS_URL" "$force_redownload"; then
        echo "ERROR: Failed to download/extract WFLW annotations"
        exit 1
    fi
    
    # Download and extract WFLW images
    if ! download_if_missing "$IMAGES_FILE" "$IMAGES_DIR" "gdrive" "$IMAGES_ID" "$force_redownload"; then
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
    
    # Check what we actually have
    local has_images=false
    local has_annotations=false
    
    if [ -d "WFLW_images" ]; then
        has_images=true
        IMAGE_COUNT=$(find WFLW_images -name "*.jpg" -o -name "*.png" 2>/dev/null | wc -l)
    fi
    
    if [ -d "WFLW_annotations" ]; then
        has_annotations=true
        ANNOTATION_COUNT=$(find WFLW_annotations -name "*.txt" -o -name "*.pts" -o -name "*.mat" 2>/dev/null | wc -l)
    fi
    
    echo "Dataset Summary:"
    if [ "$has_images" = "true" ]; then
        echo "  - Images directory: WFLW_images ($IMAGE_COUNT files)"
    fi
    if [ "$has_annotations" = "true" ]; then
        echo "  - Annotations directory: WFLW_annotations ($ANNOTATION_COUNT annotation files)"
    fi
    
    # Check if we have the expected test.data structure
    if [ -d "test.data" ]; then
        local test_data_count
        test_data_count=$(find test.data -name "*.jpg" -o -name "*.png" 2>/dev/null | wc -l)
        echo "  - Additional test data: test.data ($test_data_count files)"
    fi
    
    # Determine success based on what we have
    if [ "$has_images" = "true" ] && [ "$IMAGE_COUNT" -gt 0 ]; then
        echo "✅ Images successfully downloaded and extracted"
        
        if [ "$has_annotations" = "true" ] && [ "$ANNOTATION_COUNT" -gt 0 ]; then
            echo "✅ Annotation files found"
            echo "WFLW dataset ready for use"
            return 0
        else
            echo "⚠️  WARNING: No annotation files found in WFLW_annotations directory"
            echo "   This may be expected if the dataset structure has changed"
            echo "   Images are available for use, but landmark annotations may need to be sourced separately"
            return 0
        fi
    else
        echo "❌ ERROR: No images found - dataset download may have failed"
        echo "Directory contents:"
        ls -la
        return 1
    fi
}

# Run the main download process
echo "Starting WFLW dataset download..."

if ! command -v wget &> /dev/null; then
    echo "ERROR: wget is required but not installed"
    echo "Please install wget: sudo apt-get install wget"
    exit 1
fi

if ! command -v gdown &> /dev/null; then
    echo "ERROR: gdown is required but not installed"
    echo "Please install gdown: pip install gdown"
    exit 1
fi

if ! command -v unzip &> /dev/null; then
    echo "ERROR: unzip is required but not installed"
    echo "Please install unzip: sudo apt-get install unzip"
    exit 1
fi

if main "$@" && verify_dataset; then
    echo "=== Download Complete Successfully ==="
    exit 0
else
    echo "=== Download Failed ==="
    exit 1
fi
