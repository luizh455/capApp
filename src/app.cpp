#include <capapp/app.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <cmath>
#include <array>

#if defined(_WIN32)
#include <objbase.h>
#endif

namespace capapp::capture {
    std::unique_ptr<ICaptureDevice> create_capture_device(const std::wstring& symbolicLink);
}

namespace capapp {

namespace {

struct AppSettings {
    bool fullscreen{false};
    bool showHud{true};
    std::string cameraName;
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{60};
    uint8_t compression{static_cast<uint8_t>(capture::CaptureCompression::Auto)};
    uint8_t filter{static_cast<uint8_t>(ui::FilterMode::Passthrough)};
    bool lanczos{true};
    std::wstring audioInputId;
    std::wstring audioOutputId;
    float volume{1.0f};
    bool muted{false};
};

std::filesystem::path settings_path() {
#if defined(_WIN32)
    wchar_t* appData = nullptr;
    if (_wdupenv_s(&appData, nullptr, L"APPDATA") == 0 && appData && *appData) {
        const std::filesystem::path directory(appData);
        std::free(appData);
        return directory / "CapApp" / "settings.ini";
    }
    std::free(appData);
#else
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "CapApp" / "settings.ini";
    }
#endif
    return std::filesystem::current_path() / "capapp.ini";
}

bool parse_unsigned(const std::map<std::string, std::string>& values,
                    const char* key, uint32_t& result) {
    const auto it = values.find(key);
    if (it == values.end()) return false;
    const auto* begin = it->second.data();
    const auto* end = begin + it->second.size();
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool parse_boolean(const std::map<std::string, std::string>& values,
                   const char* key, bool& result) {
    uint32_t parsed = 0;
    if (!parse_unsigned(values, key, parsed) || parsed > 1) return false;
    result = parsed != 0;
    return true;
}

std::wstring decode_wide_id(const std::string& encoded) {
    std::wstring result;
    if (encoded.size() % 8 != 0) return result;
    for (size_t i = 0; i < encoded.size(); i += 8) {
        uint32_t codeUnit = 0;
        const auto parsed = std::from_chars(encoded.data() + i, encoded.data() + i + 8,
                                            codeUnit, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != encoded.data() + i + 8) return {};
        result.push_back(static_cast<wchar_t>(codeUnit));
    }
    return result;
}

std::string encode_wide_id(const std::wstring& value) {
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0');
    for (wchar_t character : value) {
        encoded << std::setw(8) << static_cast<uint32_t>(character);
    }
    return encoded.str();
}

AppSettings load_settings() {
    AppSettings settings;
    const auto path = settings_path();
    std::ifstream file(path);
    if (!file) return settings;

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line)) {
        const size_t separator = line.find('=');
        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }

    parse_boolean(values, "fullscreen", settings.fullscreen);
    parse_boolean(values, "show_hud", settings.showHud);
    uint32_t parsedWidth = settings.width;
    uint32_t parsedHeight = settings.height;
    if (parse_unsigned(values, "width", parsedWidth) && parsedWidth > 0 && parsedWidth <= 16384) {
        settings.width = parsedWidth;
    }
    if (parse_unsigned(values, "height", parsedHeight) && parsedHeight > 0 && parsedHeight <= 16384) {
        settings.height = parsedHeight;
    }
    uint32_t parsedFps = settings.fps;
    if (parse_unsigned(values, "fps", parsedFps) && parsedFps > 0 && parsedFps <= 1000) {
        settings.fps = parsedFps;
    }
    uint32_t value = 0;
    if (parse_unsigned(values, "compression", value) &&
        value <= static_cast<uint8_t>(capture::CaptureCompression::MJPEG)) {
        settings.compression = static_cast<uint8_t>(value);
    }
    if (parse_unsigned(values, "filter", value) &&
        value <= static_cast<uint8_t>(ui::FilterMode::HighContrast)) {
        settings.filter = static_cast<uint8_t>(value);
    }
    parse_boolean(values, "lanczos", settings.lanczos);
    parse_boolean(values, "muted", settings.muted);
    if (const auto it = values.find("camera_name"); it != values.end()) {
        std::istringstream input(it->second);
        input >> std::quoted(settings.cameraName);
    }
    if (const auto it = values.find("audio_input_id"); it != values.end()) {
        settings.audioInputId = decode_wide_id(it->second);
    }
    if (const auto it = values.find("audio_output_id"); it != values.end()) {
        settings.audioOutputId = decode_wide_id(it->second);
    }
    if (const auto it = values.find("volume"); it != values.end()) {
        std::istringstream input(it->second);
        float volume = settings.volume;
        if (input >> volume && std::isfinite(volume) && volume >= 0.0f && volume <= 1.0f) {
            settings.volume = volume;
        }
    }
    return settings;
}

} // namespace

CapApplication::CapApplication(AppOptions options)
    : options_(options) {}

CapApplication::~CapApplication() {
    stop_capture_reconfiguration_worker();
#if defined(_WIN32)
    if (memDC_) {
        SelectObject(memDC_, oldBitmap_);
        DeleteObject(memBitmap_);
        DeleteDC(memDC_);
    }
#endif
}

void CapApplication::queue_capture_reconfiguration(CaptureRequest request) {
    {
        std::lock_guard lock(captureRequestMutex_);
        if (stopCaptureReconfiguration_) {
            std::cerr << "[CapApp Error] Capture reconfiguration worker is unavailable.\n";
            return;
        }
        pendingCaptureRequest_ = std::move(request);
        captureReconfiguring_.store(true, std::memory_order_release);
    }
    captureRequestCv_.notify_one();
}

void CapApplication::capture_reconfiguration_loop() {
#if defined(_WIN32)
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        std::cerr << "[CapApp Error] Failed to initialize COM for capture configuration: 0x"
                  << std::hex << comResult << std::dec << "\n";
        std::lock_guard lock(captureRequestMutex_);
        pendingCaptureRequest_.reset();
        captureReconfiguring_.store(false, std::memory_order_release);
        stopCaptureReconfiguration_ = true;
        return;
    }
#endif

    for (;;) {
        CaptureRequest request;
        {
            std::unique_lock lock(captureRequestMutex_);
            captureRequestCv_.wait(lock, [this] {
                return stopCaptureReconfiguration_ || pendingCaptureRequest_.has_value();
            });
            if (stopCaptureReconfiguration_) break;

            request = std::move(*pendingCaptureRequest_);
            pendingCaptureRequest_.reset();
        }

        {
            std::lock_guard lock(captureDeviceMutex_);
            {
                std::lock_guard frameLock(lastFrameMutex_);
                lastFrame_.data = nullptr;
                lastFrame_.width = 0;
                lastFrame_.height = 0;
            }
            if (captureDev_) captureDev_->close();

            auto newDevice = capture::create_capture_device(request.symbolicLink);
            if (!newDevice->open(request.config) || !newDevice->start_stream()) {
                std::cerr << "[CapApp Error] Failed to apply capture configuration for "
                          << request.config.deviceName << ".\n";
                newDevice->close();
                captureDev_ = std::move(newDevice);
            } else {
                captureDev_ = std::move(newDevice);
                std::cout << "[CapApp] Capture configuration applied: "
                          << request.config.targetWidth << "x" << request.config.targetHeight
                          << " @" << request.config.targetFps << " FPS\n";
            }
        }

        {
            std::lock_guard lock(captureRequestMutex_);
            if (!pendingCaptureRequest_) {
                captureReconfiguring_.store(false, std::memory_order_release);
            }
        }
    }

