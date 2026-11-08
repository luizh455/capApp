# src/CMakeLists.txt

add_library(capapp_core
    version.cpp
    vulkan_context.cpp
    wasapi_engine.cpp
    capture_device.cpp
    mf_capture_device.cpp
    camera_selector.cpp
    av_sync_pipeline.cpp
    color_converter.cpp
    frame_recorder.cpp
    window.cpp
    d3d11_renderer.cpp
    app.cpp
    benchmark.cpp
)

target_link_libraries(capapp_core PUBLIC
    capapp_core_headers
)

if(WIN32)
    target_link_libraries(capapp_core PUBLIC
        user32
        gdi32
        mf
        mfplat
        mfuuid
        mfreadwrite
        ole32
        oleaut32
        propsys
        setupapi
        cfgmgr32
        d3d11
        dxgi
        d3dcompiler
        gdiplus
    )
endif()

target_set_warnings(capapp_core)

# Executable target
add_executable(capapp
    main.cpp
)

if(WIN32)
    set(CAPAPP_ICON_PATH "${PROJECT_SOURCE_DIR}/assets/icons/capapp.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/capapp.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/capapp.rc"
        @ONLY
    )
    target_sources(capapp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/capapp.rc")
endif()

target_link_libraries(capapp PRIVATE
    capapp_core
)

target_set_warnings(capapp)

add_custom_command(TARGET capapp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/assets/images"
        "$<TARGET_FILE_DIR:capapp>/assets/images"
)
