# LinuxFace 🎭

> A high-performance real-time facial processing and virtual webcam application for Linux

[![CI Workflow](https://github.com/Arrooy/LinuxCam/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/ci.yml)
[![Code Formatting](https://github.com/Arrooy/LinuxCam/actions/workflows/code-formatting.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/code-formatting.yml)
[![Clang-Tidy Analysis](https://github.com/Arrooy/LinuxCam/actions/workflows/clang-tidy.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/clang-tidy.yml)
[![codecov](https://codecov.io/github/Arrooy/LinuxCam/branch/develop/graph/badge.svg?token=NWVE3AC940)](https://codecov.io/github/Arrooy/LinuxCam)
[![Security Analysis - Flawfinder](https://github.com/Arrooy/LinuxCam/actions/workflows/flawfinder.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/flawfinder.yml)

## ✨ Features

- 🎥 **Real-time Face Processing**: Computer Vision playground
- 📹 **Virtual Webcam Integration**: Seamless virtual camera output via v4l2loopback
- 🚀 **GPU Acceleration**: CUDA 12 support for high-performance processing
- 🎨 **Multiple Processing Modes**: Face swapping, enhancement, and analysis
- 📊 **Testing**: Automated CI with coverage reporting

## 📋 Table of Contents

- [Docker Installation (Recommended)](#-docker-installation-recommended)
- [Native Installation](#️-native-installation)
- [Quick Start](#-quick-start)
- [Usage](#-usage)
- [Development](#-development)
- [Contributing](#-contributing)
- [License](#-license)

## 🐳 Docker Installation (Recommended)

The easiest way to run LinuxCam is using Docker with GPU support. This method handles all dependencies automatically and provides a consistent environment.

### Prerequisites

**System Requirements:**
- Linux host with NVIDIA GPU
- Docker Engine 20.10+
- Docker Compose v2.0+
- NVIDIA Container Toolkit

**Installation Steps:**

1. **Install Docker and Docker Compose**
   ```bash
   # Install Docker
   curl -fsSL https://get.docker.com -o get-docker.sh
   sudo sh get-docker.sh
   sudo usermod -aG docker $USER
   
   # Install Docker Compose (if not included)
   sudo apt-get update
   sudo apt-get install docker-compose-plugin
   ```

2. **Install NVIDIA Container Toolkit**
   ```bash
   # Add NVIDIA package repositories
   distribution=$(. /etc/os-release;echo $ID$VERSION_ID)
   curl -s -L https://nvidia.github.io/nvidia-docker/gpgkey | sudo apt-key add -
   curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.list | \
       sudo tee /etc/apt/sources.list.d/nvidia-docker.list
   
   # Install NVIDIA container toolkit
   sudo apt-get update
   sudo apt-get install -y nvidia-container-toolkit
   
   # Restart Docker daemon
   sudo systemctl restart docker
   
   # Verify installation
   docker run --rm --gpus all nvidia/cuda:12.6.0-base-ubuntu22.04 nvidia-smi
   ```

3. **Clone Repository**
   ```bash
   git clone --recurse-submodules https://github.com/Arrooy/LinuxCam.git
   cd LinuxCam
   ```

4. **Setup Virtual Camera on Host**
   ```bash
   # Run the setup script (requires sudo)
   sudo bash scripts/setup_v4l2loopback.sh
   ```

5. **Generate SSL Certificates**
   ```bash
   # Generate self-signed certificates for HTTPS
   bash scripts/generate_ssl_certs.sh
   ```

6. **Enable X11 Access for Docker**
   ```bash
   # Allow Docker containers to access X11 display
   xhost +local:docker
   ```
   > **Note:** This needs to be run once per session. Add to `~/.bashrc` for automatic execution.

7. **Run with Docker Compose**
   ```bash
   # Build and start the container
   docker-compose up -d
   
   # View logs
   docker-compose logs -f
   
   # Stop the container
   docker-compose down
   ```

**Quick Start Alternative:**
```bash
# One-line setup (after cloning repo)
sudo bash scripts/setup_v4l2loopback.sh && \
bash scripts/generate_ssl_certs.sh && \
xhost +local:docker && \
docker-compose up
```

### Docker Configuration

**Custom Configuration:**
- Mount your own `config.yaml`: Edit `docker-compose.yml` volumes section
- Use different camera device: Change device mappings in `docker-compose.yml`
- Adjust GPU memory: Modify resource limits in `docker-compose.yml`

**Development Mode:**
```bash
# Use development container with source code mounted
docker-compose -f docker-compose.dev.yml up -d

# Enter the container for development
docker exec -it linuxface-dev bash

# Inside container: build and run
cd /workspace/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
./LinuxFace
```

**Common Docker Commands:**
```bash
# Enter running container
docker exec -it linuxface-dev bash

# View container logs
docker logs -f linuxface-dev

# Restart container
docker restart linuxface-dev

# Stop and remove container
docker-compose -f docker-compose.dev.yml down
```

---

## 🛠️ Native Installation

### Prerequisites

#### System Requirements

- **Linux**: Ubuntu 20.04+ or equivalent
- **GPU**: NVIDIA GPU with CUDA 12 support (recommended)
- **Compiler**: GCC 12 or earlier (CUDA 12 limitation)

#### Dependencies

**Core Libraries:**

```bash
# Essential build tools
sudo apt-get install nasm cmake build-essential

# Image processing libraries
sudo apt-get install libopenblas-dev liblapack-dev

# Web framework dependencies (Drogon)
sudo apt-get install libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev pkg-config

# Video processing libraries
sudo apt-get install libavdevice-dev libavfilter-dev libavformat-dev
sudo apt-get install libavcodec-dev libswresample-dev libswscale-dev libavutil-dev

# OpenGL libraries (required for UI)
sudo apt-get install libgl1-mesa-dev libglu1-mesa-dev mesa-common-dev libglx-dev

# X11 libraries (required for GLFW and UI)
sudo apt-get install libxinerama-dev libxcursor-dev libxi-dev libxrandr-dev
sudo apt-get install libxext-dev libxfixes-dev libxrender-dev
```

**CUDA & cuDNN:**

For GPU acceleration, install CUDA toolkit and cuDNN libraries:

```bash
# Add NVIDIA CUDA repository
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update

# Install CUDA Toolkit 12.6
sudo apt install -y cuda-toolkit-12-6

# Install cuDNN 9 for CUDA 12 (required for ONNX Runtime CUDA provider)
sudo apt install -y libcudnn9-dev-cuda-12

# Add CUDA to PATH and library path
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH

# Make environment variables permanent
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/usr/local/cuda/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc

```bash

**Alternative Manual Installation:**

- **CUDA 12**: [Download from NVIDIA](https://developer.nvidia.com/cuda-downloads)
- **cuDNN 9**: [Download from NVIDIA](https://developer.nvidia.com/cudnn-downloads)

> ⚠️ **Important**: CUDA 12 supports GCC ≤ 12. If you have GCC 13+, install an older version:
>
> ```bash
> sudo apt-get install gcc-12 g++-12
> export CC="/usr/bin/gcc-12" && export CXX="/usr/bin/g++-12"
> ```

### Clone Repository

```bash
# Clone with submodules
git clone --recurse-submodules -j8 https://github.com/Arrooy/LinuxCam.git
cd LinuxCam

# Alternative method
git clone https://github.com/Arrooy/LinuxCam.git
cd LinuxCam
git submodule init
git submodule update
```

### Build

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

> 💡 **Note**: The project is configured to use CUDA providers for ONNX Runtime. If you installed CUDA as described above, GPU acceleration will be automatically enabled. To run the application with CUDA support, ensure the CUDA libraries are in your path:
>
> ```bash
> # Run with CUDA support
> LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH ./LinuxFace
> ```

### CUDA Verification

To verify that CUDA is properly installed and working:

```bash
# Check NVIDIA driver
nvidia-smi

# Check CUDA compiler
nvcc --version

# Verify CUDA libraries
ls /usr/local/cuda/lib64/ | grep libcudart

# Check for cuDNN
find /usr/local/cuda -name "*cudnn*" 2>/dev/null
```

When running LinuxFace, you should see these messages indicating successful CUDA initialization:

```log
[INFO] CUDA Execution Provider available
[INFO] OnnxDetector: CUDA provider added successfully with 2GB memory limit
```

If you see warnings about "Failed to load shared library", ensure CUDA paths are set correctly.

### Troubleshooting CUDA Issues

**Common CUDA/cuDNN Errors:**

1. **CUDNN_FE failure / CUDNN_BACKEND_API_FAILED errors:**
   ```bash
   # This usually indicates cuDNN version incompatibility
   # Try using CPU execution instead by setting environment variable:
   export ORT_DISABLE_CUDA=1
   
   # Or downgrade to a compatible cuDNN version:
   sudo apt remove libcudnn9-dev-cuda-12 libcudnn9-cuda-12
   sudo apt install libcudnn8-dev libcudnn8=8.9.7.29-1+cuda12.2
   ```

2. **Memory allocation errors:**
   ```bash
   # Reduce GPU memory usage in your application or
   # Set CUDA memory limit (done automatically in LinuxFace to 2GB)
   ```

3. **Model compatibility issues:**
   ```bash
   # Some ONNX models may not be compatible with newer cuDNN versions
   # Check model documentation or try running with CPU execution
   export ORT_DISABLE_CUDA=1
   ```

### Virtual Webcam Setup

After building, set up the virtual webcam:

```bash
# Install v4l2loopback module (if not already installed)
sudo make install_v4l2loopback

# Create virtual webcam device
sudo modprobe v4l2loopback exclusive_caps=1 video_nr=8 card_label="VirtualWebcam"
```

> 💡 **Tip**: Check available video devices with `ls /dev/ | grep video` and choose a free number for `video_nr`.

## 🚀 Quick Start

1. **Configure the application**:

   ```bash
   # Edit the configuration file
   cp config.yaml my_config.yaml
   # Modify settings as needed
   ```

2. **Run LinuxFace**:

   ```bash
   ./LinuxFace
   ```

3. **Use in video calls**: Select "VirtualWebcam" as your camera in video conferencing applications.

## 📖 Usage

### Configuration

LinuxFace uses YAML configuration files for customization. The main configuration file is `config.yaml`.

**Key Configuration Sections:**

- **Input**: Camera settings and input sources
- **Processing**: Face detection and processing parameters
- **Output**: Virtual webcam and recording settings
- **Models**: AI model paths and configurations

### Command Line Options

```bash
./LinuxFace [options]

Options:
  --config <file>    Use custom configuration file
  --help            Show help message
  --version         Display version information
```

## 🔧 Development

### Development Environments

LinuxCam supports multiple development workflows:

#### VS Code Dev Container (Recommended)

The easiest way to start developing is using VS Code with Dev Containers:

1. **Prerequisites**: Docker, VS Code, and the "Dev Containers" extension
2. **Open in Container**: Open the project in VS Code and click "Reopen in Container"
3. **Start Coding**: All dependencies, tools, and extensions are automatically configured

The devcontainer includes:
- Full C++ development environment with IntelliSense
- CMake and build tools pre-configured
- Debugging support with GDB
- GPU access for testing
- All project dependencies

#### Docker Development Container

For development without VS Code:

```bash
# Start development container with source code mounted
docker-compose -f docker-compose.dev.yml up -d

# Enter the container
docker-compose -f docker-compose.dev.yml exec linuxface-dev /bin/bash

# Build the project
cd /workspace
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

#### Native Development

For traditional local development, see the [Native Installation](#-native-installation) section.

### Code Style

This project uses automated code formatting with [clang-format](https://clang.llvm.org/docs/ClangFormat.html):

- **Automatic Formatting**: Code is automatically formatted in CI/CD
- **Style Guide**: Configuration defined in `.clang-format`
- **Manual Formatting**: Use `clang-format -i <files>` for local formatting

### Testing

```bash
# Run all tests
cd build
ctest --output-on-failure

# Run specific test categories
ctest -R unit_tests
ctest -R integration_tests
```

### Continuous Integration

The project uses GitHub Actions for:

- ✅ **Build Testing**: Multi-configuration builds (Debug/Release)
- 🧪 **Unit Testing**: Comprehensive test suite
- 📊 **Coverage Reporting**: Code coverage analysis
- 🎨 **Code Formatting**: Automatic style enforcement
- 🔒 **Security Analysis**: Static security analysis with Flawfinder

## 🤝 Contributing

We welcome contributions!

### Development Workflow

1. **Fork** the repository
2. **Create** a feature branch (`git checkout -b feature/amazing-feature`)
3. **Commit** your changes (`git commit -m 'Add amazing feature'`)
4. **Push** to the branch (`git push origin feature/amazing-feature`)
5. **Open** a Pull Request

### Code Requirements

- ✅ All tests must pass
- ✅ Code coverage should not decrease
- ✅ Follow the established code style
- ✅ Include appropriate documentation
- ✅ Add tests for new functionality

## 🙏 Acknowledgments

- **CUDA**: NVIDIA CUDA for GPU acceleration
- **OpenCV**: Computer vision library
- **dlib**: Machine learning library for facial recognition
- **v4l2loopback**: Virtual video device support

## 📞 Support

- 🐛 **Bug Reports**: [GitHub Issues](https://github.com/Arrooy/LinuxCam/issues)
- 💬 **Discussions**: [GitHub Discussions](https://github.com/Arrooy/LinuxCam/discussions)
- 📧 **Contact**: Open an issue for support questions

---

Built with ❤️ for the Linux community
