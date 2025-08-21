#!/bin/bash

# Coverage report generation script for LinuxFace project
# Generates LCOV format for VS Code Coverage Gutters

echo "Generating coverage report for LinuxFace project..."

# Configuration
BUILD_DIR="build"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: Build directory '$BUILD_DIR' not found!"
    echo "Please run the build first with: cmake --build $BUILD_DIR"
    exit 1
fi

# Verify coverage data
GCDA_COUNT=$(find $BUILD_DIR -name "*.gcda" | wc -l)
GCNO_COUNT=$(find $BUILD_DIR -name "*.gcno" | wc -l)

if [ $GCDA_COUNT -eq 0 ] || [ $GCNO_COUNT -eq 0 ]; then
    echo "ERROR: No coverage data found. Run tests first: ./build/linuxface_tests"
    exit 1
fi

echo "Found $GCDA_COUNT .gcda files and $GCNO_COUNT .gcno files"

echo ""
echo "Generating LCOV coverage report..."

# Try simple gcovr approach which worked before
if gcovr -r . --filter src/ --exclude tests/ --exclude external/ --exclude build/_deps/ --lcov -o build/coverage.lcov 2>/dev/null; then
    echo "Coverage reports generated:"
    echo "  LCOV: build/coverage.lcov"
    echo "  LCOV: lcov.info (for VS Code Coverage Gutters)"
    echo ""
    echo "To view coverage in VS Code:"
    echo "  1. Open any source file"
    echo "  2. Ctrl+Shift+P -> 'Coverage Gutters: Display Coverage'"
else
    echo "LCOV generation failed, but HTML coverage is available at: build/coverage.html"
    echo "You can still view coverage reports in your browser."
fi
