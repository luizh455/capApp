# CapApp Architecture & Module Design

CapApp is designed for ultra-low latency capture, processing, and presentation.

## Core Modules
1. **Core Data Structures (`include/capapp/`, `src/`)**: Lock-free SPSC queues, inline frame allocations.
2. **Audio Subsystem**: Shared-mode WASAPI capture from enumerated Windows input endpoints, converted to interleaved float audio and timestamped in nanoseconds. Captured audio is monitored to a selectable Windows output endpoint; input and output selections are persisted, with gain (0-100%) and mute applied before monitoring, metering, and A/V synchronization.
3. **Rendering Subsystem**: Vulkan exclusive fullscreen context and low-latency swapchain presentation.
4. **Capture & Decoder Pipeline**: Hardware-accelerated decoding (Vulkan Video / D3D11 / NVDEC) with zero-copy texture transfers.

Application preferences are saved to `%APPDATA%\CapApp\settings.ini` on Windows (`~/.config/CapApp/settings.ini` elsewhere) and restored at startup. Explicit command-line options take precedence over saved camera, resolution, FPS, fullscreen, and HUD settings.