#if defined(_WIN32)
    CoUninitialize();
#endif
}

void CapApplication::stop_capture_reconfiguration_worker() noexcept {
    {
        std::lock_guard lock(captureRequestMutex_);
        stopCaptureReconfiguration_ = true;
        pendingCaptureRequest_.reset();
    }
    captureRequestCv_.notify_one();
    if (captureReconfigurationThread_.joinable()) {
        captureReconfigurationThread_.join();
    }
}

void CapApplication::save_settings() const noexcept {
    try {
        const auto path = settings_path();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            std::cerr << "[CapApp Error] Could not create settings directory: "
                      << error.message() << "\n";
            return;
        }

        std::ofstream file(path, std::ios::trunc);
        if (!file) {
            std::cerr << "[CapApp Error] Could not write settings file: "
                      << path.string() << "\n";
            return;
        }

        const auto* camera = cameraSelector_ ? cameraSelector_->active_device() : nullptr;
        const auto resolution = cameraSelector_ ? cameraSelector_->active_resolution()
                                                : capture::ResolutionPreset{};
        file << "fullscreen=" << (window_ && window_->is_fullscreen() ? 1 : 0) << '\n'
             << "show_hud=" << (options_.showHud ? 1 : 0) << '\n'
             << "camera_name=" << std::quoted(camera ? camera->name : std::string{}) << '\n'
             << "width=" << resolution.width << '\n'
             << "height=" << resolution.height << '\n'
             << "fps=" << (cameraSelector_ ? cameraSelector_->active_fps() : options_.targetFps) << '\n'
             << "compression=" << static_cast<unsigned>(captureCompression_) << '\n'
             << "filter=" << static_cast<unsigned>(activeFilter_) << '\n'
             << "lanczos=" << (useLanczosScaling_ ? 1 : 0) << '\n';
        if (audioEngine_) {
            file << "audio_input_id=" << encode_wide_id(audioEngine_->selected_input_device_id()) << '\n'
                 << "audio_output_id=" << encode_wide_id(audioEngine_->selected_output_device_id()) << '\n'
                 << "volume=" << std::setprecision(9) << audioEngine_->volume() << '\n'
                 << "muted=" << (audioEngine_->is_muted() ? 1 : 0) << '\n';
        }
        file.flush();
        if (!file) {
            std::cerr << "[CapApp Error] Failed while writing settings file: "
                      << path.string() << "\n";
        }
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "[CapApp Error] Could not save application settings: "
                  << error.what() << "\n";
    } catch (const std::ios_base::failure& error) {
        std::cerr << "[CapApp Error] Could not save application settings: "
                  << error.what() << "\n";
    }
}

