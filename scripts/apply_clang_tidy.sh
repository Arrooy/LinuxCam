#!/bin/bash

# Script to apply clang-tidy formatting and fixes
# Usage: 
#   ./apply_clang_tidy.sh                    # Process all files
#   ./apply_clang_tidy.sh <file>             # Process single file
#   ./apply_clang_tidy.sh --check <file>     # Check single file without applying fixes

# Get number of CPU cores for parallel processing
CORES=$(nproc)
CORES="1"
# Function to process a single file with clang-tidy
process_file() {
    local file="$1"
    local check_only="$2"
    
    echo "Processing: $file"
    
    if [[ "$check_only" == "true" ]]; then
        echo "Checking (dry-run mode): $file"
        clang-tidy -p build --config-file=.clang-tidy "$file" 2>/dev/null || echo "Warning: Failed to check $file"
    else
        echo "Applying fixes to: $file"
        # First apply clang-format for consistent base formatting
        clang-format -i --style=file "$file"
        # Then apply clang-tidy fixes
        clang-tidy -p build --fix --fix-errors --config-file=.clang-tidy "$file" 2>/dev/null || echo "Warning: Failed to process $file"
        # Final formatting pass
        clang-format -i --style=file "$file"
    fi
}

# Export the function so it can be used by xargs
export -f process_file

# Parse command line arguments
if [[ $# -eq 1 && "$1" != "--help" && "$1" != "-h" ]]; then
    # Single file mode
    TARGET_FILE="$1"
    if [[ ! -f "$TARGET_FILE" ]]; then
        echo "Error: File '$TARGET_FILE' does not exist"
        exit 1
    fi
    echo "Single file mode: Processing $TARGET_FILE"
    process_file "$TARGET_FILE" "false"
    echo "Single file processing completed!"
    exit 0
elif [[ $# -eq 2 && "$1" == "--check" ]]; then
    # Check mode for single file
    TARGET_FILE="$2"
    if [[ ! -f "$TARGET_FILE" ]]; then
        echo "Error: File '$TARGET_FILE' does not exist"
        exit 1
    fi
    echo "Check mode: Analyzing $TARGET_FILE"
    process_file "$TARGET_FILE" "true"
    echo "Check completed!"
    exit 0
elif [[ $# -gt 0 ]]; then
    echo "Usage:"
    echo "  $0                    # Process all files"
    echo "  $0 <file>             # Process single file"
    echo "  $0 --check <file>     # Check single file without applying fixes"
    exit 1
fi

echo "Using $CORES parallel processes"
echo "Starting comprehensive clang-tidy application..."

# First, apply clang-format to ensure consistent base formatting
echo "Step 1: Applying clang-format..."
find src include \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | xargs -0 -P "$CORES" -I {} clang-format -i --style=file {}

# Apply clang-tidy to header files first (they're usually safer)
echo "Step 2: Applying clang-tidy to header files in parallel..."
find include \( -name "*.h" -o -name "*.hpp" \) -print0 | xargs -0 -P "$CORES" -I {} bash -c 'process_file "$@"' _ {}

# Apply clang-tidy to source files (more careful approach)
echo "Step 3: Applying clang-tidy to source files in parallel..."
find src -name "*.cpp" -print0 | xargs -0 -P "$CORES" -I {} bash -c 'process_file "$@"' _ {}

echo "Step 4: Final formatting pass..."
find src include \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | xargs -0 -P "$CORES" -I {} clang-format -i --style=file {}

echo "Clang-tidy application completed!"
