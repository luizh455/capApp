@echo off
setlocal
cd /d "%~dp0\.."

if not exist "build\src\capapp.exe" (
    echo Building CapApp first...
    call scripts\build.bat
)

echo Starting CapApp with Interactive GUI and Live Telemetry...
echo.
echo Keyboard Controls:
echo   [C]     - Cycle Camera Inputs (Webcam / HDMI Capture Card)
echo   [F11]   - Toggle Exclusive Fullscreen / Windowed
echo   [Tab]   - Toggle OSD Telemetry HUD
echo   [Space] - Pause / Resume Streaming
echo   [Esc]   - Exit Application
echo.
start "" "build\src\capapp.exe" %*
