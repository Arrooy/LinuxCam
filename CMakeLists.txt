cmake_minimum_required(VERSION 3.14.0)
project(LinuxFace)

option(BUILD_TESTING "Build the tests for the project" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(BUILD_V4L2LOOPBACK "Build v4l2loopback kernel module" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE "Release")
# set(CMAKE_BUILD_TYPE Debug)

set(USE_AVX_INSTRUCTIONS ON CACHE BOOL "Use AVX instructions for Dlib")
set(DLIB_NO_GUI_SUPPORT OFF CACHE BOOL "Disable GUI support for Dlib")
set(DLIB_USE_CUDA ON CACHE BOOL "Use CUDA for Dlib")

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

# Global coverage setup for Debug builds when explicitly enabled
if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Code coverage enabled for Debug build")
    
    # Find gcov
    find_program(GCOV_PATH gcov)
    if(NOT GCOV_PATH)
        message(WARNING "gcov not found! Coverage reports may not work properly.")
    endif()
endif()

# Check for nasm
find_program(NASM_EXECUTABLE nasm)
if(NOT NASM_EXECUTABLE)
    message(FATAL_ERROR "NASM not found! Please install NASM before proceeding. libjpeg-turbo requires it.\nIf Ubuntu, consider running:\nsudo apt install nasm")
endif()

add_compile_options(-Wall)

# Suppress warnings from external dependencies
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")

include(FetchContent)

# Fetch dlib
FetchContent_Declare(dlib
    GIT_REPOSITORY https://github.com/davisking/dlib.git
    GIT_TAG        v19.24.8
    # DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(dlib)

include(ExternalProject)
set(LIBJPEG_TURBO_INSTALL_DIR "${CMAKE_BINARY_DIR}/libjpeg-turbo-install")

# Add libjpeg-turbo as an ExternalProject
ExternalProject_Add(libjpeg-turbo
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG        3.1.0
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBJPEG_TURBO_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DWITH_SIMD=ON
        -DREQUIRE_SIMD=TRUE
    PREFIX ${CMAKE_BINARY_DIR}/libjpeg-turbo
    INSTALL_DIR ${LIBJPEG_TURBO_INSTALL_DIR}
)

# Manually define an imported target for libjpeg
add_library(libjpeg STATIC IMPORTED)
set_target_properties(libjpeg PROPERTIES
    IMPORTED_LOCATION "${LIBJPEG_TURBO_INSTALL_DIR}/lib/libjpeg.a"
)

add_library(turbojpeg STATIC IMPORTED)
set_target_properties(turbojpeg PROPERTIES
    IMPORTED_LOCATION "${LIBJPEG_TURBO_INSTALL_DIR}/lib/libturbojpeg.a"
)

add_custom_target(libjpeg_include_dir ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBJPEG_TURBO_INSTALL_DIR}/include
    DEPENDS libjpeg-turbo
)

add_dependencies(libjpeg libjpeg_include_dir)
add_dependencies(turbojpeg libjpeg_include_dir)

# ========= ONNX Runtime Setup =========
include(FetchContent)

FetchContent_Declare(
    onnxruntime
    #URL https://github.com/microsoft/onnxruntime/releases/download/v1.22.0/onnxruntime-linux-x64-gpu-1.22.0.tgz
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-x64-gpu-1.17.3.tgz
    # DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(onnxruntime)

set(ONNXRUNTIME_ROOT ${onnxruntime_SOURCE_DIR})

include_directories(${ONNXRUNTIME_ROOT}/include)
link_directories(${ONNXRUNTIME_ROOT}/lib)

# Pre-built ONNX Runtime GPU package includes CUDA provider in the main library
# The GPU version already has CUDAExecutionProvider compiled in
set(ONNXRUNTIME_LIB onnxruntime)

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

include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        2f86d13775d119edbb69af52e5f566fd65c6953b
  # DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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

include(FetchContent)

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
    "${LIBJPEG_TURBO_INSTALL_DIR}/include"
    ${FFMPEG_INCLUDE_DIRS}
)

# Link dependencies for the library
target_link_libraries(linuxface
    dlib::dlib
    libjpeg
    turbojpeg
    imgui
    glfw
    ${OPENGL_LIBRARIES}
    GL
    glad
    yaml-cpp::yaml-cpp
    gifdec
    ${ONNXRUNTIME_LIB}
    ${FFMPEG_LIBRARIES}
    drogon
)

# Enable coverage for the main library
enable_coverage(linuxface)

# Define the main executable
add_executable(LinuxFace src/main.cpp)
target_include_directories(LinuxFace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web
    "${LIBJPEG_TURBO_INSTALL_DIR}/include"
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

# Ensure include directory is set *after* it exists
add_dependencies(LinuxFace libjpeg_include_dir)

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
        "${LIBJPEG_TURBO_INSTALL_DIR}/include"
        ${CMAKE_BINARY_DIR}  # For generated test_config.h
    )

    # Only link the static library and gtest_main
    target_link_libraries(linuxface_tests PRIVATE linuxface gtest_main)

    # Enable coverage for tests
    enable_coverage(linuxface_tests)

    gtest_discover_tests(linuxface_tests)
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