bool CapApplication::initialize() {
    const AppSettings saved = load_settings();
    if (!options_.fullscreenSpecified && !options_.startFullscreen) {
        options_.startFullscreen = saved.fullscreen;
    }
    if (!options_.hudSpecified && options_.showHud) {
        options_.showHud = saved.showHud;
    }
    if (!options_.cameraSpecified && options_.cameraName.empty() && options_.cameraIndex == 0 &&
        !saved.cameraName.empty()) {
        options_.cameraName = saved.cameraName;
    }
    if (!options_.resolutionSpecified && options_.targetWidth == 1920 &&
        options_.targetHeight == 1080) {
        options_.targetWidth = saved.width;
        options_.targetHeight = saved.height;
    }
    if (!options_.fpsSpecified && options_.targetFps == 60) {
        options_.targetFps = saved.fps;
    }
    captureCompression_ = static_cast<capture::CaptureCompression>(saved.compression);
    activeFilter_ = static_cast<ui::FilterMode>(saved.filter);
    useLanczosScaling_ = saved.lanczos;

    std::cout << "[CapApp] Initializing native window...\n";
    ui::WindowConfig winCfg{};
    winCfg.title = L"CapApp - High-Performance Ultra Low-Latency Capture";
    winCfg.width = 1280;
    winCfg.height = 720;
    winCfg.startFullscreen = options_.startFullscreen;

    window_ = std::make_unique<ui::NativeWindow>(winCfg);
    if (!window_->create()) {
        std::cerr << "[CapApp Error] Failed to create native Win32 window.\n";
        return false;
    }

#if defined(_WIN32)
    d3dRenderer_ = std::make_unique<ui::D3D11Renderer>();
    if (!d3dRenderer_->initialize(window_->hwnd())) {
        std::cerr << "[CapApp Warning] GPU renderer unavailable; using GDI fallback.\n";
        d3dRenderer_.reset();
    }
#endif

    std::cout << "[CapApp] Initializing Vulkan context & GPU device...\n";
    vulkanCtx_ = std::make_unique<vulkan::VulkanContext>();
    if (!vulkanCtx_->initialize()) {
        std::cerr << "[CapApp Warning] Vulkan hardware initialization fallback to virtual context.\n";
    }

    std::cout << "[CapApp] Initializing WASAPI low-latency audio engine (2-4 frames)...\n";
    audio::WasapiConfig audioCfg{};
    audioCfg.sampleRate = 48000;
    audioCfg.channels = 2;
    audioCfg.bufferFrameCount = 4; // 2-4 frames per repository guidelines
    audioCfg.exclusiveMode = false;
    audioCfg.enableDeviceCapture = true;
    audioEngine_ = std::make_unique<audio::WasapiAudioEngine>(audioCfg);
    if (!audioEngine_->initialize()) {
        std::cerr << "[CapApp Error] Failed to initialize the audio capture engine.\n";
    }
    if (!saved.audioInputId.empty() &&
        !audioEngine_->select_input_device_id(saved.audioInputId)) {
        std::cerr << "[CapApp Warning] Saved audio input is unavailable; using the default input.\n";
    }
    if (!saved.audioOutputId.empty() &&
        !audioEngine_->select_output_device_id(saved.audioOutputId)) {
        std::cerr << "[CapApp Warning] Saved audio output is unavailable; using the default output.\n";
    }
    audioEngine_->set_volume(saved.volume);
    audioEngine_->set_muted(saved.muted);
    if (!audioEngine_->start()) {
        std::cerr << "[CapApp Error] Failed to start the audio capture engine.\n";
    }

    std::cout << "[CapApp] Enumerating video capture devices...\n";
    cameraSelector_ = std::make_unique<capture::CameraSelector>();
    if (!options_.cameraName.empty()) {
        cameraSelector_->select_device_by_name(options_.cameraName);
    } else if (options_.cameraSpecified || options_.cameraIndex > 0) {
        cameraSelector_->select_device(options_.cameraIndex);
    }
    if (options_.targetWidth > 0 && options_.targetHeight > 0) {
        cameraSelector_->set_resolution(options_.targetWidth, options_.targetHeight);
    }
    if (options_.targetFps > 0) {
        cameraSelector_->set_fps(options_.targetFps);
    }

    const auto* initialCam = cameraSelector_->active_device();
    if (initialCam &&
        std::find(initialCam->supportedCompressions.begin(),
                  initialCam->supportedCompressions.end(), captureCompression_) ==
            initialCam->supportedCompressions.end()) {
        std::cerr << "[CapApp Warning] Saved capture format is unavailable for "
                  << initialCam->name << "; using Auto.\n";
        captureCompression_ = capture::CaptureCompression::Auto;
    }
    std::cout << "[CapApp] Active Camera: [" << (initialCam ? initialCam->index : 0) << "] "
              << (initialCam ? initialCam->name : "Default") << "\n";

    std::cout << "[CapApp] Opening hardware capture device...\n";
    const std::wstring symLink = initialCam ? initialCam->symbolicLink : L"";
    captureDev_ = capture::create_capture_device(symLink);
    capture::CaptureDeviceConfig capCfg{};
    capCfg.targetWidth = initialCam ? initialCam->width : 1920;
    capCfg.targetHeight = initialCam ? initialCam->height : 1080;
    capCfg.targetFps = cameraSelector_->active_fps();
    capCfg.preferredFormat = initialCam ? initialCam->preferredFormat : PixelFormat::BGRA8;
    capCfg.compression = captureCompression_;
    capCfg.deviceName = initialCam ? initialCam->name : "Default Device";
    capCfg.enableZeroCopy = true;

    if (!captureDev_->open(capCfg) || !captureDev_->start_stream()) {
        // MF device open failed (e.g. no camera permission in sandbox / CI).
        // Fall back to the stub DesktopCaptureDevice so the rest of the app works.
        std::cerr << "[CapApp Warning] Real camera open failed — falling back to stub capture device.\n";
        captureDev_ = capture::create_capture_device(L""); // forces DesktopCaptureDevice
        if (!captureDev_->open(capCfg) || !captureDev_->start_stream()) {
            std::cerr << "[CapApp Error] Fallback capture device also failed.\n";
            return false;
        }
    }
    captureStreamInfo_ = captureDev_->stream_info();

    // Dynamic camera switching callback
    cameraSelector_->onDeviceChanged = [this](const capture::CameraDeviceInfo& newDev) {
        std::cout << "[CapApp] Dynamic Camera Switch -> [" << newDev.index << "] " << newDev.name << "\n";
        capture::CaptureDeviceConfig cfg{};
        cfg.targetWidth = newDev.width;
        cfg.targetHeight = newDev.height;
        cfg.targetFps = cameraSelector_->active_fps();
        cfg.preferredFormat = newDev.preferredFormat;
        captureCompression_ = capture::CaptureCompression::Auto;
        cfg.compression = captureCompression_;
        cfg.deviceName = newDev.name;
        cfg.enableZeroCopy = true;

        queue_capture_reconfiguration({newDev.symbolicLink, std::move(cfg)});
    };

    // Dynamic resolution / FPS format change callback
    cameraSelector_->onFormatChanged = [this](uint32_t width, uint32_t height, uint32_t fps) {
        const auto* dev = cameraSelector_->active_device();
        std::cout << "[CapApp] Format Re-negotiation -> " << width << "x" << height << " @" << fps << " FPS\n";
        capture::CaptureDeviceConfig cfg{};
        cfg.targetWidth = width;
        cfg.targetHeight = height;
        cfg.targetFps = fps;
        cfg.preferredFormat = dev ? dev->preferredFormat : PixelFormat::BGRA8;
        cfg.compression = captureCompression_;
        cfg.deviceName = dev ? dev->name : "Default";
        cfg.enableZeroCopy = true;

        queue_capture_reconfiguration({dev ? dev->symbolicLink : L"", std::move(cfg)});
    };
    captureReconfigurationThread_ = std::thread(&CapApplication::capture_reconfiguration_loop, this);

    avSync_ = std::make_unique<sync::AVSyncPipeline>(vulkanCtx_.get());
    frameRecorder_ = std::make_unique<capture::FrameRecorder>();
    frameRecorder_->start();

    // Setup input callbacks
    window_->onResize = [this](uint32_t, uint32_t) {
        render_frame();
    };
    window_->onPaint = [this]() {
        render_frame();
    };
    window_->onKeyPress = [this](uint32_t key) {
        if (key == VK_ESCAPE) {
            toggle_hud();
            if (!options_.showHud) {
                optionPanelOpen_ = false;
            }
        } else if (key == VK_TAB) {
            showNegotiatedInfo_ = !showNegotiatedInfo_;
        } else if (key == VK_UP) {
            if (optionPanelOpen_) {
                const auto choices = home_option_choices(selectedHomeOption_);
                if (!choices.empty()) {
                    selectedOptionChoice_ = (selectedOptionChoice_ +
                        static_cast<int>(choices.size()) - 1) % static_cast<int>(choices.size());
                }
            } else {
                select_home_option(selectedHomeOption_ - 1);
            }
        } else if (key == VK_DOWN) {
            if (optionPanelOpen_) {
                const auto choices = home_option_choices(selectedHomeOption_);
                if (!choices.empty()) {
                    selectedOptionChoice_ = (selectedOptionChoice_ + 1) %
                        static_cast<int>(choices.size());
                }
            } else {
                select_home_option(selectedHomeOption_ + 1);
            }
        } else if (key == VK_LEFT) {
            if (optionPanelOpen_) optionPanelOpen_ = false;
            else select_home_option(selectedHomeOption_ - 1);
        } else if (key == VK_RIGHT || key == VK_RETURN) {
            if (optionPanelOpen_) {
                apply_option_choice(selectedHomeOption_,
                                    static_cast<size_t>(selectedOptionChoice_));
                optionPanelOpen_ = false;
            } else {
                open_option_panel();
            }
        } else if (key == VK_SPACE) {
            toggle_pause();
        } else if (key == 'C' || key == 'c') {
            cycle_camera();
        } else if (key == 'R' || key == 'r') {
            cycle_resolution();
        } else if (key == 'P' || key == 'p') {
            cycle_fps();
        } else if (key == 'F' || key == 'f') {
            cycle_filter();
        } else if (key == 'S' || key == 's') {
            take_snapshot();
        } else if (key == 'L' || key == 'l') {
            toggle_scaling_filter();
        } else if (key == 'K' || key == 'k') {
            cycle_capture_compression();
        } else if (key == VK_OEM_PLUS || key == VK_ADD) {
            audioEngine_->set_volume(audioEngine_->volume() + 0.05f);
        } else if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
            audioEngine_->set_volume(audioEngine_->volume() - 0.05f);
        } else if (key == 'M' || key == 'm') {
            audioEngine_->set_muted(!audioEngine_->is_muted());
        } else if (key == 'I' || key == 'i') {
            if (audioEngine_->cycle_input_device()) {
                std::wcout << L"[CapApp] Audio input -> "
                           << audioEngine_->selected_input_device_name() << L"\n";
            } else {
                std::cerr << "[CapApp Warning] No other active audio input is available.\n";
            }
        }
        save_settings();
    };
    window_->onMouseClick = [this](int32_t x, int32_t y) {
        constexpr int rowTop = 100;
        constexpr int rowHeight = 40;
        constexpr int optionCount = 11;
        if (!options_.showHud) return;
        if (optionPanelOpen_ && x >= 604 && x <= 932 && y >= 100) {
            constexpr int choiceTop = 106;
            constexpr int choiceHeight = 38;
            const int visibleRow = (y - choiceTop) / choiceHeight;
            const auto choices = home_option_choices(selectedHomeOption_);
            const int start = std::max(0, std::min(
                selectedOptionChoice_ - 5,
                static_cast<int>(choices.size()) - 10));
            const int choice = start + visibleRow;
            if (y >= choiceTop && choice >= 0 &&
                choice < static_cast<int>(choices.size())) {
                selectedOptionChoice_ = choice;
                apply_option_choice(selectedHomeOption_, static_cast<size_t>(choice));
                optionPanelOpen_ = false;
                save_settings();
            }
        } else if (x >= 24 && x <= 576 && y >= rowTop &&
                   y < rowTop + rowHeight * optionCount) {
            const int option = (y - rowTop) / rowHeight;
            select_home_option(option);
            open_option_panel();
        }
    };

    lastFpsUpdateNs_ = audio::WasapiAudioEngine::query_system_clock_ns();
    std::cout << "[CapApp] Engine ready. Launching interactive presentation loop.\n";
    return true;
}

