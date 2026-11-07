# Repository Guidelines

## Project Structure & Module Organization

`
J:\dev\capApp/
├── src/              # Source code (Vulkan, audio, decoder modules)
├── include/          # Header files (.hxx, .hpp)
├── shaders/          # HLSL/Vulkan SPIR-V shader files
├── tests/            # Unit and integration tests
├── cmake/            # CMakeLists.txt, presets, build configs
├── assets/           # Configs, icons, default resources
├── docs/             # Technical documentation
└── scripts/          # Build helpers, CI pipelines
## Build, Test, and Development Commands

Use CMake to configure projects. In J:\dev\capApp\, run:

`ash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
`

Run tests with Google Test:

`ash
ctest --test-dir build -C Release --output-on-failure
`

## Coding Style & Naming Conventions

- Use 4-space indentation (no tabs)
- GPU resources: Vk*Resource (VkBuffer, VkImageView)
- Audio buffers: AudioBuffer* with timestamp in nanoseconds
- Frame structs marked inline to reduce allocation overhead
- Enable compiler warnings: -Wall -Wextra -Wpedantic

## Testing Guidelines

Testing framework: **Google Test** integrated via CMake. Run:

`ash
ctest --test-dir build -j4
`

Target coverage: ≥80% (use gcov or coverity).  
Test naming: {Device}::{Decoder}::When{Condition} (e.g., CaptureDevice::Vulkan::OnLostContext)

## Commit & Pull Request Guidelines

Use **Conventional Commits**:

- eat: new capability (e.g., HDR passthrough)
- perf: latency improvements (e.g., frame queue optimization)
- ix: bug corrections
- efactor: code reorganization without behavior change

PR checklist:

- Link related issue in description (Refs #123)
- Include latency benchmark comparison (before/after)
- Attach screenshots if UI changes were made
- Ensure CI passes before merging

## Performance Tips ⚡

- Capture device must run in **Vulkan exclusive context**
- WASAPI buffer size: 2–4 frames max to minimize latency
- Use lock-free ring buffers for frame queue (std::atomic flags)
- Profile with RenderDoc or GPU debugger to identify stalls
