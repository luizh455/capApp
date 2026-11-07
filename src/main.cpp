#include <capapp/app.hpp>
#include <capapp/common.hpp>
#include <capapp/profiling/benchmark.hpp>
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "  " << capapp::get_version_string() << "\n";
    std::cout << "  High-Performance Low-Latency Capture Engine\n";
    std::cout << "=========================================================\n";

    capapp::AppOptions options{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--benchmark") == 0 || std::strcmp(argv[i], "-b") == 0) {
            std::cout << "[CapApp] Initiating automated glass-to-glass latency benchmark (500 frames)...\n";
            capapp::profiling::LatencyBenchmark bench(500);
            auto report = bench.run();
            std::cout << report.format_report() << "\n";
            return 0;
        } else if (std::strcmp(argv[i], "--fullscreen") == 0 || std::strcmp(argv[i], "-f") == 0) {
            options.startFullscreen = true;
        } else if (std::strcmp(argv[i], "--no-hud") == 0) {
            options.showHud = false;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: capapp.exe [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --benchmark, -b     Run automated end-to-end latency benchmark\n";
            std::cout << "  --fullscreen, -f    Start directly in exclusive fullscreen\n";
            std::cout << "  --no-hud            Hide OSD telemetry overlay on start\n";
            std::cout << "  --help, -h          Show help message\n";
            std::cout << "\nKeyboard Controls:\n";
            std::cout << "  F11   - Toggle Exclusive Fullscreen / Windowed\n";
            std::cout << "  Tab   - Toggle OSD Telemetry HUD\n";
            std::cout << "  Space - Pause / Resume Streaming\n";
            std::cout << "  Esc   - Exit Application\n";
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