void CapApplication::toggle_hud() noexcept {
    options_.showHud = !options_.showHud;
}

void CapApplication::toggle_pause() noexcept {
    isPaused_ = !isPaused_;
}

void CapApplication::cycle_camera() noexcept {
    if (cameraSelector_) {
        cameraSelector_->cycle_next_device();
    }
}

void CapApplication::cycle_resolution() noexcept {
    if (cameraSelector_) {
        auto res = cameraSelector_->cycle_resolution();
        std::cout << "[CapApp] Toggled Resolution -> " << res.name << " (" << res.width << "x" << res.height << ")\n";
    }
}

void CapApplication::cycle_fps() noexcept {
    if (cameraSelector_) {
        uint32_t fps = cameraSelector_->cycle_fps();
        std::cout << "[CapApp] Toggled FPS Target -> " << fps << " FPS\n";
    }
}

void CapApplication::cycle_filter() noexcept {
    uint8_t nextMode = (static_cast<uint8_t>(activeFilter_) + 1) % 4;
    activeFilter_ = static_cast<ui::FilterMode>(nextMode);
    std::cout << "[CapApp] Post-Processing Shader FX -> " << ui::filter_mode_name(activeFilter_) << "\n";
}

void CapApplication::toggle_scaling_filter() noexcept {
    useLanczosScaling_ = !useLanczosScaling_;
    std::cout << "[CapApp] Scaling filter -> "
              << (useLanczosScaling_ ? "Lanczos-3" : "Smooth mipmap") << "\n";
}

void CapApplication::cycle_capture_compression(int direction) noexcept {
    const auto* device = cameraSelector_ ? cameraSelector_->active_device() : nullptr;
    if (!device || device->supportedCompressions.empty()) {
        std::cerr << "[CapApp Warning] No capture compression modes are available.\n";
        return;
    }

    const auto& modes = device->supportedCompressions;
    const auto current = std::find(modes.begin(), modes.end(), captureCompression_);
    const size_t currentIndex = current == modes.end()
        ? (direction < 0 ? 0 : modes.size() - 1)
        : static_cast<size_t>(current - modes.begin());
    const size_t nextIndex = direction < 0
        ? (currentIndex + modes.size() - 1) % modes.size()
        : (currentIndex + 1) % modes.size();
    captureCompression_ = modes[nextIndex];

    const auto resolution = cameraSelector_->active_resolution();
    capture::CaptureDeviceConfig cfg{};
    cfg.targetWidth = resolution.width;
    cfg.targetHeight = resolution.height;
    cfg.targetFps = cameraSelector_->active_fps();
    cfg.preferredFormat = device->preferredFormat;
    cfg.compression = captureCompression_;
    cfg.deviceName = device->name;
    cfg.enableZeroCopy = true;
    queue_capture_reconfiguration({device->symbolicLink, std::move(cfg)});
    std::cout << "[CapApp] Requested capture compression: "
              << capture::capture_compression_name(captureCompression_) << "\n";
}

void CapApplication::take_snapshot() noexcept {
    std::lock_guard lock(lastFrameMutex_);
    if (frameRecorder_ && lastFrame_.data && lastFrame_.width > 0) {
        frameRecorder_->capture_snapshot(lastFrame_);
        std::cout << "[CapApp] Captured snapshot frame " << lastFrame_.timestampNs << "\n";
    }
}

void CapApplication::select_home_option(int option) noexcept {
    constexpr int optionCount = 11;
    selectedHomeOption_ = (option % optionCount + optionCount) % optionCount;
    optionPanelOpen_ = false;
}

std::vector<std::wstring> CapApplication::home_option_choices(int option) const {
    const auto widen = [](const std::string& value) {
        return std::wstring(value.begin(), value.end());
    };
    std::vector<std::wstring> choices;
    switch (option) {
        case 0:
            if (cameraSelector_) {
                for (const auto& device : cameraSelector_->devices()) {
                    choices.push_back(widen(device.name));
                }
            }
            break;
        case 1:
            choices = {L"1920 x 1080", L"2560 x 1440", L"3840 x 2160"};
            if (cameraSelector_) {
                const auto resolution = cameraSelector_->active_resolution();
                if (std::find(choices.begin(), choices.end(),
                    std::to_wstring(resolution.width) + L" x " +
                    std::to_wstring(resolution.height)) == choices.end()) {
                    choices.push_back(std::to_wstring(resolution.width) + L" x " +
                                      std::to_wstring(resolution.height) + L" (Custom)");
                }
            }
            break;
        case 2:
            choices = {L"30 FPS", L"60 FPS"};
            if (cameraSelector_ && cameraSelector_->active_fps() != 30 &&
                cameraSelector_->active_fps() != 60) {
                choices.push_back(std::to_wstring(cameraSelector_->active_fps()) + L" FPS");
            }
            break;
        case 3:
            if (cameraSelector_) {
                const auto* device = cameraSelector_->active_device();
                if (device) {
                    for (const auto mode : device->supportedCompressions) {
                        choices.push_back(widen(capture::capture_compression_name(mode)));
                    }
                }
            }
            break;
        case 4:
            choices = {L"Passthrough (Direct)", L"CRT Scanlines",
                       L"Sharpening", L"High Contrast"};
            break;
        case 5:
            choices = {L"Smooth", L"Lanczos-3"};
            break;
        case 6:
            for (const auto& device : audioEngine_->input_devices()) {
                choices.push_back(device.name);
            }
            break;
        case 7:
            for (const auto& device : audioEngine_->output_devices()) {
                choices.push_back(device.name);
            }
            break;
        case 8:
            for (int volume = 0; volume <= 100; volume += 10) {
                choices.push_back(std::to_wstring(volume) + L"%");
            }
            break;
        case 9:
            choices = {L"Unmuted", L"Muted"};
            break;
        case 10:
            choices = {L"Windowed", L"Fullscreen", L"Native size (fit screen)"};
            break;
    }
    return choices;
}

