#pragma once

#include <string>
#include <functional>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace capapp::ui {

struct WindowConfig {
    std::wstring title{L"CapApp - Ultra Low-Latency Capture Engine"};
    uint32_t width{1280};
    uint32_t height{720};
    bool startFullscreen{false};
};

class NativeWindow {
public:
    explicit NativeWindow(const WindowConfig& config = {});
    ~NativeWindow();

    // Non-copyable
    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool create();
    void destroy() noexcept;

    void poll_events();
    [[nodiscard]] bool should_close() const noexcept;
    void request_close() noexcept;

    void toggle_fullscreen();
    [[nodiscard]] bool is_fullscreen() const noexcept;
    bool resize_to_client_size(uint32_t width, uint32_t height);

    [[nodiscard]] uint32_t width() const noexcept;
    [[nodiscard]] uint32_t height() const noexcept;

#if defined(_WIN32)
    [[nodiscard]] HWND hwnd() const noexcept;
#endif

    // Event callbacks
    std::function<void(uint32_t key)> onKeyPress;
    std::function<void(int32_t x, int32_t y)> onMouseClick;
    std::function<void(uint32_t width, uint32_t height)> onResize;
    std::function<void()> onPaint;

private:
#if defined(_WIN32)
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HWND hwnd_{nullptr};
    HINSTANCE hInstance_{nullptr};
    WINDOWPLACEMENT prevPlacement_{sizeof(WINDOWPLACEMENT)};
#endif

    WindowConfig config_;
    bool shouldClose_{false};
    bool isFullscreen_{false};
    uint32_t currentWidth_{1280};
    uint32_t currentHeight_{720};
};

} // namespace capapp::ui
