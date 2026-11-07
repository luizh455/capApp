# CapApp Architecture & Module Design

CapApp is designed for ultra-low latency capture, processing, and presentation.

## Core Modules
1. **Core Data Structures (`include/capapp/`, `src/`)**: Lock-free SPSC queues, inline frame allocations.
2. **Audio Subsystem**: Low-latency WASAPI capture engine (2-4 frames buffer) with nanosecond PTS timestamps.
3. **Rendering Subsystem**: Vulkan exclusive fullscreen context and low-latency swapchain presentation.
4. **Capture & Decoder Pipeline**: Hardware-accelerated decoding (Vulkan Video / D3D11 / NVDEC) with zero-copy texture transfers.