int CapApplication::current_option_choice(int option) const {
    switch (option) {
        case 0:
            return cameraSelector_ ? static_cast<int>(cameraSelector_->active_index()) : -1;
        case 1: {
            if (!cameraSelector_) return -1;
            const auto resolution = cameraSelector_->active_resolution();
            if (resolution.width == 1920 && resolution.height == 1080) return 0;
            if (resolution.width == 2560 && resolution.height == 1440) return 1;
            if (resolution.width == 3840 && resolution.height == 2160) return 2;
            return 3;
        }
        case 2:
            if (!cameraSelector_) return -1;
            if (cameraSelector_->active_fps() == 30) return 0;
            if (cameraSelector_->active_fps() == 60) return 1;
            return 2;
        case 3: {
            if (!cameraSelector_) return -1;
            const auto* device = cameraSelector_->active_device();
            if (!device) return -1;
            const auto mode = std::find(device->supportedCompressions.begin(),
                                        device->supportedCompressions.end(), captureCompression_);
            return mode == device->supportedCompressions.end()
                ? -1 : static_cast<int>(mode - device->supportedCompressions.begin());
        }
        case 4:
            return static_cast<int>(activeFilter_);
        case 5:
            return useLanczosScaling_ ? 1 : 0;
        case 6: {
            const auto devices = audioEngine_->input_devices();
            const auto id = audioEngine_->selected_input_device_id();
            const auto device = std::find_if(devices.begin(), devices.end(),
                [&id](const audio::AudioInputDevice& input) { return input.id == id; });
            return device == devices.end() ? -1 : static_cast<int>(device - devices.begin());
        }
        case 7: {
            const auto devices = audioEngine_->output_devices();
            const auto id = audioEngine_->selected_output_device_id();
            const auto device = std::find_if(devices.begin(), devices.end(),
                [&id](const audio::AudioInputDevice& output) { return output.id == id; });
            return device == devices.end() ? -1 : static_cast<int>(device - devices.begin());
        }
        case 8:
            return static_cast<int>(std::lround(audioEngine_->volume() * 10.0f));
        case 9:
            return audioEngine_->is_muted() ? 1 : 0;
        case 10:
            return window_->is_fullscreen() ? 1 : 0;
        default:
            return -1;
    }
}

void CapApplication::open_option_panel() {
    const auto choices = home_option_choices(selectedHomeOption_);
    if (choices.empty()) return;
    const int current = current_option_choice(selectedHomeOption_);
    selectedOptionChoice_ = current >= 0 && current < static_cast<int>(choices.size())
        ? current : 0;
    optionPanelOpen_ = true;
}

void CapApplication::apply_option_choice(int option, size_t choice) {
    const auto choices = home_option_choices(option);
    if (choice >= choices.size()) return;
    switch (option) {
        case 0:
            if (cameraSelector_) cameraSelector_->select_device(static_cast<uint32_t>(choice));
            break;
        case 1: {
            constexpr std::array<std::pair<uint32_t, uint32_t>, 3> resolutions{{
                {1920, 1080}, {2560, 1440}, {3840, 2160}
            }};
            if (cameraSelector_) {
                if (choice < resolutions.size()) {
                    cameraSelector_->set_resolution(resolutions[choice].first,
                                                    resolutions[choice].second);
                } else {
                    const auto current = cameraSelector_->active_resolution();
                    cameraSelector_->set_resolution(current.width, current.height);
                }
            }
            break;
        }
        case 2:
            if (cameraSelector_) {
                if (choice == 0) cameraSelector_->set_fps(30);
                else if (choice == 1) cameraSelector_->set_fps(60);
            }
            break;
        case 3: {
            const auto* device = cameraSelector_ ? cameraSelector_->active_device() : nullptr;
            if (!device || choice >= device->supportedCompressions.size()) break;
            captureCompression_ = device->supportedCompressions[choice];
            const auto resolution = cameraSelector_->active_resolution();
            capture::CaptureDeviceConfig config{};
            config.targetWidth = resolution.width;
            config.targetHeight = resolution.height;
            config.targetFps = cameraSelector_->active_fps();
            config.preferredFormat = device->preferredFormat;
            config.compression = captureCompression_;
            config.deviceName = device->name;
            config.enableZeroCopy = true;
            queue_capture_reconfiguration({device->symbolicLink, std::move(config)});
            break;
        }
        case 4:
            activeFilter_ = static_cast<ui::FilterMode>(choice);
            break;
        case 5:
            useLanczosScaling_ = choice == 1;
            break;
        case 6: {
            const auto devices = audioEngine_->input_devices();
            if (choice < devices.size()) audioEngine_->select_input_device_id(devices[choice].id);
            break;
        }
        case 7: {
            const auto devices = audioEngine_->output_devices();
            if (choice < devices.size() &&
                !audioEngine_->select_output_device_id(devices[choice].id)) {
                std::cerr << "[CapApp Warning] Could not switch to the selected audio output.\n";
            }
            break;
        }
        case 8:
            audioEngine_->set_volume(static_cast<float>(choice) / 10.0f);
            break;
        case 9:
            audioEngine_->set_muted(choice == 1);
            break;
        case 10:
            if (choice == 2) {
                uint32_t frameWidth = 0;
                uint32_t frameHeight = 0;
                {
                    std::lock_guard frameLock(lastFrameMutex_);
                    frameWidth = lastFrame_.width;
                    frameHeight = lastFrame_.height;
                }
                if (frameWidth == 0 || frameHeight == 0) {
                    std::cerr << "[CapApp Warning] Cannot resize to native capture size before a frame is available.\n";
                    break;
                }
                if (window_->is_fullscreen()) window_->toggle_fullscreen();
                if (!window_->resize_to_client_size(frameWidth, frameHeight)) {
                    std::cerr << "[CapApp Error] Could not resize the window to the native capture size.\n";
                } else {
                    std::cout << "[CapApp] Window resized to "
                              << window_->width() << "x" << window_->height()
                              << " client pixels.\n";
                }
            } else if ((choice == 1) != window_->is_fullscreen()) {
                window_->toggle_fullscreen();
            }
            break;
    }
    save_settings();
}

