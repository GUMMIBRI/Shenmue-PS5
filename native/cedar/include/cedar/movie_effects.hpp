// Original explicitly classified movie-only GPU presentation effects.
#pragma once
#include "cedar/post_types.hpp"

namespace cedar {
enum class MovieCleanup : std::uint8_t { off, gentle, strong };
enum class MovieSourceReadiness : std::uint8_t { not_ready, completed, earlier_in_same_commands };
struct MovieEffectsConfig {
    MovieCleanup cleanup{MovieCleanup::off};
    bool blur_backdrop{};
    float blur_radius_pixels{2.0f};
    float backdrop_brightness{1.0f};
    float pixel_aspect{1.0f}; // Actual producer's pixel aspect, never a resolution heuristic.
};
struct MovieEffectRecord {
    std::uint64_t source_serial{};
    std::uint32_t passes_recorded{};
    bool output_recorded{}, cleanup_recorded{}, backdrop_recorded{};
    SDL_FRect center{};
    PostColorEncoding output_encoding{PostColorEncoding::unknown};
};

// Borrows device and caller command/source/target handles on the owning main/GPU
// thread. Producer must explicitly bracket real movie lifetime with a nonzero
// serial and identify FrameContent::movie; dimensions never classify a movie.
// No command submission, fence ownership, window, events, decoder or playback.
class MovieEffects final {
public:
    MovieEffects() noexcept = default;
    ~MovieEffects();
    MovieEffects(const MovieEffects&) = delete;
    MovieEffects& operator=(const MovieEffects&) = delete;
    Status initialize(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format) noexcept;
    Status begin_source(std::uint64_t serial) noexcept;
    Status end_source(std::uint64_t serial) noexcept;
    Status record(SDL_GPUCommandBuffer* commands, std::uint64_t serial,
                   const PostSource& source, const PostTarget& target,
                   MovieSourceReadiness readiness, const MovieEffectsConfig& config,
                   MovieEffectRecord& result) noexcept;
    void shutdown() noexcept;
    bool source_active() const noexcept { return active_; }
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    Status native_failure() noexcept;
    Status ensure_blur(std::uint32_t width, std::uint32_t height) noexcept;
    void release_blur() noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUGraphicsPipeline* cleanup_pipeline_{};
    SDL_GPUGraphicsPipeline* blur_pipeline_{};
    SDL_GPUSampler* sampler_{};
    SDL_GPUTexture* blur_horizontal_{};
    SDL_GPUTexture* blur_vertical_{};
    SDL_GPUTextureFormat target_format_{SDL_GPU_TEXTUREFORMAT_INVALID};
    std::uint32_t blur_width_{}, blur_height_{};
    std::uint64_t serial_{};
    bool active_{};
    char error_[256]{};
};
} // namespace cedar
