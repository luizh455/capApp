#include <capapp/ui/window.hpp>
#include <algorithm>
#include <iostream>

namespace capapp::ui {

#if defined(_WIN32)
static const wchar_t* WINDOW_CLASS_NAME = L"CapApp_NativeWindowClass";

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
        case WM_KEYDOWN:
            if (self) {
                if (wParam == VK_F11) {
                    self->toggle_fullscreen();
                }
                if (self->onKeyPress) {
                    self->onKeyPress(static_cast<uint32_t>(wParam));
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (self && self->onMouseClick) {
                self->onMouseClick(
                    static_cast<int16_t>(LOWORD(lParam)),
                    static_cast<int16_t>(HIWORD(lParam)));
            }
            return 0;

        case WM_SIZE:
            if (self) {
                self->currentWidth_ = LOWORD(lParam);
                self->currentHeight_ = HIWORD(lParam);
                if (self->onResize) {
                    self->onResize(self->currentWidth_, self->currentHeight_);
                }
            }
            return 0;

        case WM_PAINT:
            if (self && self->onPaint) {
                self->onPaint();
            }
            ValidateRect(hwnd, nullptr);
            return 0;

        case WM_CLOSE:
            if (self) {
                self->request_close();
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}
#endif

NativeWindow::NativeWindow(const WindowConfig& config)
    : config_(config), currentWidth_(config.width), currentHeight_(config.height) {}

NativeWindow::~NativeWindow() {
    destroy();
}

bool NativeWindow::create() {
#if defined(_WIN32)
    hInstance_ = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = NativeWindow::window_proc;
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = WINDOW_CLASS_NAME;

    RegisterClassExW(&wc);

    RECT rect{0, 0, static_cast<LONG>(config_.width), static_cast<LONG>(config_.height)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    const int windowWidth = rect.right - rect.left;
    const int windowHeight = rect.bottom - rect.top;

    // Center window on screen
    const int screenX = (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
    const int screenY = (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

    hwnd_ = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        config_.title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        screenX, screenY,
        windowWidth, windowHeight,
        nullptr, nullptr, hInstance_, nullptr
    );

    if (!hwnd_) {
        return false;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    if (config_.startFullscreen) {
        toggle_fullscreen();
    }

    return true;
#else
    return true;
#endif
}

void NativeWindow::destroy() noexcept {
#if defined(_WIN32)
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (hInstance_) {
        UnregisterClassW(WINDOW_CLASS_NAME, hInstance_);
        hInstance_ = nullptr;
    }
#endif
    shouldClose_ = true;
}

void NativeWindow::poll_events() {
#if defined(_WIN32)
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            shouldClose_ = true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
#endif
}

bool NativeWindow::should_close() const noexcept {
    return shouldClose_;
}

void NativeWindow::request_close() noexcept {
    shouldClose_ = true;
}

void NativeWindow::toggle_fullscreen() {
#if defined(_WIN32)
    if (!hwnd_) return;

    DWORD style = GetWindowLongW(hwnd_, GWL_STYLE);

    if (!isFullscreen_) {
        MONITORINFO mi{sizeof(MONITORINFO)};
        if (GetWindowPlacement(hwnd_, &prevPlacement_) &&
            GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd_, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            isFullscreen_ = true;
        }
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &prevPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        isFullscreen_ = false;
    }
#endif
}

bool NativeWindow::is_fullscreen() const noexcept {
    return isFullscreen_;
}

bool NativeWindow::resize_to_client_size(uint32_t width, uint32_t height) {
#if defined(_WIN32)
    if (!hwnd_ || isFullscreen_ || width == 0 || height == 0) return false;
    if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);

    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;

    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    RECT frameInsets{0, 0, 0, 0};
    if (!AdjustWindowRectEx(&frameInsets, style, FALSE, extendedStyle)) return false;
    const int horizontalInsets = frameInsets.right - frameInsets.left;
    const int verticalInsets = frameInsets.bottom - frameInsets.top;
    const int availableWidth =
        monitorInfo.rcWork.right - monitorInfo.rcWork.left - horizontalInsets;
    const int availableHeight =
        monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - verticalInsets;
    if (availableWidth <= 0 || availableHeight <= 0) return false;

    const double scale = std::min({
        1.0,
        static_cast<double>(availableWidth) / width,
        static_cast<double>(availableHeight) / height
    });
    const int clientWidth = std::max(1, static_cast<int>(width * scale));
    const int clientHeight = std::max(1, static_cast<int>(height * scale));

    RECT windowRect{0, 0, clientWidth, clientHeight};
    if (!AdjustWindowRectEx(&windowRect, style, FALSE, extendedStyle)) return false;
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    const int x = monitorInfo.rcWork.left +
        ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - windowWidth) / 2;
    const int y = monitorInfo.rcWork.top +
        ((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - windowHeight) / 2;

    return SetWindowPos(hwnd_, nullptr, x, y, windowWidth, windowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED) != FALSE;
#else
    (void)width;
    (void)height;
    return false;
#endif
}

uint32_t NativeWindow::width() const noexcept {
    return currentWidth_;
}

uint32_t NativeWindow::height() const noexcept {
    return currentHeight_;
}

#if defined(_WIN32)
HWND NativeWindow::hwnd() const noexcept {
    return hwnd_;
}
#endif

} // namespace capapp::ui