int CapApplication::run() {
    auto lastFrameTime = std::chrono::steady_clock::now();
    uint64_t framesInInterval = 0;
    uint64_t captureCallsInInterval = 0;
    uint64_t renderCallsInInterval = 0;
    double captureTimeInIntervalMs = 0.0;
    double renderTimeInIntervalMs = 0.0;
    bool audioCaptureWarningLogged = false;

    while (!window_->should_close()) {
        window_->poll_events();

        if (!audioCaptureWarningLogged && !audioEngine_->device_capture_available()) {
            const std::string captureError = audioEngine_->capture_error();
            if (!captureError.empty()) {
                std::cerr << "[CapApp Warning] " << captureError << "\n";
                audioCaptureWarningLogged = true;
            }
        }

        if (!isPaused_) {
            Frame frame{};
            bool hasFrame = false;
            if (!captureReconfiguring_.load(std::memory_order_acquire)) {
                std::unique_lock captureLock(captureDeviceMutex_, std::try_to_lock);
                if (captureLock.owns_lock() &&
                    !captureReconfiguring_.load(std::memory_order_acquire)) {
                    captureStreamInfo_ = captureDev_->stream_info();
                    const auto captureStart = std::chrono::steady_clock::now();
                    hasFrame = captureDev_->acquire_frame(frame);
                    captureTimeInIntervalMs += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - captureStart).count();
                    ++captureCallsInInterval;
                    if (hasFrame) {
                        {
                            std::lock_guard frameLock(lastFrameMutex_);
                            lastFrame_ = frame;
                        }
                        avSync_->push_video_frame(frame);
                        captureDev_->release_frame(frame);
                        ++framesInInterval;
                    }
                }
            }
            if (!hasFrame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AudioBuffer audioBuf{};
            while (audioEngine_->pop_captured_buffer(audioBuf)) {
                avSync_->push_audio_buffer(audioBuf);
            }

            Frame pairedVideo{};
            AudioBuffer pairedAudio{};
            avSync_->get_synchronized_pair(pairedVideo, pairedAudio);

            const auto renderStart = std::chrono::steady_clock::now();
            render_frame();
            renderTimeInIntervalMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - renderStart).count();
            ++renderCallsInInterval;
            frameCount_++;
        } else {
            AudioBuffer discardedAudio{};
            while (audioEngine_->pop_captured_buffer(discardedAudio)) {}
        }

        // FPS calculation every 500ms
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
        if (elapsedMs >= 500) {
            currentFps_ = (framesInInterval * 1000.0) / elapsedMs;
            captureAcquireMs_ = captureCallsInInterval
                ? captureTimeInIntervalMs / captureCallsInInterval : 0.0;
            renderFrameMs_ = renderCallsInInterval
                ? renderTimeInIntervalMs / renderCallsInInterval : 0.0;
            framesInInterval = 0;
            captureCallsInInterval = 0;
            renderCallsInInterval = 0;
            captureTimeInIntervalMs = 0.0;
            renderTimeInIntervalMs = 0.0;
            lastFrameTime = now;
        }

        if (isPaused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }

    std::cout << "[CapApp] Shutting down application...\n";
    save_settings();
    stop_capture_reconfiguration_worker();
    {
        std::lock_guard lock(captureDeviceMutex_);
        if (captureDev_) captureDev_->close();
    }
    audioEngine_->stop();
    vulkanCtx_->shutdown();
    window_->destroy();
    return 0;
}

void CapApplication::render_frame() {
#if defined(_WIN32)
    HWND hwnd = window_->hwnd();
    if (!hwnd) return;

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) return;

    std::lock_guard frameLock(lastFrameMutex_);
    if (d3dRenderer_) {
        if (d3dRenderer_->render(
                lastFrame_, options_.showHud || showNegotiatedInfo_,
                [this](HDC hudDc, int hudWidth, int hudHeight) {
                    render_hud(hudDc, hudWidth, hudHeight);
                }, useLanczosScaling_)) {
            return;
        }
        std::cerr << "[CapApp Warning] GPU presentation failed; switching to GDI fallback.\n";
        d3dRenderer_.reset();
    }

    HDC hdc = GetDC(hwnd);

    // Recreate backbuffer on resize
    if (!memDC_ || width != backbufferWidth_ || height != backbufferHeight_) {
        if (memDC_) {
            SelectObject(memDC_, oldBitmap_);
            DeleteObject(memBitmap_);
            DeleteDC(memDC_);
        }
        memDC_ = CreateCompatibleDC(hdc);
        memBitmap_ = CreateCompatibleBitmap(hdc, width, height);
        oldBitmap_ = reinterpret_cast<HBITMAP>(SelectObject(memDC_, memBitmap_));
        backbufferWidth_ = width;
        backbufferHeight_ = height;
    }

    // ----------------------------------------------------------------
    // 1. Draw camera frame (or dark placeholder if no frame available)
    // ----------------------------------------------------------------
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC_, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (lastFrame_.data && lastFrame_.width > 0 && lastFrame_.height > 0) {
        const uint64_t sourceWidth = lastFrame_.width;
        const uint64_t sourceHeight = lastFrame_.height;
        int destWidth = width;
        int destHeight = height;

        if (static_cast<uint64_t>(width) * sourceHeight >
            static_cast<uint64_t>(height) * sourceWidth) {
            destWidth = static_cast<int>(static_cast<uint64_t>(height) * sourceWidth / sourceHeight);
            if (destWidth == 0) destWidth = 1;
        } else {
            destHeight = static_cast<int>(static_cast<uint64_t>(width) * sourceHeight / sourceWidth);
            if (destHeight == 0) destHeight = 1;
        }

        const int destX = (width - destWidth) / 2;
        const int destY = (height - destHeight) / 2;

        // Fit the frame inside the client area to preserve its aspect ratio.
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = static_cast<LONG>(lastFrame_.width);
        // Negative height → top-down DIB (MF delivers top-down rows)
        bmi.bmiHeader.biHeight      = -static_cast<LONG>(lastFrame_.height);
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage   = 0;

        SetStretchBltMode(memDC_, COLORONCOLOR);
        StretchDIBits(
            memDC_,
            destX, destY, destWidth, destHeight,
            0, 0,
            static_cast<int>(lastFrame_.width),
            static_cast<int>(lastFrame_.height),       // source rect
            lastFrame_.data,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);
    } else {
        // No frame yet — draw dark background + animated grid as a placeholder
        HBRUSH bgBrush = CreateSolidBrush(RGB(15, 23, 42)); // Slate 900
        FillRect(memDC_, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(30, 41, 59));
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(memDC_, gridPen));
        for (int x = 0; x < width; x += 40) {
            MoveToEx(memDC_, x, 0, nullptr);
            LineTo(memDC_, x, height);
        }
        for (int y = 0; y < height; y += 40) {
            MoveToEx(memDC_, 0, y, nullptr);
            LineTo(memDC_, width, y);
        }
        SelectObject(memDC_, oldPen);
        DeleteObject(gridPen);

        // Moving scanline
        int scanY = static_cast<int>((frameCount_ * 6) % static_cast<uint64_t>(height));
        HPEN scanPen = CreatePen(PS_SOLID, 3, RGB(56, 189, 248));
        oldPen = reinterpret_cast<HPEN>(SelectObject(memDC_, scanPen));
        MoveToEx(memDC_, 0, scanY, nullptr);
        LineTo(memDC_, width, scanY);
        SelectObject(memDC_, oldPen);
        DeleteObject(scanPen);
    }

    // ----------------------------------------------------------------
    // 2. HUD overlay (always on top of frame)
    // ----------------------------------------------------------------
    if (options_.showHud || showNegotiatedInfo_) {
        render_hud(memDC_, width, height);
    }

    // ----------------------------------------------------------------
    // 3. Present: blit backbuffer to window DC
    // ----------------------------------------------------------------
    BitBlt(hdc, 0, 0, width, height, memDC_, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, hdc);
#endif
}

