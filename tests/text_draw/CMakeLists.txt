# Text Draw Tests
set(TEXT_DRAW_TEST_SOURCES
    test_text_draw_core.cpp
    test_text_draw_edge_cases.cpp
    test_text_draw_enhanced_api.cpp
    test_text_draw_performance.cpp
    test_text_draw_visual_validation.cpp
)

# Create the test executable
add_executable(text_draw_tests ${TEXT_DRAW_TEST_SOURCES})

# Link libraries
target_link_libraries(text_draw_tests
    PRIVATE
    gtest_main
    linuxface
)

# Include directories
target_include_directories(text_draw_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Add tests to CTest
gtest_discover_tests(text_draw_tests
    TEST_PREFIX "TextDraw."
)
