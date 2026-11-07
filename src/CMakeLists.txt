# src/CMakeLists.txt

add_library(capapp_core
    version.cpp
    vulkan_context.cpp
    wasapi_engine.cpp
    capture_device.cpp
    av_sync_pipeline.cpp
    window.cpp
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
    )
endif()

target_set_warnings(capapp_core)

# Executable target
add_executable(capapp
    main.cpp
)

target_link_libraries(capapp PRIVATE
    capapp_core
)

target_set_warnings(capapp)
