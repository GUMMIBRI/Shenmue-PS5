// Original native scene producer target, before post-processing/upscaling.
#pragma once
#include "cedar/gpu_renderer.hpp"
#include "cedar/post_types.hpp"

namespace cedar {
struct SceneTargetConfig {
    std::uint32_t width{}, height{};
    SDL_GPUTextureFormat format{SDL_GPU_TEXTUREFORMAT_INVALID};
    PostColorEncoding encoding{PostColorEncoding::unknown};
};

// Owns one offscreen color texture and borrows its live main-thread GPU device.
// Prepared allocation has undefined contents and is NEVER published as a frame.
// No window, presentation, camera, projection, engine state or submission owner.
class SceneTarget final {
public:
    static constexpr std::uint32_t max_dimension = 8192;
    static constexpr std::size_t max_color_bytes = 128u * 1024u * 1024u;
    SceneTarget() noexcept = default;
    ~SceneTarget();
    SceneTarget(const SceneTarget&) = delete;
    SceneTarget& operator=(const SceneTarget&) = delete;
    // Explicit known producer transfer and supported storage are required.
    // Replacement is staged; a failed prepare retains the previous allocation.
    // Budget is per logical color texture, not all driver/deferred allocations.
    Status prepare(SDL_GPUDevice* device, const SceneTargetConfig& config) noexcept;
    // Game draws only. Nonempty actual draws go through GpuRenderer's native
    // record_to_texture; only success publishes source. The source is ordered
    // earlier in THIS command buffer, not fence-completed. Consume it in the
    // same stream before caller submission/cancellation; serial is caller-owned
    // per-frame provenance, not a generated/observed frame. Opaque is the actual
    // producer's explicit assertion, retained exactly for RGB-only post gates.
    Status record_scene(SDL_GPUCommandBuffer* commands, GpuRenderer& renderer,
                         const GpuDraw* draws, std::size_t count, const GpuPassClear& clear,
                         FrameContent content, std::uint64_t serial, bool opaque_assertion,
                         PostSource& source) noexcept;
    // Call only after old sources have no further CPU-side command references;
    // caller must account for all submit/cancel ownership. SDL defers pending
    // GPU destruction. Device must outlive shutdown/destruction; no idle wait.
    void shutdown() noexcept;
    bool prepared() const noexcept { return texture_ != nullptr; }
    const SceneTargetConfig& config() const noexcept { return config_; }
    std::size_t color_bytes() const noexcept { return color_bytes_; }
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUTexture* texture_{};
    SceneTargetConfig config_{};
    std::size_t color_bytes_{};
    char error_[256]{};
};
} // namespace cedar
