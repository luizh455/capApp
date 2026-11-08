# CapApp development

## Requirements

- Windows
- Visual Studio with the C++ desktop development tools and Windows SDK
- CMake 3.25 or newer
- Ninja

## Build

Open a Visual Studio Developer PowerShell (or another shell configured for MSVC) in the repository directory:

```powershell
cmake --preset ninja-release
cmake --build --preset ninja-release
```

The executable is `build\src\capapp.exe`.

## Run

List the capture devices CapApp can see:

```powershell
build\src\capapp.exe --list-devices
```

Start the preview:

```powershell
build\src\capapp.exe
```

Or choose a device and target mode:

```powershell
build\src\capapp.exe --device 1 --resolution 1080p --fps 60
```

The target resolution and FPS are requests; the device may negotiate a different source mode. Use **Tab** to inspect the negotiated and running values.

## Command-line options

| Option | Description |
| --- | --- |
| `--list-devices`, `-l` | List detected capture devices and their advertised formats |
| `--device`, `-d <index or name>` | Select a capture device |
| `--resolution`, `-r <mode>` | Request `1080p`, `1440p`, `4k`, or explicit dimensions such as `2560x1440` |
| `--fps <rate>` | Request a target frame rate |
| `--fullscreen`, `-f` | Start fullscreen |
| `--no-hud` | Start with the settings/telemetry overlay hidden |
| `--benchmark`, `-b` | Run the built-in latency benchmark |
| `--help`, `-h` | Show command-line help |

## Tests

Build and run the test suite with:

```powershell
cmake --build --preset ninja-release
ctest --preset ninja-release
```

## Project layout

| Path | Contents |
| --- | --- |
| `src/` | Application, capture, rendering, audio, and synchronization |
| `include/capapp/` | Public headers and shared types |
| `tests/` | Unit and integration tests |
| `shaders/` | Rendering shaders |
| `assets/` | Application resources |
| `docs/` | Technical documentation and screenshots |
