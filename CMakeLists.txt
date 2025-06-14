cmake_minimum_required(VERSION 3.10.0)
project(FunnyFace)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_BUILD_TYPE Debug)

set(USE_AVX_INSTRUCTIONS ON CACHE BOOL "Use AVX instructions for Dlib")
set(DLIB_NO_GUI_SUPPORT OFF CACHE BOOL "Disable GUI support for Dlib")
set(DLIB_USE_CUDA ON CACHE BOOL "Use CUDA for Dlib")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Check for nasm
find_program(NASM_EXECUTABLE nasm)
if(NOT NASM_EXECUTABLE)
    message(FATAL_ERROR "NASM not found! Please install NASM before proceeding. libjpeg-turbo requires it.\nIf Ubuntu, consider running:\nsudo apt install nasm")
endif()

add_compile_options(-Wall)

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
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.22.0/onnxruntime-linux-x64-gpu-1.22.0.tgz
    # DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(onnxruntime)

set(ONNXRUNTIME_ROOT ${onnxruntime_SOURCE_DIR})

include_directories(${ONNXRUNTIME_ROOT}/include)
link_directories(${ONNXRUNTIME_ROOT}/lib)

# Add ONNX Runtime (shared or static depending on your build)
# If shared:
set(ONNXRUNTIME_LIB onnxruntime)
# If static and built with extra providers:
# set(ONNXRUNTIME_LIB onnxruntime onnxruntime_providers_shared onnxruntime_providers_cuda)

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

# v4l2loopback
ExternalProject_Add(
  v4l2loopback
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/v4l2loopback
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${MAKE_COMMAND} 
  BUILD_IN_SOURCE 1
  INSTALL_COMMAND ""
  LOG_CONFIGURE 1
  LOG_BUILD 1
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
  message(STATUS "v4l2loopback module NOT installed. Install it using: sudo make install_v4l2loopback")

  add_custom_target(
    install_v4l2loopback
    COMMAND make
    COMMAND sudo make install
    COMMAND sudo depmod -a
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/external/v4l2loopback
    COMMENT "Building and installing v4l2loopback kernel module"
  )
endif()

add_custom_target(
  load_loopback_module
  COMMAND sudo modprobe v4l2loopback exclusive_caps=1 devices=1 video_nr=8 card_label="Webcam del tet"
  COMMENT "Loading v4l2loopback kernel module"
)

# Make sure modprobe happens after it's built
add_dependencies(load_loopback_module v4l2loopback)

include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        2f86d13775d119edbb69af52e5f566fd65c6953b
  # DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(yaml-cpp)

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
endif()


file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp)

# Define the main executable
add_executable(FunnyFace
    ${SRC_FILES}
)

# Link dependencies
target_link_libraries(FunnyFace
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
)

# Ensure include directory is set *after* it exists
add_dependencies(FunnyFace libjpeg_include_dir)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    "${LIBJPEG_TURBO_INSTALL_DIR}/include"
    ${PROJECT_SOURCE_DIR}/include
)


# Strip symbols in Release mode
# target_link_options(FunnyFace PRIVATE $<$<CONFIG:RELEASE>:-s>)
