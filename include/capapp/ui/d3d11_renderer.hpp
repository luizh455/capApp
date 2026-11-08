#pragma once

#include <capapp/frame.hpp>
#include <cstdint>
#include <functional>
#include <memory>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace capapp::ui {

enum class ScalingMode : uint8_t {
    None = 0,
    Smooth,
    Lanczos3
};

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

#if defined(_WIN32)
    bool initialize(HWND window);
    bool render(const Frame& frame, bool showHud,
                const std::function<void(HDC, int, int)>& drawHud,
                bool useLanczos = false, bool noScaling = false);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace capapp::ui
