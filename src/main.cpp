#include <capapp/app.hpp>
#include <capapp/common.hpp>
#include <capapp/capture/camera_selector.hpp>
#include <capapp/profiling/benchmark.hpp>
#include <iostream>
#include <cstring>
#include <cctype>

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "  " << capapp::get_version_string() << "\n";
    std::cout << "  High-Performance Low-Latency Capture Engine\n";
    std::cout << "=========================================================\n";

    capapp::AppOptions options{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list-devices") == 0 || std::strcmp(argv[i], "-l") == 0) {
            std::cout << "\nScanning for Video Capture & Camera Devices...\n";
            auto devices = capapp::capture::CameraSelector::enumerate_devices();
            std::cout << "Found " << devices.size() << " Video Capture Input(s):\n";
            for (const auto& dev : devices) {
                std::cout << "  [" << dev.index << "] " << dev.name
                          << " (" << dev.width << "x" << dev.height << " @" << dev.maxFps << " FPS)"
                          << (dev.isVirtual ? " [Virtual]" : "") << "\n";
                std::cout << "      Capture formats:";
                for (const auto compression : dev.supportedCompressions) {
                    std::cout << " " << capapp::capture::capture_compression_name(compression);
                }
                std::cout << "\n";
            }
            std::cout << "\nTo start with a specific device: capapp.exe --device <index_or_name>\n";
            return 0;
        } else if ((std::strcmp(argv[i], "--device") == 0 || std::strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            std::string arg = argv[++i];
            options.cameraSpecified = true;
            if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                options.cameraIndex = static_cast<uint32_t>(std::stoul(arg));
            } else {
                options.cameraName = arg;
            }
        } else if ((std::strcmp(argv[i], "--resolution") == 0 || std::strcmp(argv[i], "-r") == 0) && i + 1 < argc) {
            std::string resStr = argv[++i];
            options.resolutionSpecified = true;
            if (resStr == "1080p" || resStr == "1080") {
                options.targetWidth = 1920; options.targetHeight = 1080;
            } else if (resStr == "1440p" || resStr == "1440") {
                options.targetWidth = 2560; options.targetHeight = 1440;
            } else if (resStr == "4k" || resStr == "2160p" || resStr == "2160") {
                options.targetWidth = 3840; options.targetHeight = 2160;
            } else if (auto xPos = resStr.find('x'); xPos != std::string::npos) {
                options.targetWidth = static_cast<uint32_t>(std::stoul(resStr.substr(0, xPos)));
                options.targetHeight = static_cast<uint32_t>(std::stoul(resStr.substr(xPos + 1)));
            }
        } else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            options.targetFps = static_cast<uint32_t>(std::stoul(argv[++i]));
            options.fpsSpecified = true;
        } else if (std::strcmp(argv[i], "--benchmark") == 0 || std::strcmp(argv[i], "-b") == 0) {
            std::cout << "[CapApp] Initiating automated glass-to-glass latency benchmark (500 frames)...\n";
            capapp::profiling::LatencyBenchmark bench(500);
            auto report = bench.run();
            std::cout << report.format_report() << "\n";
            return 0;
        } else if (std::strcmp(argv[i], "--fullscreen") == 0 || std::strcmp(argv[i], "-f") == 0) {
            options.startFullscreen = true;
            options.fullscreenSpecified = true;
        } else if (std::strcmp(argv[i], "--no-hud") == 0) {
            options.showHud = false;
            options.hudSpecified = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: capapp.exe [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --list-devices, -l     List all connected camera and capture inputs\n";
            std::cout << "  --device, -d <id>      Select camera by device index or name\n";
            std::cout << "  --resolution, -r <res> Set resolution target (1080p, 1440p, 4k, or WxH)\n";
            std::cout << "  --fps <target>         Set target frame rate (30 or 60)\n";
            std::cout << "  --benchmark, -b        Run automated end-to-end latency benchmark\n";
            std::cout << "  --fullscreen, -f       Start directly in exclusive fullscreen\n";
            std::cout << "  --no-hud               Hide OSD telemetry overlay on start\n";
            std::cout << "  --help, -h             Show help message\n";
            std::cout << "\nKeyboard Controls:\n";
            std::cout << "  C     - Cycle through Camera Inputs\n";
            std::cout << "  R     - Cycle Target Resolution (1080p -> 1440p -> 4K)\n";
            std::cout << "  P     - Cycle Target FPS (30 FPS <-> 60 FPS)\n";
            std::cout << "  K     - Cycle Capture Format (Auto, YUY2, MJPEG)\n";
            std::cout << "  L     - Toggle Scaling Filter (Smooth / Lanczos-3)\n";
            std::cout << "  F     - Cycle Post-Processing Shader FX (Scanlines, Sharpen, Contrast)\n";
            std::cout << "  S     - Take High-Res Frame Snapshot\n";
            std::cout << "  I     - Cycle Audio Input | +/- - Volume | M - Mute\n";
            std::cout << "  F11   - Toggle Exclusive Fullscreen / Windowed\n";
            std::cout << "  Tab   - Toggle OSD Telemetry HUD\n";
            std::cout << "  Space - Pause / Resume Streaming\n";
            std::cout << "  Esc   - Exit Application\n";
            std::cout << "Settings are saved automatically and restored on the next launch.\n";
            return 0;
        }
    }

    capapp::CapApplication app(options);
    if (!app.initialize()) {
        std::cerr << "Failed to initialize CapApp.\n";
        return 1;
    }

    return app.run();
}
