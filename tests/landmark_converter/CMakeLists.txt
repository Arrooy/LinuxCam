# Landmark Converter Tests
set(LANDMARK_CONVERTER_TEST_SOURCES
    test_landmark_converter.cpp
)

# Create the test executable
add_executable(landmark_converter_tests ${LANDMARK_CONVERTER_TEST_SOURCES})

# Link libraries
target_link_libraries(landmark_converter_tests
    PRIVATE
    gtest_main
    linuxface
)

# Include directories
target_include_directories(landmark_converter_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Add tests to CTest
gtest_discover_tests(landmark_converter_tests
    TEST_PREFIX "LandmarkConverter."
)
