cmake_minimum_required(VERSION 3.21.0)
project(LinuxFace)

option(BUILD_TESTING "Build the tests for the project" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(BUILD_V4L2LOOPBACK "Build v4l2loopback kernel module" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers (AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer)" OFF)
option(ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer (mutually exclusive with other sanitizers)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set default build type only if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Suppress NVCC deprecated GPU target warnings
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Wno-deprecated-gpu-targets")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Coverage function
function(enable_coverage target)
    if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Enabling coverage for target: ${target}")
        target_compile_options(${target} PRIVATE 
            --coverage 
            -fprofile-arcs 
            -ftest-coverage
            -O0
            -g
        )
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

# Sanitizer function
function(enable_sanitizers target)
    if(ENABLE_SANITIZERS AND ENABLE_THREAD_SANITIZER)
        message(FATAL_ERROR "Cannot enable both general sanitizers and ThreadSanitizer simultaneously")
    endif()
    
    if(ENABLE_SANITIZERS)
        message(STATUS "Enabling sanitizers for target: ${target}")
        target_compile_options(${target} PRIVATE
            -fsanitize=address
            -fsanitize=leak
            -fsanitize=undefined
            -fsanitize=float-divide-by-zero
            -fsanitize=float-cast-overflow
            -fno-sanitize-recover=all
            -fstack-protector-strong
            -O1
            -g
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address
            -fsanitize=leak
            -fsanitize=undefined
            -fsanitize=float-divide-by-zero
            -fsanitize=float-cast-overflow
        )
    endif()
    
    if(ENABLE_THREAD_SANITIZER)
        message(STATUS "Enabling ThreadSanitizer for target: ${target}")
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -O1
            -g
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=thread
        )
    endif()
endfunction()

# Global coverage setup for Debug builds when explicitly enabled
if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Code coverage enabled for Debug build")
    
    # Find gcov
    find_program(GCOV_PATH gcov)
    if(NOT GCOV_PATH)
        message(WARNING "gcov not found! Coverage reports may not work properly.")
    endif()
endif()

# Global sanitizer setup and environment configuration
if(ENABLE_SANITIZERS OR ENABLE_THREAD_SANITIZER)
    # Check compiler support
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Sanitizers supported by ${CMAKE_CXX_COMPILER_ID}")
    else()
        message(WARNING "Sanitizers may not be fully supported by ${CMAKE_CXX_COMPILER_ID}")
    endif()

    # Set recommended environment variables for better sanitizer output
    if(ENABLE_SANITIZERS)
        message(STATUS "AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer enabled")
        message(STATUS "Recommended environment variables:")
        message(STATUS "  export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:check_initialization_order=1")
        message(STATUS "  export UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1")
        message(STATUS "  export LSAN_OPTIONS=print_stats=1")
    endif()
    
    if(ENABLE_THREAD_SANITIZER)
        message(STATUS "ThreadSanitizer enabled")
        message(STATUS "Recommended environment variables:")
        message(STATUS "  export TSAN_OPTIONS=abort_on_error=1:halt_on_error=1")
    endif()
    
    # Force Debug build type for sanitizers to be most effective
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Sanitizers are most effective with Debug build type. Consider using -DCMAKE_BUILD_TYPE=Debug")
    endif()
endif()

# Check for nasm
find_program(NASM_EXECUTABLE nasm)
if(NOT NASM_EXECUTABLE)
    message(FATAL_ERROR "NASM not found! Please install NASM before proceeding. libjpeg-turbo requires it.\nIf Ubuntu, consider running:\nsudo apt install nasm")
endif()

add_compile_options(-march=native)

include(FetchContent)

include(ExternalProject)
# Use system JPEG libraries (compatible with ffmpeg's libjpeg.so.8)
find_package(JPEG REQUIRED)
find_library(TURBOJPEG_LIBRARY NAMES turbojpeg REQUIRED)
find_path(TURBOJPEG_INCLUDE_DIR NAMES turbojpeg.h REQUIRED)

add_library(libjpeg INTERFACE)
target_include_directories(libjpeg INTERFACE ${JPEG_INCLUDE_DIR})
target_link_libraries(libjpeg INTERFACE ${JPEG_LIBRARIES})

add_library(turbojpeg INTERFACE)
target_include_directories(turbojpeg INTERFACE ${TURBOJPEG_INCLUDE_DIR} ${JPEG_INCLUDE_DIR})
target_link_libraries(turbojpeg INTERFACE ${TURBOJPEG_LIBRARY})

# libcurl
find_package(CURL REQUIRED)

# json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

# ========= ONNX Runtime Setup =========
FetchContent_Declare(
    onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.24.1/onnxruntime-linux-x64-gpu_cuda13-1.24.1.tgz
)

FetchContent_MakeAvailable(onnxruntime)

set(ONNXRUNTIME_ROOT ${onnxruntime_SOURCE_DIR})

# Create an interface library for ONNX Runtime to properly expose includes
add_library(onnxruntime_lib INTERFACE)
target_include_directories(onnxruntime_lib INTERFACE ${ONNXRUNTIME_ROOT}/include)
target_link_directories(onnxruntime_lib INTERFACE ${ONNXRUNTIME_ROOT}/lib)
target_link_libraries(onnxruntime_lib INTERFACE onnxruntime)

# Set RPATH so the executable can find ONNX Runtime libraries at runtime
set(CMAKE_INSTALL_RPATH "${ONNXRUNTIME_ROOT}/lib")
set(CMAKE_BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# ======================================

# GLAD from https://glad.dav1d.de/

# Add glad manually
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

set(OpenGL_GL_PREFERENCE "GLVND")
# Find required packages for UI
find_package(OpenGL REQUIRED)

# Add GLFW
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# ImGui source files
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    # Backend files
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Create ImGui library
add_library(imgui STATIC ${IMGUI_SOURCES})

# ImGui include directories
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link ImGui with GLFW and OpenGL
target_link_libraries(imgui PUBLIC
    glfw
    ${OPENGL_LIBRARIES}
)

# ========= v4l2loopback (Optional) =========
# Only build if explicitly requested with -DBUILD_V4L2LOOPBACK=ON
if(BUILD_V4L2LOOPBACK)
    message(STATUS "v4l2loopback build enabled")
    
    # Create a wrapper script to handle build failures gracefully
    file(WRITE "${CMAKE_BINARY_DIR}/build_v4l2loopback.sh"
"#!/bin/bash
cd ${CMAKE_SOURCE_DIR}/external/v4l2loopback
if make; then
    echo 'v4l2loopback built successfully'
    exit 0
else
    echo 'v4l2loopback build failed - kernel headers may be missing'
    exit 1
fi
")
    
    execute_process(COMMAND chmod +x "${CMAKE_BINARY_DIR}/build_v4l2loopback.sh")
    
    ExternalProject_Add(
      v4l2loopback
      SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/v4l2loopback
      CONFIGURE_COMMAND ""
      BUILD_COMMAND bash ${CMAKE_BINARY_DIR}/build_v4l2loopback.sh
      BUILD_IN_SOURCE 1
      INSTALL_COMMAND ""
      LOG_CONFIGURE 1
      LOG_BUILD 1
      BUILD_ALWAYS 0
    )
    
    # Function to check if v4l2loopback module file exists
    function(check_v4l2loopback_installed result)
      execute_process(
        COMMAND bash -c "test -f /lib/modules/$(uname -r)/kernel/drivers/media/video/v4l2loopback.ko && echo yes || echo no"
        OUTPUT_VARIABLE _mod_exists
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(_mod_exists STREQUAL "yes")
        set(${result} TRUE PARENT_SCOPE)
      else()
        set(${result} FALSE PARENT_SCOPE)
      endif()
    endfunction()
    
    check_v4l2loopback_installed(IS_V4L2_INSTALLED)
    
    if(IS_V4L2_INSTALLED)
      message(STATUS "v4l2loopback module already installed")
    else()
      message(STATUS "v4l2loopback module NOT installed - use 'make install_v4l2loopback' to install")
      
      add_custom_target(
        install_v4l2loopback
        COMMAND bash ${CMAKE_BINARY_DIR}/build_v4l2loopback.sh
        COMMAND sudo make install
        COMMAND sudo depmod -a
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/external/v4l2loopback
        COMMENT "Building and installing v4l2loopback kernel module (requires sudo)"
      )
    endif()
    
    add_custom_target(
      load_loopback_module
      COMMAND sudo modprobe v4l2loopback exclusive_caps=1 devices=1 video_nr=8 card_label="Virtual Camera"
      COMMENT "Loading v4l2loopback kernel module"
    )
    
    add_dependencies(load_loopback_module v4l2loopback)
else()
    message(STATUS "v4l2loopback build disabled (use -DBUILD_V4L2LOOPBACK=ON to enable)")
    message(STATUS "Virtual camera output will not be available")
endif()

# ==========================================

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        2f86d13775d119edbb69af52e5f566fd65c6953b
)
FetchContent_MakeAvailable(yaml-cpp)

# Disable coverage for external yaml-cpp library
if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(yaml-cpp PROPERTIES
        COMPILE_FLAGS ""
        LINK_FLAGS ""
    )
    target_compile_options(yaml-cpp PRIVATE -O2)  # Override the debug flags
endif()

# Fetch gifdec from GitHub
FetchContent_Declare(
    gifdec
    GIT_REPOSITORY https://github.com/lecram/gifdec.git
    GIT_TAG master
)

# Clone & populate if not already fetched
FetchContent_GetProperties(gifdec)
if(NOT gifdec_POPULATED)
    FetchContent_Populate(gifdec)

    # Create a static library target
    add_library(gifdec STATIC ${gifdec_SOURCE_DIR}/gifdec.c)

    # Make headers available to users of gifdec
    target_include_directories(gifdec PUBLIC ${gifdec_SOURCE_DIR})
    
    # Disable coverage for external gifdec library
    if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(gifdec PRIVATE -O2)  # Override debug/coverage flags
    endif()
endif()

# Fetch Drogon web framework
FetchContent_Declare(
    drogon
    GIT_REPOSITORY https://github.com/drogonframework/drogon.git
    GIT_TAG        v1.9.8
)

# Configure Drogon build options
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_ORM OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(drogon)

# Fetch libdatachannel for WebRTC support
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.23.2
)

# Configure libdatachannel build options
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)  # We only need WebRTC, not WebSocket from this lib

FetchContent_MakeAvailable(libdatachannel)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Find FFmpeg libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)

# Create a static library for main code
add_library(linuxface STATIC ${SRC_FILES})

# Set include directories for the library
target_include_directories(linuxface PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${FFMPEG_INCLUDE_DIRS}
)

# Link dependencies for the library
target_link_libraries(linuxface
    libjpeg
    turbojpeg
    imgui
    glfw
    ${OPENGL_LIBRARIES}
    GL
    glad
    yaml-cpp::yaml-cpp
    gifdec
    onnxruntime_lib
    ${FFMPEG_LIBRARIES}
    drogon
    datachannel-static
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

# Enable coverage for the main library
enable_coverage(linuxface)

# Enable sanitizers for the main library
enable_sanitizers(linuxface)

# Define the main executable
add_executable(LinuxFace src/main.cpp)
target_include_directories(LinuxFace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web
)
target_link_libraries(LinuxFace PRIVATE linuxface)

# Set RPATH to find ONNX Runtime library at runtime
set_target_properties(LinuxFace PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
    INSTALL_RPATH "${ONNXRUNTIME_ROOT}/lib"
    BUILD_RPATH_USE_ORIGIN TRUE
)

# Enable coverage for the main executable too
enable_coverage(LinuxFace)

# Enable sanitizers for the main executable too
enable_sanitizers(LinuxFace)

target_compile_options(LinuxFace PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wdouble-promotion
)

# -Werror

# Tests executable links to the library
if(BUILD_TESTING)
    message(STATUS "Building with tests enabled")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    # Define test data directory for test resources
    set(TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/common")
    
    # Configure a header file with test paths
    configure_file(
        "${CMAKE_SOURCE_DIR}/tests/config/test_config.h.in"
        "${CMAKE_BINARY_DIR}/test_config.h"
    )

    # Automatically find all test source files
    file(GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS "tests/*.cpp")

    add_executable(linuxface_tests ${TEST_SRC_FILES})

    target_include_directories(linuxface_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}  # For generated test_config.h
    )

    # Only link the static library and gtest_main
    target_link_libraries(linuxface_tests PRIVATE linuxface gtest_main)

    # Enable coverage for tests
    enable_coverage(linuxface_tests)

    # Enable sanitizers for tests
    enable_sanitizers(linuxface_tests)

    # Configure test discovery with appropriate timeouts for sanitizers
    if(ENABLE_SANITIZERS OR ENABLE_THREAD_SANITIZER)
        # Sanitizers make test discovery slow, so increase timeout and disable discovery
        message(STATUS "Sanitizers enabled - skipping automatic test discovery to avoid timeouts")
        message(STATUS "Run tests manually with: ./linuxface_tests")
        # Note: Tests can still be run manually, just not auto-discovered by CTest
    else()
        # Normal test discovery when sanitizers are disabled
        gtest_discover_tests(linuxface_tests)
    endif()
endif()

# Coverage target for convenience
if(ENABLE_COVERAGE)
    add_custom_target(coverage
        COMMAND gcovr -r ${CMAKE_SOURCE_DIR} 
                --exclude-directories '${CMAKE_BINARY_DIR}'
                --exclude-directories '${CMAKE_SOURCE_DIR}/external'
                --exclude '${CMAKE_SOURCE_DIR}/tests/.*'
                --filter '${CMAKE_SOURCE_DIR}/src/.*'
                --html-details coverage.html
                --xml coverage.xml
                --print-summary
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report"
        DEPENDS linuxface_tests
    )
endif()
