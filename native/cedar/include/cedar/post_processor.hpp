#pragma once
#include "cedar/fsr1.hpp"
#include "cedar/display_effects.hpp"
#include "cedar/movie_effects.hpp"

namespace cedar {
using OriginalPostPresentation = Status (*)(void*, SDL_GPUCommandBuffer*, const PostSource&, const PostTarget&);
enum PostEffect : std::uint32_t {
    post_fsr1 = 1u, post_rcas = 2u, post_display = 4u, post_movie_cleanup = 8u, post_movie_backdrop = 16u
};
struct PostProcessorConfig {
    Fsr1Options fsr{};
    DisplayEffectSettings display{};
    MovieEffectsConfig movie{}; // Off/false defaults; pixel aspect is producer-supplied.
};
struct PostProcessorRecord {
    std::uint32_t requested_mask{}, effective_mask{}, latched_mask{};
    std::uint32_t passes_recorded{}; // Child-reported shader passes + display pass; callback opaque.
    std::uint32_t copies_recorded{}; // Graph crop blits only; FSR internal copies not exposed by its API.
    Status first_effect_error{Status::ok}, baseline_status{Status::unavailable};
    bool baseline_called{}, baseline_recorded{}, fallback_used{}, presentation_recorded{};
    PostSource last_valid_source{};
    char first_effect_message[256]{};
};

// Actual GPU command composition, never submission/completion or a game route.
// All calls/destruction on the creating main thread, outside active GPU passes.
// Caller keeps device/sources/targets/commands alive through actual GPU use.
// Original callback MUST really record the supplied source into supplied target;
// return ok only then, with no active GPU pass left even on failure. It must respect the given viewport and preserve the
// caller's load/clear contract. It may not submit/cancel or draw native UI.
class PostProcessor final {
public:
    static constexpr std::uint64_t max_owned_bytes = 64u * 1024u * 1024u;
    PostProcessor() noexcept = default;
    ~PostProcessor();
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    Status initialize(SDL_GPUDevice*, SDL_GPUTextureFormat target_format,
                       OriginalPostPresentation, void* callback_context) noexcept;
    Status configure(const PostProcessorConfig&) noexcept;
    Status begin_movie(std::uint64_t nonzero_serial) noexcept;
    Status end_movie(std::uint64_t serial) noexcept;
    Status record(SDL_GPUCommandBuffer*, const PostSource&, const PostTarget&,
                    MovieSourceReadiness, PostProcessorRecord&) noexcept;
    void reset_failure() noexcept;
    void shutdown() noexcept;
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status, const char*) noexcept;
    bool thread() const noexcept;
    void effect_error(PostProcessorRecord&, Status, const char*, std::uint32_t latch_bit = 0) noexcept;
    void remember_failure(unsigned slot, Status, const char*) noexcept;
    Status baseline(SDL_GPUCommandBuffer*, const PostSource&, const PostTarget&, PostProcessorRecord&) noexcept;
    Status prepare_movie(std::uint32_t iw, std::uint32_t ih, std::uint32_t ow, std::uint32_t oh, bool cleanup) noexcept;
    Status ensure_movie() noexcept;
    void release_targets() noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUTextureFormat target_format_{SDL_GPU_TEXTUREFORMAT_INVALID};
    OriginalPostPresentation original_{};
    void* callback_context_{};
    std::uint64_t owner_thread_{}, serial_{};
    PostProcessorConfig config_{};
    Fsr1 fsr_{};
    DisplayEffects display_{};
    MovieEffects movie_{};
    bool fsr_ready_{}, display_ready_{}, movie_ready_{}, movie_active_{};
    bool fsr_failed_{}, display_failed_{}, movie_failed_{}, targets_failed_{};
    struct Failure { Status status{Status::ok}; char message[256]{}; } failures_[4]{};
    SDL_GPUTexture* crop_{};
    SDL_GPUTexture* clean_{};
    SDL_GPUTexture* presentation_{};
    std::uint32_t iw_{}, ih_{}, ow_{}, oh_{};
    char error_[256]{};
};
} // namespace cedar
