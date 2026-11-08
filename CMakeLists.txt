cmake_minimum_required(VERSION 3.25)

# Embedded debug format avoids mspdbsrv IPC lockups on Windows (must be set before project)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "MSVC debug format")

project(CapApp
    VERSION 0.1.0
    DESCRIPTION "High-performance low-latency capture application"
    LANGUAGES CXX C
)

if(WIN32)
    enable_language(RC)
endif()

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for language servers (clangd / VS Code / CLion)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Embedded debug format avoids mspdbsrv IPC lockups on Windows
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

# Module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# Project options
option(BUILD_TESTING "Build unit and integration tests" ON)
option(CAPAPP_BUILD_DOCS "Build documentation" OFF)

# Top-level include directory
add_library(capapp_core_headers INTERFACE)
target_include_directories(capapp_core_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(capapp_core_headers INTERFACE cxx_std_20)

# Add subdirectories
add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
