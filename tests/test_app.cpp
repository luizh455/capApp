#include <gtest/gtest.h>
#include <capapp/ui/window.hpp>
#include <capapp/ui/d3d11_renderer.hpp>
#include <capapp/app.hpp>
#include <vector>

using namespace capapp;
using namespace capapp::ui;

// Naming: {Device}::{Decoder}::When{Condition}
TEST(CaptureUI_Window, WhenCreated_HasValidDimensions) {
    WindowConfig cfg{};
    cfg.width = 800;
    cfg.height = 600;
    cfg.startFullscreen = false;

    NativeWindow win(cfg);
    EXPECT_EQ(win.width(), 800u);
    EXPECT_EQ(win.height(), 600u);
    EXPECT_FALSE(win.is_fullscreen());
    EXPECT_FALSE(win.should_close());
}

TEST(CaptureUI_Window, WhenFullscreenToggled_UpdatesState) {
    WindowConfig cfg{};
    cfg.width = 640;
    cfg.height = 480;

    NativeWindow win(cfg);
    ASSERT_TRUE(win.create());
    EXPECT_FALSE(win.is_fullscreen());

    win.toggle_fullscreen();
    EXPECT_TRUE(win.is_fullscreen());

    win.toggle_fullscreen();
    EXPECT_FALSE(win.is_fullscreen());

    win.destroy();
    EXPECT_TRUE(win.should_close());
}

TEST(CaptureUI_App, WhenInitialized_RunsTelemetryPipeline) {
    AppOptions opts{};
    opts.startFullscreen = false;
    opts.showHud = true;
    opts.targetFps = 60;

    CapApplication app(opts);
    EXPECT_TRUE(app.initialize());
}

TEST(CaptureUI_D3D11Renderer, WhenFramePresented_HandlesResizeAndHud) {
    WindowConfig cfg{};
    cfg.width = 640;
    cfg.height = 480;
    NativeWindow win(cfg);
    ASSERT_TRUE(win.create());

    ui::D3D11Renderer renderer;
    ASSERT_TRUE(renderer.initialize(win.hwnd()));

    std::vector<uint8_t> pixels(8 * 4 * 4, 0x7f);
    Frame frame{};
    frame.width = 8;
    frame.height = 4;
    frame.stride = 8 * 4;
    frame.format = PixelFormat::BGRA8;
    frame.data = pixels.data();
    frame.dataSize = pixels.size();

    bool hudDrawn = false;
    EXPECT_TRUE(renderer.render(frame, true, [&hudDrawn](HDC, int width, int height) {
        hudDrawn = width >= 540 && height >= 370;
    }));
    EXPECT_TRUE(hudDrawn);

    SetWindowPos(win.hwnd(), nullptr, 0, 0, 1000, 800,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    EXPECT_TRUE(renderer.render(frame, false, {}));
}
