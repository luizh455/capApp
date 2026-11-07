#include <gtest/gtest.h>
#include <capapp/ui/window.hpp>
#include <capapp/app.hpp>

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
