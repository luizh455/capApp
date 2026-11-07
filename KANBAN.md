# CapApp Interactive Kanban Board

Welcome to the development task board for **CapApp** (High-Performance Low-Latency Capture Application).
This board tracks progress across milestones. Tasks move through stages: `[Backlog]` ➔ `[To Do]` ➔ `[In Progress]` ➔ `[In Review]` ➔ `[Done]`.

---

## 📌 Board Status Overview

| Column | Count | Focus Area |
| :--- | :--- | :--- |
| **📋 Backlog** | 0 | All tasks completed |
| **⏳ To Do** | 0 | All tasks completed |
| **🚧 In Progress** | 0 | All tasks completed |
| **🔍 In Review** | 0 | All tasks completed |
| **✅ Done** | 25 | All 6 Milestones Completed & Fully Tested (26/26 Tests) |

---

## 🚀 Active Kanban Board

*(All active tasks across all 6 milestones have been successfully implemented, tested, and verified!)*

---

## ✅ Completed Tasks (All Milestones)
- [x] **TASK-001**: Setup CMake root & build presets (`CMakeLists.txt`, `cmake/`, C++20, `-Wall -Wextra -Wpedantic`, Ninja support)
- [x] **TASK-002**: Scaffold directory layout (`src/`, `include/`, `shaders/`, `tests/`, `assets/`, `scripts/`, `docs/`)
- [x] **TASK-003**: Configure Google Test harness via CMake (`ctest` runner, test fixtures)
- [x] **TASK-004**: Implement basic project headers & compiler feature detection (`include/capapp/common.hpp`)
- [x] **TASK-010**: Implement single-producer single-consumer (SPSC) lock-free ring buffer (`RingBuffer<T>` with atomic indices)
- [x] **TASK-011**: Define zero-allocation inline Frame struct (`inline struct Frame` with nanosecond PTS timestamps)
- [x] **TASK-012**: Define `AudioBuffer` container with nanosecond timestamping & interleave utilities
- [x] **TASK-013**: Unit test suite: `RingBuffer::WhenConcurrentAccess::ExpectNoTornFrames`
- [x] **TASK-020**: Vulkan instance, physical device enumeration, and queue family selection
- [x] **TASK-021**: Vulkan exclusive fullscreen context and low-latency presentation engine
- [x] **TASK-022**: Resource abstraction layer: `Vk*Resource` RAII handles (`VkBufferResource`, `VkImageResource`)
- [x] **TASK-023**: Shader pipeline compilation setup (HLSL / GLSL to SPIR-V)
- [x] **TASK-024**: Integration test: `CaptureDevice::Vulkan::OnLostContext` recovery handler
- [x] **TASK-030**: WASAPI capture initialization (exclusive / low-latency event-driven mode, 2-4 frames buffer)
- [x] **TASK-031**: Audio ring buffer dispatch with nanosecond clock synchronization
- [x] **TASK-032**: WASAPI render / loopback audio monitor module
- [x] **TASK-033**: Test suite: Audio buffer underrun/overrun simulation under high CPU load
- [x] **TASK-040**: Hardware capture device interface (`ICaptureDevice`, `IDecoder`)
- [x] **TASK-041**: Video hardware decoder module (D3D11 / DXGI Desktop Duplication / Vulkan Video)
- [x] **TASK-042**: Zero-copy surface transfer into Vulkan textures
- [x] **TASK-043**: Audio/Video PTS synchronization and jitter buffer
- [x] **TASK-050**: High-resolution latency profiling hooks & RenderDoc capture markers
- [x] **TASK-051**: Latency benchmark harness (measuring capture-to-present glass-to-glass latency)
- [x] **TASK-052**: CI workflow script for automated building and `ctest` execution
- [x] **TASK-GUI**: Native Win32 windowing with double-buffered rendering and glassmorphic HUD overlay

---

## 📝 Operating Guidelines for Interactive Implementation
1. We pick **one task at a time** from **To Do**.
2. Move it to **In Progress**.
3. Implement code, write corresponding unit tests according to naming conventions (`{Device}::{Decoder}::When{Condition}`).
4. Run tests and build checks (`cmake --build build`, `ctest`).
5. Move task to **Done**, update `KANBAN.md`, and move to the next item.