void CapApplication::render_hud(HDC hdc, int width, int height) {
#if defined(_WIN32)
    (void)width;
    (void)height;

    SetBkMode(hdc, TRANSPARENT);
    if (!options_.showHud) {
        if (!showNegotiatedInfo_) return;
        RECT monitorPanel{16, 16, 584, 184};
        HBRUSH monitorBrush = CreateSolidBrush(RGB(18, 25, 39));
        HPEN monitorPen = CreatePen(PS_SOLID, 1, RGB(47, 64, 91));
        HBRUSH oldMonitorBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, monitorBrush));
        HPEN oldMonitorPen = reinterpret_cast<HPEN>(SelectObject(hdc, monitorPen));
        RoundRect(hdc, monitorPanel.left, monitorPanel.top,
                  monitorPanel.right, monitorPanel.bottom, 20, 20);
        SelectObject(hdc, oldMonitorBrush);
        SelectObject(hdc, oldMonitorPen);
        DeleteObject(monitorBrush);
        DeleteObject(monitorPen);

        HFONT monitorFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldMonitorFont = reinterpret_cast<HFONT>(SelectObject(hdc, monitorFont));
        render_monitoring_info(hdc, 30);
        SetTextColor(hdc, RGB(132, 154, 183));
        constexpr const wchar_t* monitorControls = L"Tab hide  -  Esc menu";
        TextOutW(hdc, 38, 158, monitorControls,
                 static_cast<int>(wcslen(monitorControls)));
        SelectObject(hdc, oldMonitorFont);
        DeleteObject(monitorFont);
        return;
    }

    RECT panel{16, 16, 584, showNegotiatedInfo_ ? 730 : 584};
    HBRUSH panelBrush = CreateSolidBrush(RGB(18, 25, 39));
    HPEN panelPen = CreatePen(PS_SOLID, 1, RGB(47, 64, 91));
    HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, panelBrush));
    HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, panelPen));
    RoundRect(hdc, panel.left, panel.top, panel.right, panel.bottom, 20, 20);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(panelBrush);
    DeleteObject(panelPen);

    SetTextColor(hdc, RGB(115, 218, 255));
    HFONT titleFont = CreateFontW(25, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, titleFont));
    TextOutW(hdc, 36, 31, L"CAPTURE HOME", 12);
    SetTextColor(hdc, RGB(166, 184, 209));
    HFONT subFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, subFont);
    constexpr const wchar_t* subtitle = L"Choose a setting  -  → / Enter opens options";
    TextOutW(hdc, 38, 63, subtitle, static_cast<int>(wcslen(subtitle)));

    HFONT bodyFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, bodyFont);

    const auto* activeCam = cameraSelector_ ? cameraSelector_->active_device() : nullptr;
    const auto widen = [](const std::string& value) {
        return std::wstring(value.begin(), value.end());
    };
    const auto resolution = cameraSelector_ ? cameraSelector_->active_resolution()
                                            : capture::ResolutionPreset{};
    std::array<std::wstring, 11> values{
        activeCam ? widen(activeCam->name) : L"No camera",
        std::to_wstring(resolution.width) + L" x " + std::to_wstring(resolution.height),
        std::to_wstring(cameraSelector_ ? cameraSelector_->active_fps() : options_.targetFps) + L" FPS",
        widen(capture::capture_compression_name(captureCompression_)),
        widen(std::string(ui::filter_mode_name(activeFilter_))),
        useLanczosScaling_ ? L"Lanczos-3" : L"Smooth",
        audioEngine_->selected_input_device_name().empty()
            ? L"No audio input" : audioEngine_->selected_input_device_name(),
        audioEngine_->selected_output_device_name().empty()
            ? L"No audio output" : audioEngine_->selected_output_device_name(),
        std::to_wstring(static_cast<int>(std::lround(audioEngine_->volume() * 100.0f))) + L"%",
        audioEngine_->is_muted() ? L"Muted" : L"Unmuted",
        window_->is_fullscreen() ? L"Fullscreen" : L"Windowed"
    };
    const std::array<const wchar_t*, 11> labels{
        L"VIDEO INPUT", L"RESOLUTION", L"FRAME RATE", L"CAPTURE FORMAT", L"PICTURE FILTER",
        L"SCALING", L"AUDIO INPUT", L"AUDIO OUTPUT", L"VOLUME", L"AUDIO MUTE", L"DISPLAY"
    };
    constexpr int rowTop = 100;
    constexpr int rowHeight = 40;
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        const int y = rowTop + i * rowHeight;
        RECT row{28, y, 572, y + 36};
        const bool focused = i == selectedHomeOption_;
        HBRUSH rowBrush = CreateSolidBrush(focused ? RGB(37, 61, 91) : RGB(25, 35, 52));
        HPEN rowPen = CreatePen(PS_SOLID, focused ? 2 : 1,
                                focused ? RGB(71, 190, 245) : RGB(39, 53, 74));
        HBRUSH previousBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, rowBrush));
        HPEN previousPen = reinterpret_cast<HPEN>(SelectObject(hdc, rowPen));
        RoundRect(hdc, row.left, row.top, row.right, row.bottom, 9, 9);
        SelectObject(hdc, previousBrush);
        SelectObject(hdc, previousPen);
        DeleteObject(rowBrush);
        DeleteObject(rowPen);

        SetTextColor(hdc, focused ? RGB(120, 221, 255) : RGB(153, 171, 196));
        TextOutW(hdc, 42, y + 10, labels[i], static_cast<int>(wcslen(labels[i])));

        HPEN radioPen = CreatePen(PS_SOLID, 2, RGB(84, 199, 245));
        HBRUSH radioBrush = CreateSolidBrush(focused ? RGB(37, 61, 91) : RGB(25, 35, 52));
        HPEN previousRadioPen = reinterpret_cast<HPEN>(SelectObject(hdc, radioPen));
        HBRUSH previousRadioBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, radioBrush));
        Ellipse(hdc, 263, y + 10, 279, y + 26);
        HBRUSH dot = CreateSolidBrush(RGB(84, 199, 245));
        HBRUSH previousDot = reinterpret_cast<HBRUSH>(SelectObject(hdc, dot));
        Ellipse(hdc, 268, y + 15, 274, y + 21);
        SelectObject(hdc, previousDot);
        DeleteObject(dot);
        SelectObject(hdc, previousRadioBrush);
        SelectObject(hdc, previousRadioPen);
        DeleteObject(radioBrush);
        DeleteObject(radioPen);

        SetTextColor(hdc, RGB(239, 246, 255));
        RECT valueRect{292, y + 5, 556, y + 31};
        DrawTextW(hdc, values[i].c_str(), -1, &valueRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_LEFT);
    }

    if (optionPanelOpen_) {
        RECT optionPanel{596, 16, 944, 544};
        HBRUSH optionPanelBrush = CreateSolidBrush(RGB(18, 25, 39));
        HPEN optionPanelPen = CreatePen(PS_SOLID, 1, RGB(47, 64, 91));
        HBRUSH oldOptionBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, optionPanelBrush));
        HPEN oldOptionPen = reinterpret_cast<HPEN>(SelectObject(hdc, optionPanelPen));
        RoundRect(hdc, optionPanel.left, optionPanel.top,
                  optionPanel.right, optionPanel.bottom, 20, 20);
        SelectObject(hdc, oldOptionBrush);
        SelectObject(hdc, oldOptionPen);
        DeleteObject(optionPanelBrush);
        DeleteObject(optionPanelPen);

        SetTextColor(hdc, RGB(115, 218, 255));
        SelectObject(hdc, titleFont);
        TextOutW(hdc, 620, 31, L"OPTIONS", 7);
        SetTextColor(hdc, RGB(166, 184, 209));
        SelectObject(hdc, subFont);
        TextOutW(hdc, 622, 63, labels[selectedHomeOption_],
                 static_cast<int>(wcslen(labels[selectedHomeOption_])));

        const auto choices = home_option_choices(selectedHomeOption_);
        const int currentChoice = current_option_choice(selectedHomeOption_);
        constexpr int choiceTop = 106;
        constexpr int choiceHeight = 38;
        constexpr int visibleChoices = 10;
        const int firstChoice = std::max(0, std::min(
            selectedOptionChoice_ - visibleChoices / 2,
            static_cast<int>(choices.size()) - visibleChoices));
        const int lastChoice = std::min(firstChoice + visibleChoices,
                                        static_cast<int>(choices.size()));
        for (int choice = firstChoice; choice < lastChoice; ++choice) {
            const int row = choice - firstChoice;
            const int y = choiceTop + row * choiceHeight;
            RECT choiceRect{608, y, 932, y + 34};
            const bool focused = choice == selectedOptionChoice_;
            const bool checked = choice == currentChoice;
            HBRUSH choiceBrush = CreateSolidBrush(
                focused ? RGB(37, 61, 91) : RGB(25, 35, 52));
            HPEN choicePen = CreatePen(PS_SOLID, focused ? 2 : 1,
                focused ? RGB(71, 190, 245) : RGB(39, 53, 74));
            HBRUSH previousBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, choiceBrush));
            HPEN previousPen = reinterpret_cast<HPEN>(SelectObject(hdc, choicePen));
            RoundRect(hdc, choiceRect.left, choiceRect.top,
                      choiceRect.right, choiceRect.bottom, 8, 8);
            SelectObject(hdc, previousBrush);
            SelectObject(hdc, previousPen);
            DeleteObject(choiceBrush);
            DeleteObject(choicePen);

            HPEN radioPen = CreatePen(PS_SOLID, 2, checked ? RGB(84, 199, 245) : RGB(112, 132, 160));
            HBRUSH radioBrush = CreateSolidBrush(focused ? RGB(37, 61, 91) : RGB(25, 35, 52));
            HPEN previousRadioPen = reinterpret_cast<HPEN>(SelectObject(hdc, radioPen));
            HBRUSH previousRadioBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, radioBrush));
            Ellipse(hdc, 624, y + 9, 640, y + 25);
            if (checked) {
                HBRUSH dotBrush = CreateSolidBrush(RGB(84, 199, 245));
                HBRUSH previousDotBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, dotBrush));
                Ellipse(hdc, 629, y + 14, 635, y + 20);
                SelectObject(hdc, previousDotBrush);
                DeleteObject(dotBrush);
            }
            SelectObject(hdc, previousRadioBrush);
            SelectObject(hdc, previousRadioPen);
            DeleteObject(radioBrush);
            DeleteObject(radioPen);

            SetTextColor(hdc, focused ? RGB(239, 246, 255) : RGB(184, 199, 220));
            RECT choiceText{652, y + 4, 916, y + 30};
            DrawTextW(hdc, choices[choice].c_str(), -1, &choiceText,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_LEFT);
        }
        SetTextColor(hdc, RGB(132, 154, 183));
        SelectObject(hdc, subFont);
        constexpr const wchar_t* optionControls = L"↑/↓ choose    Enter select    ← back";
        TextOutW(hdc, 620, 511, optionControls,
                 static_cast<int>(wcslen(optionControls)));
    }

    SetTextColor(hdc, RGB(132, 154, 183));
    SelectObject(hdc, subFont);
    if (showNegotiatedInfo_) {
        render_monitoring_info(hdc, 550);
        SetTextColor(hdc, RGB(132, 154, 183));
        constexpr const wchar_t* controls = L"Esc menu    Tab toggle monitoring details";
        TextOutW(hdc, 38, 690, controls, static_cast<int>(wcslen(controls)));
    } else {
        constexpr const wchar_t* controls = L"Esc menu    Tab show monitoring details";
        TextOutW(hdc, 38, 548, controls, static_cast<int>(wcslen(controls)));
        const std::wstring status =
            (audioEngine_->device_capture_available() ? L"MIC READY" : L"MIC UNAVAILABLE") +
            std::wstring(L"   ·   ") +
            (audioEngine_->monitoring_available() ? L"MONITORING" : L"NO OUTPUT");
        SetTextColor(hdc, audioEngine_->monitoring_available()
            ? RGB(110, 231, 183) : RGB(248, 113, 113));
        TextOutW(hdc, 38, 566, status.c_str(), static_cast<int>(status.size()));
    }

    SelectObject(hdc, oldFont);
    DeleteObject(subFont);
    DeleteObject(bodyFont);
    DeleteObject(titleFont);
