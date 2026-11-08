# CapApp

**A low-latency Windows preview for capture cards and cameras.**

CapApp opens a video capture device, displays its feed in a resizable window or fullscreen, and provides live diagnostics for the negotiated capture mode and USB connection. It is built in C++20 with Windows Media Foundation and a Direct3D 11 presentation path.

## Features

- Enumerates connected capture devices and their advertised YUY2/MJPEG formats.
- Preserves the source aspect ratio while resizing, filling unused space with black bars.
- Lets you switch capture format, resolution, frame-rate target, and scaling filter while running.
- Resizes the windowed preview to the running capture frame's native size, scaling down proportionally when it will not fit the screen.
- Lets you select both the audio input and monitoring output devices from the settings menu.
- Offers Lanczos-3 and smooth mipmap scaling options.
- Displays selected settings separately from the negotiated source mode and live running output, FPS, timing, and USB link information (`Tab`).
- Reports nominal YUY2 input bandwidth when available. MJPEG’s compressed bitrate is shown as unavailable because the current Media Foundation path exposes decoded frames.
- Supports windowed and fullscreen viewing, plus frame snapshots.

> CapApp is under active development. Device support, available modes, and performance depend on the capture device, USB connection, and PC.

## Requirements

- Windows
- Visual Studio with the C++ desktop development tools and Windows SDK
- CMake 3.25 or newer
- Ninja

## Build

From a Visual Studio Developer PowerShell or other shell configured for MSVC:

```powershell
cmake --preset ninja-release
cmake --build --preset ninja-release
```

The executable is created at `build\src\capapp.exe`.

## Run

List detected devices and advertised capture formats:

```powershell
build\src\capapp.exe --list-devices
```

Start CapApp with its default device:

```powershell
build\src\capapp.exe
```

Choose a device, resolution, and frame-rate target:

```powershell
build\src\capapp.exe --device 1 --resolution 1080p --fps 60
```

`--device` accepts a device index or name. Supported resolution targets include `1080p`, `1440p`, `4k`, and explicit dimensions such as `2560x1440`. The capture device determines which modes can actually be negotiated.

### Command-line options

| Option | Description |
| --- | --- |
| `--list-devices`, `-l` | List detected capture devices and formats |
| `--device`, `-d <index or name>` | Select a capture device |
| `--resolution`, `-r <mode>` | Set a target resolution (`1080p`, `1440p`, `4k`, or `WxH`) |
| `--fps <rate>` | Set a target frame rate |
| `--fullscreen`, `-f` | Start fullscreen |
| `--no-hud` | Start with the telemetry HUD hidden |
| `--benchmark`, `-b` | Run the built-in latency benchmark |
| `--help`, `-h` | Show command-line help |

### Keyboard controls

The live preview opens with a controller-friendly home panel. Click a setting or focus it with Up/Down, then press Right/Enter to open its options in the side panel. Use Up/Down to browse choices, Enter to select, and Left to return.

| Key | Action |
| --- | --- |
| `C` | Cycle capture devices |
| `R` | Cycle target resolution |
| `P` | Cycle target frame rate |
| `K` | Cycle available capture formats |
| `L` | Toggle scaling filter |
| `F` | Cycle post-processing effects |
| `S` | Save a frame snapshot |
| `F11` | Toggle fullscreen/windowed mode |
| `I` | Cycle audio input |
| `+` / `-` | Adjust audio monitor volume |
| `M` | Mute/unmute audio |
| `Esc` | Toggle the settings menu open/closed |
| `Tab` | Show/hide negotiated and live running details; shown alone when the menu is closed |
| `Space` | Pause/resume preview |

## Tests

Build and run the test suite with:

```powershell
cmake --build --preset ninja-release
ctest --preset ninja-release
```

## Project layout

| Path | Contents |
| --- | --- |
| `src/` | Application, capture, rendering, audio, and synchronization code |
| `include/capapp/` | Public headers and shared types |
| `tests/` | Unit and integration tests |
| `shaders/` | Rendering shaders |
| `assets/` | Application resources |
| `docs/` | Technical documentation |
