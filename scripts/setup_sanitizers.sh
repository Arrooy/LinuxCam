#!/bin/bash
# Sanitizer Environment Setup Script
# Source this file before running sanitizer-enabled builds:
# source scripts/setup_sanitizers.sh

echo "Setting up sanitizer environment variables..."

# AddressSanitizer options
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:check_initialization_order=1:detect_stack_use_after_return=1:strict_init_order=1:detect_invalid_pointer_pairs=2"

# UndefinedBehaviorSanitizer options  
export UBSAN_OPTIONS="print_stacktrace=1:abort_on_error=1:halt_on_error=1"

# LeakSanitizer options
export LSAN_OPTIONS="print_stats=1:report_objects=1"

# ThreadSanitizer options (use when ENABLE_THREAD_SANITIZER=ON)
export TSAN_OPTIONS="abort_on_error=1:halt_on_error=1:report_bugs=1:history_size=7"

# General sanitizer options
export MSAN_OPTIONS="abort_on_error=1:print_stats=1"

echo "Environment variables set:"
echo "  ASAN_OPTIONS=$ASAN_OPTIONS"
echo "  UBSAN_OPTIONS=$UBSAN_OPTIONS" 
echo "  LSAN_OPTIONS=$LSAN_OPTIONS"
echo "  TSAN_OPTIONS=$TSAN_OPTIONS"
echo "  MSAN_OPTIONS=$MSAN_OPTIONS"
echo ""
echo "Now you can build with sanitizers enabled:"
echo "  mkdir -p build && cd build"
echo "  cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON .."
echo "  make -j\$(nproc)"
echo ""
echo "Or with ThreadSanitizer (mutually exclusive):"
echo "  cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_THREAD_SANITIZER=ON .."
echo ""
echo "Then run your application normally. Sanitizer output will be printed to stderr."
