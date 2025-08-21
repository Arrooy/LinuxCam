#!/bin/bash

# Get number of CPU cores for parallel processing
CORES=$(nproc)
echo "Using $CORES parallel processes"

# Function to process a single file with clang-tidy
process_file() {
    local file="$1"
    echo "Processing: $file"
    clang-tidy -p build --fix --fix-errors --config-file=.clang-tidy "$file" 2>/dev/null || echo "Warning: Failed to process $file"
}

# Export the function so it can be used by xargs
export -f process_file

# Apply clang-tidy to all source and header files systematically
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
