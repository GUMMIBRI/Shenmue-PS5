// SPDX-License-Identifier: MPL-2.0
// Source-informed native adaptation of PE1's optional FSR1 presentation facility.
#pragma once
#include "cedar/post_types.hpp"

namespace cedar {
struct Fsr1Options { bool enabled{}; bool rcas_enabled{true}; float sharpness_stops{1}; };
struct Fsr1Record {
    bool applied{};
    std::uint32_t passes_recorded{};
    PostSource output{}; // Valid only on complete recording; not a completion fence.
};
// AMD FSR1 spatial EASU/RCAS, no motion/history/AI/frame generation. Borrowed
// device; main thread, outside active GPU passes. Opaque, perceptual SDR only.
// Exact crop is copied to compact storage before gathers; native UI is excluded.
class Fsr1 final {
public:
    Fsr1() noexcept = default;
    ~Fsr1();
    Fsr1(const Fsr1&) = delete;
    Fsr1& operator=(const Fsr1&) = delete;
    Status initialize(SDL_GPUDevice* device) noexcept;
    Status configure(Fsr1Options options) noexcept;
    Status record(SDL_GPUCommandBuffer* commands, const PostSource& source,
                  std::uint32_t output_width, std::uint32_t output_height,
                  Fsr1Record& result) noexcept;
    void reset_failure() noexcept { failure_latched_ = false; }
    void shutdown() noexcept;
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message, bool latch = false) noexcept;
    Status native_failure() noexcept;
    Status prepare(std::uint32_t iw, std::uint32_t ih, std::uint32_t ow, std::uint32_t oh) noexcept;
    bool pass(SDL_GPUCommandBuffer*, SDL_GPUTexture*, SDL_GPUTexture*,
              SDL_GPUGraphicsPipeline*, const void*, Uint32) noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUGraphicsPipeline* easu_{}, *rcas_{};
    SDL_GPUSampler* sampler_{};
    SDL_GPUTexture* compact_{}, *intermediate_{}, *output_{};
    std::uint32_t iw_{}, ih_{}, ow_{}, oh_{};
    Fsr1Options options_{};
    bool failure_latched_{};
    char error_[256]{};
};
} // namespace cedar
