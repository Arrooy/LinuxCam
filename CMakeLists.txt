
cmake_minimum_required(VERSION 3.10.0)
project(FunnyFace)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(CMAKE_BUILD_TYPE "Release")
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
        -DWITH_SIMD=ON # Enable SIMD, which requires nasm
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

# Ensure dependent targets wait for libjpeg-turbo to be built
add_custom_target(libjpeg_include_dir ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBJPEG_TURBO_INSTALL_DIR}/include
    DEPENDS libjpeg-turbo
)

add_dependencies(libjpeg libjpeg_include_dir)
add_dependencies(turbojpeg libjpeg_include_dir)

# Define the main executable
add_executable(FunnyFace main.cpp camera.h camera.cpp common.h JPEGManager.h JPEGManager.cpp)

# Link dependencies
target_link_libraries(FunnyFace dlib::dlib libjpeg turbojpeg)


# Ensure include directory is set *after* it exists
add_dependencies(FunnyFace libjpeg_include_dir)
target_include_directories(FunnyFace PRIVATE "${LIBJPEG_TURBO_INSTALL_DIR}/include")

# Strip symbols in Release mode
target_link_options(FunnyFace PRIVATE $<$<CONFIG:RELEASE>:-s>)
