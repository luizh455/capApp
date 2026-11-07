#pragma once

#include <capapp/vulkan/vulkan_types.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

namespace capapp::vulkan {

enum class ShaderStage : uint32_t {
    Vertex = 0,
    Fragment,
    Compute
};

struct ShaderBytecode {
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
    std::vector<uint32_t> spirvWords;

    [[nodiscard]] bool is_valid() const noexcept {
        return !spirvWords.empty();
    }

    [[nodiscard]] size_t size_bytes() const noexcept {
        return spirvWords.size() * sizeof(uint32_t);
    }
};

class ShaderPipeline {
public:
    ShaderPipeline() = default;

    void add_stage(ShaderStage stage, std::vector<uint32_t> spirvWords, std::string entryPoint = "main") {
        stages_.push_back({stage, std::move(entryPoint), std::move(spirvWords)});
    }

    [[nodiscard]] const std::vector<ShaderBytecode>& stages() const noexcept {
        return stages_;
    }

    [[nodiscard]] bool is_complete_graphics_pipeline() const noexcept {
        bool hasVert = false;
        bool hasFrag = false;
        for (const auto& stage : stages_) {
            if (stage.stage == ShaderStage::Vertex && stage.is_valid()) hasVert = true;
            if (stage.stage == ShaderStage::Fragment && stage.is_valid()) hasFrag = true;
        }
        return hasVert && hasFrag;
    }

private:
    std::vector<ShaderBytecode> stages_;
};

} // namespace capapp::vulkan
