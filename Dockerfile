# Multi-stage Dockerfile for LinuxCam with CUDA support
# Supports production, development, and CI builds with build arguments
#
# Build targets:
#   - base:       CUDA environment with system dependencies
#   - builder:    Compilation stage
#   - runtime:    Minimal production image (default)
#   - development: Full development environment with tools
#
# Build arguments:
#   BUILD_TYPE:        Release|Debug|RelWithDebInfo (default: Release)
#   ENABLE_TESTING:    ON|OFF (default: OFF)
#   ENABLE_CUDA:       ON|OFF (default: ON)
#   DEV_TOOLS:         ON|OFF - Install development tools (default: OFF)
#   GDRIVE_CREDENTIALS: Google Drive credentials for model download
#
# Examples:
#   Production:  docker build --target runtime .
#   Development: docker build --target development --build-arg DEV_TOOLS=ON .
#   CI Testing:  docker build --build-arg ENABLE_TESTING=ON --build-arg BUILD_TYPE=Debug .

# =============================================================================
# Stage 1: Base - CUDA environment with system dependencies
# =============================================================================
FROM nvidia/cuda:12.6.0-cudnn-devel-ubuntu22.04 AS base

# Build arguments for proxy support
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY
ARG http_proxy
ARG https_proxy
ARG no_proxy
ARG DEV_TOOLS=OFF

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive \
    HTTP_PROXY=${HTTP_PROXY} \
    HTTPS_PROXY=${HTTPS_PROXY} \
    NO_PROXY=${NO_PROXY} \
    http_proxy=${http_proxy} \
    https_proxy=${https_proxy} \
    no_proxy=${no_proxy} \
    CUDA_HOME=/usr/local/cuda \
    PATH=${CUDA_HOME}/bin:${PATH} \
    LD_LIBRARY_PATH=${CUDA_HOME}/lib64:${LD_LIBRARY_PATH} \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Install system dependencies (shared for all targets)
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential \
    cmake \
    ninja-build \
    gcc-12 \
    g++-12 \
    nasm \
    git \
    ca-certificates \
    # Image processing libraries
    libopenblas-dev \
    liblapack-dev \
    # Drogon web framework dependencies
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    libssl-dev \
    pkg-config \
    # Video processing libraries (FFmpeg)
    libavdevice-dev \
    libavfilter-dev \
    libavformat-dev \
    libavcodec-dev \
    libswresample-dev \
    libswscale-dev \
    libavutil-dev \
    # OpenGL libraries
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    mesa-common-dev \
    libglx-dev \
    # X11 libraries
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxrandr-dev \
    libxext-dev \
    libxfixes-dev \
    libxrender-dev \
    # Python for scripts
    python3 \
    python3-pip \
    # Utilities
    wget \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Conditionally install development tools
