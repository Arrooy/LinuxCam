add_executable(webrtc_unit_tests
    test_webrtcTransport.cpp
)

target_link_libraries(webrtc_unit_tests
    linuxface
    gtest
    gtest_main
)

target_include_directories(webrtc_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

add_test(NAME WebRTCUnitTests COMMAND webrtc_unit_tests)

set_tests_properties(WebRTCUnitTests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
    target_compile_options(webrtc_unit_tests PRIVATE /W4)
else()
    target_compile_options(webrtc_unit_tests PRIVATE -Wall -Wextra -pedantic)
endif()
