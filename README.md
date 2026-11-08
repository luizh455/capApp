# CapApp

**A low-latency Windows preview for capture cards and cameras.**

CapApp previews live video in a resizable or fullscreen window, with capture controls, audio monitoring, and live device diagnostics.

## Screenshots

![CapApp home menu with live capture and negotiated metrics](docs/screenshots/capapp-monitoring.png)

![CapApp home menu with the video input options panel open](docs/screenshots/capapp-options.png)

## What it does

- Previews cameras and capture cards and reports the mode negotiated with the device.
- Lets you change the capture device, resolution, frame-rate target, and capture format while the app is running.
- Preserves the video aspect ratio when resizing; choose **None** (nearest-neighbor), **Smooth**, or **Lanczos-3** scaling.
- Fits a window to the running frame dimensions with **Native size**, scaling down proportionally when needed.
- Captures audio from a selectable input and monitors it through a selectable output, with software volume and mute controls.
- Shows negotiated source settings separately from the running output size, measured FPS, frame timings, and USB information.
- Remembers your settings between launches.

## Controls

The home menu opens on startup. Use **Up/Down** to focus a setting and **Right** or **Enter** to open its choices. Use **Up/Down** to browse choices and **Enter** to apply one; **Left** returns from the choices panel. You can also click settings and choices.

| Key | Action |
| --- | --- |
| `Esc` | Toggle the settings menu |
| `Tab` | Show/hide negotiated and running capture details; when the menu is closed, show details without the menu |
| `C` | Cycle video input |
| `R` | Cycle target resolution |
| `P` | Cycle target frame rate |
| `K` | Cycle available capture formats |
| `L` | Cycle scaling: None, Smooth, Lanczos-3 |
| `I` | Cycle audio input |
| `+` / `-` | Adjust audio monitor volume |
| `M` | Mute/unmute audio monitoring |
| `S` | Save a frame snapshot |
| `F11` | Toggle fullscreen/windowed mode |
| `Space` | Pause/resume the preview |

The **Display** setting includes Windowed, Fullscreen, and Native size (fit screen). Audio output selection and other configurable options are in the home menu. Settings are stored at `%APPDATA%\CapApp\settings.ini` and restored on launch; explicit command-line values take precedence for supported options.

## Development

Build requirements, setup instructions, tests, and command-line options are documented in [DEVELOPMENT.md](DEVELOPMENT.md).
