#include <gtest/gtest.h>
#include <capapp/capture/frame_recorder.hpp>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>

using namespace capapp;
using namespace capapp::capture;

TEST(CaptureDevice_Video, WhenSnapshotRequested_SavesPPMFileAsynchronously) {
    FrameRecorder recorder;
    ASSERT_TRUE(recorder.start());
    EXPECT_TRUE(recorder.is_active());

    uint32_t w = 8;
    uint32_t h = 8;
    std::vector<uint8_t> pixels(w * h * 4, 200);

    Frame frame{};
    frame.width = w;
    frame.height = h;
    frame.timestampNs = 1'234'567'890ULL;
    frame.data = pixels.data();
    frame.format = PixelFormat::BGRA8;

    std::string testFile = "test_output_snapshot.ppm";
    EXPECT_TRUE(recorder.capture_snapshot(frame, testFile));

    // Wait briefly for background thread processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    recorder.stop();
    EXPECT_FALSE(recorder.is_active());

    FrameRecorderStats stats = recorder.stats();
    EXPECT_EQ(stats.totalSnapshotsRequested, 1u);
    EXPECT_EQ(stats.totalSnapshotsSaved, 1u);
    EXPECT_EQ(stats.totalFailedSaves, 0u);
    EXPECT_EQ(stats.lastSavedPath, testFile);

    // Verify file exists on disk
    std::ifstream checkFile(testFile, std::ios::binary);
    EXPECT_TRUE(checkFile.is_open());
    checkFile.close();

    // Clean up test file
    std::remove(testFile.c_str());
}
