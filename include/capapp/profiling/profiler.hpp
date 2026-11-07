#pragma once

#include <capapp/common.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <mutex>
#include <cstdint>

namespace capapp::profiling {

struct LatencyStats {
    uint64_t sampleCount{0};
    TimestampNs minNs{0};
    TimestampNs maxNs{0};
    TimestampNs avgNs{0};
    TimestampNs p50Ns{0};
    TimestampNs p90Ns{0};
    TimestampNs p99Ns{0};
};

class Profiler {
public:
    static Profiler& instance() {
        static Profiler s_instance;
        return s_instance;
    }

    void record_sample(std::string_view section, TimestampNs durationNs) {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_[std::string(section)].push_back(durationNs);
    }

    LatencyStats get_stats(std::string_view section) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = samples_.find(std::string(section));
        if (it == samples_.end() || it->second.empty()) {
            return {};
        }

        std::vector<TimestampNs> sorted = it->second;
        std::sort(sorted.begin(), sorted.end());

        LatencyStats s{};
        s.sampleCount = sorted.size();
        s.minNs = sorted.front();
        s.maxNs = sorted.back();

        uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
        s.avgNs = sum / s.sampleCount;

        s.p50Ns = sorted[s.sampleCount * 50 / 100];
        s.p90Ns = sorted[s.sampleCount * 90 / 100];
        s.p99Ns = sorted[s.sampleCount * 99 / 100];
        return s;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
    }

    // RenderDoc capture markers
    bool is_renderdoc_attached() const noexcept {
        return renderDocAttached_;
    }

    void set_renderdoc_attached(bool attached) noexcept {
        renderDocAttached_ = attached;
    }

    void begin_renderdoc_marker(const char* name) noexcept {
        if (renderDocAttached_) {
            activeMarkers_.push_back(name);
        }
    }

    void end_renderdoc_marker() noexcept {
        if (renderDocAttached_ && !activeMarkers_.empty()) {
            activeMarkers_.pop_back();
        }
    }

    [[nodiscard]] size_t active_marker_count() const noexcept {
        return activeMarkers_.size();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<TimestampNs>> samples_;
    bool renderDocAttached_{false};
    std::vector<std::string> activeMarkers_;
};

class ScopedLatencyMarker {
public:
    explicit ScopedLatencyMarker(std::string_view name)
        : name_(name), start_(std::chrono::steady_clock::now()) {
        Profiler::instance().begin_renderdoc_marker(name_.data());
    }

    ~ScopedLatencyMarker() {
        auto end = std::chrono::steady_clock::now();
        auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        Profiler::instance().record_sample(name_, static_cast<TimestampNs>(durationNs));
        Profiler::instance().end_renderdoc_marker();
    }

private:
    std::string_view name_;
    std::chrono::time_point<std::chrono::steady_clock> start_;
};

#define CAPAPP_PROFILE_SCOPE(name) \
    ::capapp::profiling::ScopedLatencyMarker g_prof_marker_##__LINE__(name)

} // namespace capapp::profiling