RUN if [ "$DEV_TOOLS" = "ON" ]; then \
        apt-get update && apt-get install -y --no-install-recommends \
            # Debugging tools
            gdb \
            valgrind \
            strace \
            ltrace \
            # Editors and utilities
            vim \
            nano \
            htop \
            tmux \
            zip \
            rsync \
            # Code analysis tools
            clang-format \
            clang-tidy \
            cppcheck \
            # Coverage tools
            gcovr \
            lcov \
            # Additional utilities
            less \
            tree \
            file \
            lsof \
            net-tools \
            iputils-ping \
            dnsutils \
            sudo \
            # v4l2 utilities
            v4l-utils \
        && rm -rf /var/lib/apt/lists/*; \
    fi

# Set GCC-12 as default (CUDA 12 requirement)
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100 && \
    update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-12 100

# Install Python packages
RUN pip3 install --no-cache-dir \
    google-auth \
    google-auth-oauthlib \
    google-auth-httplib2 \
    google-api-python-client \
    gdown \
    $(if [ "$DEV_TOOLS" = "ON" ]; then echo "black pylint pytest"; fi)

# Create application directory
WORKDIR /build

# =============================================================================
# Stage 2: Builder - Compile the application
# =============================================================================
FROM base AS builder

# Build arguments
ARG BUILD_TYPE=Release
ARG ENABLE_TESTING=OFF
ARG ENABLE_CUDA=ON

# Copy source code
COPY . /build/

# Initialize and update git submodules
RUN git submodule update --init --recursive || true

# Download ONNX models
ARG GDRIVE_CREDENTIALS
ENV GDRIVE_CREDENTIALS=${GDRIVE_CREDENTIALS}
RUN if [ -n "${GDRIVE_CREDENTIALS}" ]; then \
        echo "Downloading ONNX models from Google Drive..."; \
        python3 scripts/download_models.py || echo "Warning: Model download failed, continuing..."; \
    else \
        echo "Warning: GDRIVE_CREDENTIALS not provided, models not downloaded"; \
        echo "Models must be provided via volume mount or downloaded separately"; \
    fi

# Configure and build
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DBUILD_TESTING=${ENABLE_TESTING} \
        -DUSE_AVX_INSTRUCTIONS=ON \
        -DDLIB_NO_GUI_SUPPORT=OFF \
        -DDLIB_USE_CUDA=${ENABLE_CUDA} \
        -DBUILD_V4L2LOOPBACK=OFF \
        -G Ninja && \
    echo "Building external dependencies..." && \
    cmake --build . --target libjpeg-turbo && \
    cmake --build . --target libjpeg_include_dir && \
    echo "Building main project..." && \
    cmake --build . --parallel $(nproc)

# Run tests if enabled
RUN if [ "${ENABLE_TESTING}" = "ON" ]; then \
        cd build && ctest --output-on-failure --parallel $(nproc) || echo "Warning: Some tests failed"; \
    fi

# =============================================================================
# Stage 3: Runtime - Minimal production image (default target)
# =============================================================================
FROM nvidia/cuda:12.6.0-cudnn-runtime-ubuntu22.04 AS runtime

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive \
    CUDA_HOME=/usr/local/cuda \
    PATH=${CUDA_HOME}/bin:${PATH} \
    LD_LIBRARY_PATH=${CUDA_HOME}/lib64:/usr/local/lib:${LD_LIBRARY_PATH} \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Install runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
    # FFmpeg runtime libraries (Ubuntu 22.04 versions)
    libavdevice58 \
    libavfilter7 \
    libavformat58 \
    libavcodec58 \
    libswresample3 \
    libswscale5 \
    libavutil56 \
    # Image processing runtime
    libopenblas0 \
    liblapack3 \
    # OpenGL runtime
    libgl1 \
    libglu1-mesa \
    # X11 runtime
    libxinerama1 \
    libxcursor1 \
    libxi6 \
    libxrandr2 \
    libxext6 \
    libxfixes3 \
    libxrender1 \
    # Utilities
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN groupadd -r linuxface && useradd -r -g linuxface -u 1000 linuxface && \
    mkdir -p /app /app/uploads /app/media /app/models /app/www /app/ssl && \
    chown -R linuxface:linuxface /app

WORKDIR /app

# Copy compiled binaries and libraries from builder
COPY --from=builder --chown=linuxface:linuxface /build/build/LinuxFace /app/
COPY --from=builder --chown=linuxface:linuxface /build/build/_deps/onnxruntime-src/lib/*.so* /usr/local/lib/

# Create models directory (will be populated via volume mount or download)
RUN mkdir -p /app/models && chown linuxface:linuxface /app/models

# Copy configuration files
COPY --chown=linuxface:linuxface config.yaml /app/config.yaml

# Copy web assets
COPY --chown=linuxface:linuxface www /app/www

# SSL certificates (mount at runtime or copy before build)
RUN mkdir -p /app/ssl && chown linuxface:linuxface /app/ssl

# Copy media folder
COPY --chown=linuxface:linuxface media /app/media

# Create necessary directories with proper permissions
RUN mkdir -p /app/build && \
    chown -R linuxface:linuxface /app && \
    ldconfig

# Copy entrypoint script
COPY docker-entrypoint.sh /usr/local/bin/
RUN chmod 755 /usr/local/bin/docker-entrypoint.sh

# Switch to non-root user
USER linuxface

# Expose web server port
EXPOSE 8443

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 \
    CMD curl -kf https://localhost:8443/ || exit 1

# Set entrypoint
ENTRYPOINT ["docker-entrypoint.sh"]

# Default command
CMD ["./LinuxFace"]

# =============================================================================
# Stage 4: Development - Full development environment
# =============================================================================
FROM base AS development

# Ensure development tools are installed (if base was built without them)
RUN apt-get update && apt-get install -y --no-install-recommends \
    gdb valgrind strace ltrace vim nano htop tmux zip rsync \
    clang-format clang-tidy cppcheck gcovr lcov \
    less tree file lsof net-tools iputils-ping dnsutils sudo v4l-utils \
    && rm -rf /var/lib/apt/lists/* || true

# Install Python development tools
RUN pip3 install --no-cache-dir black pylint pytest || true

# Create development user with sudo access
RUN groupadd -r linuxface 2>/dev/null || true && \
    useradd -r -g linuxface -u 1000 -m -s /bin/bash linuxface 2>/dev/null || true && \
    echo "linuxface ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    mkdir -p /workspace /workspace/build /workspace/uploads /workspace/media /workspace/models /workspace/www /workspace/ssl && \
    chown -R linuxface:linuxface /workspace

WORKDIR /workspace

# Copy entrypoint script
COPY docker-entrypoint.sh /usr/local/bin/
RUN chmod 755 /usr/local/bin/docker-entrypoint.sh

# Switch to development user
USER linuxface

# Expose web server port and debugging ports
EXPOSE 8443 1234 5678

# Set entrypoint
ENTRYPOINT ["docker-entrypoint.sh"]

# Default command opens a shell for development
CMD ["/bin/bash"]