#endif
}

void CapApplication::render_monitoring_info(HDC hdc, int top) {
#if defined(_WIN32)
    const auto widen = [](const std::string& value) {
        return std::wstring(value.begin(), value.end());
    };
    SetTextColor(hdc, RGB(115, 218, 255));
    TextOutW(hdc, 38, top, L"NEGOTIATED / RUNNING", 20);
    SetTextColor(hdc, RGB(184, 199, 220));
    const auto sourceMode =
        std::to_wstring(captureStreamInfo_.sourceWidth) + L" x " +
        std::to_wstring(captureStreamInfo_.sourceHeight) + L" @ " +
        std::to_wstring(captureStreamInfo_.sourceFps) + L" FPS  " +
        widen(captureStreamInfo_.sourceFormat);
    TextOutW(hdc, 38, top + 20, sourceMode.c_str(), static_cast<int>(sourceMode.size()));
    const auto runningMode =
        L"Running: " + std::to_wstring(lastFrame_.width) + L" x " +
        std::to_wstring(lastFrame_.height) + L" @ " +
        std::to_wstring(currentFps_).substr(0, 5) + L" FPS  " +
        widen(captureStreamInfo_.outputFormat);
    TextOutW(hdc, 38, top + 38, runningMode.c_str(), static_cast<int>(runningMode.size()));
    const auto timings =
        L"Capture " + std::to_wstring(captureAcquireMs_).substr(0, 5) +
        L" ms    Render " + std::to_wstring(renderFrameMs_).substr(0, 5) + L" ms";
    TextOutW(hdc, 38, top + 56, timings.c_str(), static_cast<int>(timings.size()));
    const auto usb =
        L"USB: " + widen(captureStreamInfo_.usbLinkSpeed) +
        (captureStreamInfo_.usbMaxMbps
            ? L" (" + std::to_wstring(captureStreamInfo_.usbMaxMbps) + L" Mbps)"
            : L"");
    TextOutW(hdc, 38, top + 74, usb.c_str(), static_cast<int>(usb.size()));
    const auto bandwidth = captureStreamInfo_.nominalInputBandwidthKnown
        ? L"Nominal input: " +
            std::to_wstring(captureStreamInfo_.nominalInputMbps).substr(0, 6) + L" Mbps"
        : L"Nominal input bandwidth: N/A";
    TextOutW(hdc, 38, top + 92, bandwidth.c_str(), static_cast<int>(bandwidth.size()));

    const std::wstring audioStatus =
        (audioEngine_->device_capture_available() ? L"MIC READY" : L"MIC UNAVAILABLE") +
        std::wstring(L"   ·   ") +
        (audioEngine_->monitoring_available() ? L"MONITORING" : L"NO OUTPUT");
    SetTextColor(hdc, audioEngine_->monitoring_available()
        ? RGB(110, 231, 183) : RGB(248, 113, 113));
    TextOutW(hdc, 38, top + 110, audioStatus.c_str(), static_cast<int>(audioStatus.size()));
#else
    (void)hdc;
    (void)top;
#endif
}

} // namespace capapp
