// SPDX-License-Identifier: MPL-2.0
// Source-informed native post facility; provenance in docs/DISPLAY-EFFECTS.md.
#pragma once
#include "cedar/post_types.hpp"

namespace cedar {
enum class DisplayEffectMode : std::uint8_t { off, scanlines, crt };
struct DisplayEffectSettings {
    DisplayEffectMode mode{DisplayEffectMode::off};
    // Actual producer's logical image rows, not guessed from upscaled texture.
    std::uint32_t logical_rows{};
    float scanline_strength{0.08f}; // Supported range [0,0.25].
    float mask_strength{0.055f}; // Supported range [0,0.15].
    float mask_color[3]{1.0f, 1.0f, 1.0f}; // Per-channel attenuation weights [0,1].
    SDL_GPUFilter filter{SDL_GPU_FILTER_LINEAR};
};

// Same owning GPU/main thread for every operation and destruction. Device,
// caller source/target and commands remain caller-owned; no submit or UI route.
class DisplayEffects final {
public:
    DisplayEffects() noexcept = default;
    ~DisplayEffects();
    DisplayEffects(const DisplayEffects&) = delete;
    DisplayEffects& operator=(const DisplayEffects&) = delete;
    // Non-sRGB RGBA8/BGRA8 or RGBA16_FLOAT SDR output. Resources are lazy for
    // an active effect. Reinitialize only after explicit shutdown.
    Status initialize(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format) noexcept;
    // Off returns ok/applied=false without GPU resource work, even uninitialized.
    // Active errors also leave applied=false: caller records its actual original
    // presentation instead. Unknown/linear/HDR encoding or native_ui is unsupported.
    // No pass may already be active on commands. Only base-level single-sample
    // 2D sampler source and 2D color target descriptors are supported.
    Status record(SDL_GPUCommandBuffer* commands, const PostSource& source,
                  const PostTarget& target, const DisplayEffectSettings& settings,
                  bool& applied) noexcept;
    void shutdown() noexcept;
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    Status native_failure() noexcept;
    Status prepare(SDL_GPUFilter filter) noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUGraphicsPipeline* pipeline_{};
    SDL_GPUSampler* samplers_[2]{};
    SDL_GPUTextureFormat format_{SDL_GPU_TEXTUREFORMAT_INVALID};
    char error_[256]{};
};
} // namespace cedar
