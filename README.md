# LinuxFace 🎭

> A high-performance real-time facial processing and virtual webcam application for Linux

[![CI Workflow](https://github.com/Arrooy/LinuxCam/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/ci.yml)
[![Code Formatting](https://github.com/Arrooy/LinuxCam/actions/workflows/code-formatting.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/code-formatting.yml)
[![codecov](https://codecov.io/github/Arrooy/LinuxCam/branch/develop/graph/badge.svg?token=NWVE3AC940)](https://codecov.io/github/Arrooy/LinuxCam)
[![Security Analysis - Flawfinder](https://github.com/Arrooy/LinuxCam/actions/workflows/flawfinder.yml/badge.svg?branch=develop)](https://github.com/Arrooy/LinuxCam/actions/workflows/flawfinder.yml)

## ✨ Features

- 🎥 **Real-time Face Processing**: Computer Vision playground
- 📹 **Virtual Webcam Integration**: Seamless virtual camera output via v4l2loopback
- 🚀 **GPU Acceleration**: CUDA 12 support for high-performance processing
- 🎨 **Multiple Processing Modes**: Face swapping, enhancement, and analysis
- 📊 **Testing**: Automated CI with coverage reporting

## 📋 Table of Contents

- [Installation](#️-installation)
- [Quick Start](#-quick-start)
- [Usage](#-usage)
- [Development](#-development)
- [Contributing](#-contributing)
- [License](#-license)

## 🛠️ Installation

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
sudo apt-get install libjxl-dev libopenblas-dev liblapack-dev

# Video processing libraries
sudo apt-get install libavdevice-dev libavfilter-dev libavformat-dev
sudo apt-get install libavcodec-dev libswresample-dev libswscale-dev libavutil-dev

# X11 libraries
sudo apt-get install libxinerama-dev libxcursor-dev libxi-dev
```

**CUDA & cuDNN:**

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
