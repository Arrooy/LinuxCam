cmake_minimum_required(VERSION 3.10.0)
project(FunnyFace)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE "Release")
# set(CMAKE_BUILD_TYPE Debug)

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
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/external/onnxruntime")

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
find_package(glfw3 REQUIRED)
# TODO: user external glfw intead of glfw3

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

# Define the main executable
add_executable(FunnyFace
    main.cpp camera.h camera.cpp
    common.h JPEGManager.h JPEGManager.cpp
    image.h math_utils.h
    face.h face.cpp
    detectors.h dlibDetectors.h dlibDetectors.cpp
    queue.hpp
    ui.cpp ui.h
    window.cpp window.h
    application.cpp application.h
    imageRenderGL.cpp imageRenderGL.h
    profiler.h profiler.cpp
)

# Link dependencies
target_link_libraries(FunnyFace
    dlib::dlib
    libjpeg
    turbojpeg
    ${ONNXRUNTIME_LIB}
    imgui
    glfw
    ${OPENGL_LIBRARIES}
    GL
    glad
)

# Ensure include directory is set *after* it exists
add_dependencies(FunnyFace libjpeg_include_dir)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    "${LIBJPEG_TURBO_INSTALL_DIR}/include"
)


# target_link_libraries(${PROJECT_NAME} PRIVATE GL)

# Strip symbols in Release mode
target_link_options(FunnyFace PRIVATE $<$<CONFIG:RELEASE>:-s>)
