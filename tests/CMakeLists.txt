# tests/CMakeLists.txt

option(CAPAPP_FETCH_GTEST "Fetch official GoogleTest repository from GitHub" OFF)

if(CAPAPP_FETCH_GTEST)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
else()
    # High-performance zero-dependency Google Test compatible harness
    add_library(gtest_headers INTERFACE)
    target_include_directories(gtest_headers INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
    add_library(GTest::gtest ALIAS gtest_headers)
    add_library(GTest::gtest_main ALIAS gtest_headers)
endif()

add_executable(capapp_tests
    test_main.cpp
    test_version.cpp
    test_ring_buffer.cpp
    test_vulkan_context.cpp
    test_wasapi_engine.cpp
    test_capture_sync.cpp
    test_app.cpp
    test_profiler_benchmark.cpp
    test_camera_selector.cpp
    test_color_converter.cpp
    test_frame_recorder.cpp
    test_post_processor.cpp
)

target_link_libraries(capapp_tests PRIVATE
    capapp_core
    GTest::gtest
)

target_set_warnings(capapp_tests)

add_test(NAME capapp_unit_tests COMMAND capapp_tests)
