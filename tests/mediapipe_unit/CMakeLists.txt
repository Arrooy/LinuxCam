# MediaPipe Unit Tests CMakeLists.txt

# Test executable for MediaPipe Face Landmarks unit tests
add_executable(mediapipe_unit_tests
    test_mediapipe_unit.cpp
    test_mediapipe_landmark_conversion.cpp
    test_mediapipe_real_image.cpp
    ../common/test_utils.cpp
    ../common/dataset_utils.cpp
)

# Link against required libraries
target_link_libraries(mediapipe_unit_tests
    linuxface
    ${ONNXRUNTIME_LIBRARIES}
    gtest
    gtest_main
    ${OpenCV_LIBS}
    jpeg
    pthread
)

# Set include directories
target_include_directories(mediapipe_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests/common
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

# Add test to CTest
add_test(NAME MediaPipeUnitTests COMMAND mediapipe_unit_tests)

# Set working directory for tests
set_tests_properties(MediaPipeUnitTests PROPERTIES 
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add compiler flags
target_compile_features(mediapipe_unit_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(mediapipe_unit_tests PRIVATE /W4)
else()
    target_compile_options(mediapipe_unit_tests PRIVATE -Wall -Wextra -pedantic)
endif()
