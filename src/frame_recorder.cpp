#include <capapp/capture/frame_recorder.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace capapp::capture {

FrameRecorder::FrameRecorder() = default;

FrameRecorder::~FrameRecorder() {
    stop();
}

bool FrameRecorder::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return true;
    }
    running_.store(true, std::memory_order_release);
    workerThread_ = std::thread(&FrameRecorder::worker_loop, this);
    return true;
}

void FrameRecorder::stop() {
    if (!running_.load(std::memory_order_relaxed)) {
        return;
    }
    running_.store(false, std::memory_order_release);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

bool FrameRecorder::capture_snapshot(const Frame& frame, std::string filename) {
    if (!frame.data || frame.width == 0 || frame.height == 0) {
        return false;
    }

    requested_.fetch_add(1, std::memory_order_relaxed);

    if (filename.empty()) {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "snapshot_" << timeT << "_" << frame.timestampNs << ".ppm";
        filename = ss.str();
    }

    PendingSnapshot item{};
    item.width = frame.width;
    item.height = frame.height;
    item.ptsNs = frame.timestampNs;
    item.filename = filename;
    item.pixelData.assign(frame.data, frame.data + (static_cast<size_t>(frame.width) * frame.height * 4));

    if (!queue_.push(std::move(item))) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    return true;
}

void FrameRecorder::worker_loop() {
    while (running_.load(std::memory_order_relaxed) || !queue_.empty()) {
        PendingSnapshot item;
        if (queue_.pop(item)) {
            if (save_ppm_image(item.filename, item.width, item.height, item.pixelData.data())) {
                saved_.fetch_add(1, std::memory_order_relaxed);
                lastSavedPath_ = item.filename;
            } else {
                failed_.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool FrameRecorder::save_ppm_image(
    const std::string& filepath, uint32_t width, uint32_t height, const uint8_t* bgraData) noexcept {
    if (!bgraData || width == 0 || height == 0) {
        return false;
    }

    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    // Write binary PPM header (P6, width, height, 255 max color)
    out << "P6\n" << width << " " << height << "\n255\n";

    // Write RGB pixels (convert BGRA -> RGB)
    std::vector<uint8_t> rgbRow(width * 3);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* bgraRow = bgraData + (y * width * 4);
        for (uint32_t x = 0; x < width; ++x) {
            rgbRow[x * 3 + 0] = bgraRow[x * 4 + 2]; // R
            rgbRow[x * 3 + 1] = bgraRow[x * 4 + 1]; // G
            rgbRow[x * 3 + 2] = bgraRow[x * 4 + 0]; // B
        }
        out.write(reinterpret_cast<const char*>(rgbRow.data()), rgbRow.size());
    }

    return out.good();
}

FrameRecorderStats FrameRecorder::stats() const noexcept {
    FrameRecorderStats s{};
    s.totalSnapshotsRequested = requested_.load(std::memory_order_relaxed);
    s.totalSnapshotsSaved = saved_.load(std::memory_order_relaxed);
    s.totalFailedSaves = failed_.load(std::memory_order_relaxed);
    s.lastSavedPath = lastSavedPath_;
    return s;
}

bool FrameRecorder::is_active() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

} // namespace capapp::capture
