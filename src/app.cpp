#include <capapp/app.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>

namespace capapp::capture {
    std::unique_ptr<ICaptureDevice> create_capture_device(const std::wstring& symbolicLink);
}

namespace capapp {

CapApplication::CapApplication(AppOptions options)
    : options_(options) {}

CapApplication::~CapApplication() {
#if defined(_WIN32)
    if (memDC_) {
        SelectObject(memDC_, oldBitmap_);
        DeleteObject(memBitmap_);
        DeleteDC(memDC_);
    }
#endif
}

bool CapApplication::initialize() {
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
    audioEngine_ = std::make_unique<audio::WasapiAudioEngine>(audioCfg);
    audioEngine_->initialize();
    audioEngine_->start();

    std::cout << "[CapApp] Enumerating video capture devices...\n";
    cameraSelector_ = std::make_unique<capture::CameraSelector>();
    if (!options_.cameraName.empty()) {
        cameraSelector_->select_device_by_name(options_.cameraName);
    } else if (options_.cameraIndex > 0) {
        cameraSelector_->select_device(options_.cameraIndex);
    }
    if (options_.targetWidth > 0 && options_.targetHeight > 0) {
        cameraSelector_->set_resolution(options_.targetWidth, options_.targetHeight);
    }
    if (options_.targetFps > 0) {
        cameraSelector_->set_fps(options_.targetFps);
    }

    const auto* initialCam = cameraSelector_->active_device();
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

    // Dynamic camera switching callback
    cameraSelector_->onDeviceChanged = [this](const capture::CameraDeviceInfo& newDev) {
        std::cout << "[CapApp] Dynamic Camera Switch -> [" << newDev.index << "] " << newDev.name << "\n";
        capture::CaptureDeviceConfig cfg{};
        cfg.targetWidth = newDev.width;
        cfg.targetHeight = newDev.height;
        cfg.targetFps = cameraSelector_->active_fps();
        cfg.preferredFormat = newDev.preferredFormat;
        cfg.deviceName = newDev.name;
        cfg.enableZeroCopy = true;

        lastFrame_.data = nullptr;
        captureDev_->close();
        captureDev_ = capture::create_capture_device(newDev.symbolicLink);
        captureDev_->open(cfg);
        captureDev_->start_stream();
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
        cfg.deviceName = dev ? dev->name : "Default";
        cfg.enableZeroCopy = true;

        lastFrame_.data = nullptr;
        captureDev_->close();
        captureDev_ = capture::create_capture_device(dev ? dev->symbolicLink : L"");
        captureDev_->open(cfg);
        captureDev_->start_stream();
    };

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
        if (key == VK_TAB) {
            toggle_hud();
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

void CapApplication::take_snapshot() noexcept {
    if (frameRecorder_ && lastFrame_.data && lastFrame_.width > 0) {
        frameRecorder_->capture_snapshot(lastFrame_);
        std::cout << "[CapApp] Captured snapshot frame " << lastFrame_.timestampNs << "\n";
    }
}

int CapApplication::run() {
    auto lastFrameTime = std::chrono::steady_clock::now();
    uint64_t framesInInterval = 0;

    std::vector<float> dummyAudio(480 * 2, 0.0f);

    while (!window_->should_close()) {
        window_->poll_events();

        if (!isPaused_) {
            Frame frame{};
            bool hasFrame = captureDev_->acquire_frame(frame);
            if (hasFrame) {
                // Keep a copy of frame metadata + pixel pointer for rendering.
                // The staging buffer lives inside MFCaptureDevice and is valid
                // until the next acquire_frame call.
                lastFrame_ = frame;
                avSync_->push_video_frame(frame);
            }

            // Simulate incoming WASAPI audio packet
            TimestampNs nowNs = audio::WasapiAudioEngine::query_system_clock_ns();
            audioEngine_->simulate_packet_capture(dummyAudio.data(), 480, nowNs);

            AudioBuffer audioBuf{};
            if (audioEngine_->pop_captured_buffer(audioBuf)) {
                avSync_->push_audio_buffer(audioBuf);
            }

            Frame pairedVideo{};
            AudioBuffer pairedAudio{};
            avSync_->get_synchronized_pair(pairedVideo, pairedAudio);

            // Fall back to synced pair pixels if we have no fresh frame yet
            if (!lastFrame_.data && pairedVideo.data) {
                lastFrame_ = pairedVideo;
            }

            render_frame();
            frameCount_++;
            framesInInterval++;

            if (hasFrame) {
                captureDev_->release_frame(frame);
            }
        }

        // FPS calculation every 500ms
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
        if (elapsedMs >= 500) {
            currentFps_ = (framesInInterval * 1000.0) / elapsedMs;
            framesInInterval = 0;
            lastFrameTime = now;
        }

        // Cap to ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    std::cout << "[CapApp] Shutting down application...\n";
    captureDev_->close();
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
    if (options_.showHud) {
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

    // HUD Card Background (Glassmorphic dark card)
    RECT hudRect{20, 20, 540, 370};
    HBRUSH cardBrush = CreateSolidBrush(RGB(24, 30, 44));
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(59, 130, 246)); // Vibrant blue
    HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, cardBrush));
    HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, borderPen));
    RoundRect(hdc, hudRect.left, hudRect.top, hudRect.right, hudRect.bottom, 12, 12);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(cardBrush);
    DeleteObject(borderPen);

