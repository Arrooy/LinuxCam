#!/bin/bash
# Docker entrypoint script for LinuxCam
# This script performs pre-flight checks and starts the application

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print functions
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if NVIDIA GPU is available
check_gpu() {
    print_info "Checking GPU availability..."
    if command -v nvidia-smi &> /dev/null; then
        if nvidia-smi &> /dev/null; then
            print_info "✓ NVIDIA GPU detected"
            nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader
            return 0
        else
            print_warning "✗ nvidia-smi command failed"
            print_warning "GPU acceleration will not be available"
            return 1
        fi
    else
        print_warning "✗ nvidia-smi not found"
        print_warning "GPU acceleration will not be available"
        return 1
    fi
}

# Check if CUDA libraries are accessible
check_cuda() {
    print_info "Checking CUDA libraries..."
    if [ -d "$CUDA_HOME" ]; then
        print_info "✓ CUDA_HOME: $CUDA_HOME"
        if [ -f "$CUDA_HOME/lib64/libcudart.so" ]; then
            print_info "✓ CUDA runtime library found"
            return 0
        else
            print_warning "✗ CUDA runtime library not found"
            return 1
        fi
    else
        print_warning "✗ CUDA_HOME not set or directory doesn't exist"
        return 1
    fi
}

# Check camera devices
check_cameras() {
    print_info "Checking camera devices..."
    
    # Check input camera
    if [ -c "/dev/video0" ]; then
        print_info "✓ Input camera /dev/video0 available"
    else
        print_warning "✗ Input camera /dev/video0 not found"
        print_warning "Camera input may not work"
    fi
    
    # Check virtual output camera
    if [ -c "/dev/video8" ]; then
        print_info "✓ Virtual camera /dev/video8 available"
    else
        print_warning "✗ Virtual camera /dev/video8 not found"
        print_warning "Virtual camera output will not work"
        print_warning "Run setup script on host: scripts/setup_v4l2loopback.sh"
    fi
}

# Check if ONNX models are present
check_models() {
    print_info "Checking ONNX models..."
    if [ -d "/app/models" ] && [ "$(ls -A /app/models 2>/dev/null)" ]; then
        MODEL_COUNT=$(ls -1 /app/models/*.onnx 2>/dev/null | wc -l)
        if [ "$MODEL_COUNT" -gt 0 ]; then
            print_info "✓ Found $MODEL_COUNT ONNX model(s)"
            return 0
        fi
    fi
    print_warning "✗ No ONNX models found in /app/models"
    print_warning "Application may not function properly"
    print_warning "Mount models directory or download them separately"
    return 1
}

# Check configuration file
check_config() {
    print_info "Checking configuration..."
    if [ -f "/app/config.yaml" ]; then
        print_info "✓ Configuration file found"
        return 0
    else
        print_error "✗ Configuration file not found"
        print_error "Please mount config.yaml to /app/config.yaml"
        return 1
    fi
}

# Check web assets
check_web_assets() {
    print_info "Checking web assets..."
    if [ -d "/app/www" ] && [ "$(ls -A /app/www 2>/dev/null)" ]; then
        print_info "✓ Web assets found"
        return 0
    else
        print_warning "✗ Web assets directory empty or missing"
        return 1
    fi
}

# Check SSL certificates
check_ssl() {
    print_info "Checking SSL certificates..."
    if [ -f "/app/ssl/cert.pem" ] && [ -f "/app/ssl/key.pem" ]; then
        print_info "✓ SSL certificates found"
        return 0
    else
        print_warning "✗ SSL certificates not found"
        print_warning "HTTPS may not work properly"
        return 1
    fi
}

# Check permissions
check_permissions() {
    print_info "Checking file permissions..."
    
    # Check if uploads directory is writable
    if [ -w "/app/uploads" ]; then
        print_info "✓ Uploads directory is writable"
    else
        print_warning "✗ Uploads directory is not writable"
    fi
    
    # Check if executable exists and is executable
    if [ -x "/app/LinuxFace" ]; then
        print_info "✓ LinuxFace executable is ready"
    else
        print_error "✗ LinuxFace executable not found or not executable"
        return 1
    fi
}

# Display system information
show_system_info() {
    print_info "System Information:"
    echo "  Hostname: $(hostname)"
    echo "  User: $(whoami)"
    echo "  Working Directory: $(pwd)"
    echo "  CPU Cores: $(nproc)"
    echo "  Total Memory: $(free -h | awk '/^Mem:/ {print $2}')"
}

# Main pre-flight checks
main() {
    print_info "================================================"
    print_info "LinuxCam Docker Container - Starting"
    print_info "================================================"
    echo ""
    
    # Run all checks
    show_system_info
    echo ""
    
    check_gpu || true
    check_cuda || true
    echo ""
    
    check_cameras || true
    echo ""
    
    check_models || true
    check_config || exit 1
    check_web_assets || true
    check_ssl || true
    echo ""
    
    check_permissions || exit 1
    echo ""
    
    print_info "================================================"
    print_info "Pre-flight checks completed"
    print_info "================================================"
    echo ""
    
    # Handle signals properly
    trap 'print_info "Received signal, shutting down..."; exit 0' SIGTERM SIGINT
    
    # Start the application
    print_info "Starting LinuxFace application..."
    print_info "Command: $@"
    echo ""
    
    # Execute the application with all arguments
    exec "$@"
}

# Run main function
main "$@"