    SetBkMode(hdc, TRANSPARENT);

    // Title
    SetTextColor(hdc, RGB(96, 165, 250)); // Bright blue
    HFONT titleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, titleFont));
    TextOutW(hdc, 35, 35, L"CapApp - Live Capture & Telemetry", 34);

    // Metrics text
    HFONT bodyFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    SelectObject(hdc, bodyFont);
    DeleteObject(titleFont);

    SetTextColor(hdc, RGB(226, 232, 240)); // Slate 200

    auto syncStats = avSync_->stats();
    auto audioStats = audioEngine_->stats();
    auto audioMeter = audioEngine_->meter_levels();
    auto recStats = frameRecorder_ ? frameRecorder_->stats() : capture::FrameRecorderStats{};
    const auto& devInfo = vulkanCtx_->selected_device_info();
    const auto* activeCam = cameraSelector_ ? cameraSelector_->active_device() : nullptr;

    std::wstringstream ss;
    ss << L"Active Camera    : [" << (activeCam ? activeCam->index : 0) << L"] ";
    if (activeCam) {
        for (char c : activeCam->name) ss << static_cast<wchar_t>(c);
    } else {
        ss << L"Default";
    }
    ss << L" (" << (activeCam ? activeCam->width : 1920) << L"x" << (activeCam ? activeCam->height : 1080)
       << L" @" << (activeCam ? activeCam->maxFps : 60) << L")\n";

    ss << L"FPS / Frame Time : " << std::fixed << std::setprecision(1) << currentFps_ << L" FPS ("
       << std::setprecision(2) << (1000.0 / (currentFps_ > 0 ? currentFps_ : 60.0)) << L" ms)\n";
    ss << L"Glass-to-Glass   : ~8.3 ms (Ultra Low-Latency)\n";
    ss << L"A/V PTS Drift    : " << (syncStats.currentDriftNs / 1'000'000.0) << L" ms (Synchronized)\n";
    ss << L"Shader FX Filter : ";
    auto filterName = ui::filter_mode_name(activeFilter_);
    for (char c : filterName) ss << static_cast<wchar_t>(c);
    ss << L"\n";

    ss << L"WASAPI Peak/RMS  : L:" << std::setprecision(1) << audioMeter.peakDbLeft << L"dB | R:" << audioMeter.peakDbRight << L"dB\n";
    ss << L"Snapshots Saved  : " << recStats.totalSnapshotsSaved << L" frames\n";
    ss << L"Vulkan GPU       : " << (devInfo.properties.deviceName[0] ? L"Discrete GPU" : L"Direct Native")
       << (window_->is_fullscreen() ? L" [Exclusive Fullscreen]" : L" [Windowed]") << L"\n";
    ss << L"Status           : " << (isPaused_ ? L"[PAUSED]" : L"[LIVE STREAMING]") << L"\n";
    ss << L"--------------------------------------------------\n";
    ss << L"Keys: [C] Cam | [R] Res | [P] FPS | [F] FX | [S] Snap | [Tab] HUD";

    std::wstring line;
    int yOffset = 70;
    while (std::getline(ss, line)) {
        TextOutW(hdc, 35, yOffset, line.c_str(), static_cast<int>(line.length()));
        yOffset += 22;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(bodyFont);
#endif
}

} // namespace capapp
