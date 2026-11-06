// Original Cedar device composition; no game engine, main or process boot.
#pragma once
#include "cedar/platform.hpp"
#include "cedar/presentation.hpp"
#include "cedar/ps5_audio.hpp"
#include "cedar/ps5_input.hpp"
#include "cedar/posix_assets.hpp"
#include "cedar/save_store.hpp"
#include <string_view>

namespace cedar {
struct Ps5SessionConfig {
    // Caller asserts successful module/user-service/pad/audio process setup.
    // Session never repeats process init or accepts guessed already-init codes.
    bool process_apis_ready{};
    int user_id{-1};
    PresentationConfig presentation{};
    std::uint32_t audio_channels{}, audio_grain_frames{};
    // Empty disables the facility. Nonempty roots are copied, never defaulted.
    std::string_view read_only_asset_root{};
    std::string_view app_owned_save_root{};
};
enum class SessionStage : std::uint8_t {
    none, configuration, assets, saves, input, audio, presentation, clock,
    frame, shutdown, suspend, resume
};
struct SessionDiagnostics {
    SessionStage stage{SessionStage::none};
    Status status{Status::ok};
    int native_error{};
    PosixAssetStatus asset_error{};
    SaveStatus save_error{};
    // Sticky cleanup failures survive successful retries during this session.
    int input_close_error{}, audio_close_error{};
};

// Single main/video thread, including all audio calls and destruction. No event
// pumping: caller owns SDL events, all routing and requested quit decisions.
// One-shot lifetime follows Platform: reconstruct after stop or failed start.
class Ps5Session final {
public:
    Ps5Session() noexcept;
    ~Ps5Session();
    Ps5Session(const Ps5Session&) = delete;
    Ps5Session& operator=(const Ps5Session&) = delete;
    Status start(const Ps5SessionConfig& config) noexcept;
    Status begin_frame(FrameTiming& timing, InputFrame& input) noexcept;
    Status upload(const SurfaceView& surface) noexcept;
    Status present() noexcept;
    Status submit_audio(const float* samples, std::size_t frames) noexcept;
    Status submit_audio_queued(AudioRing& queue) noexcept;
    // Idempotent best effort; repeated calls retry retained native handles.
    Status shutdown() noexcept;
    Status suspend() noexcept;
    Status resume() noexcept;
    void request_quit() noexcept { quit_requested_ = true; }
    bool quit_requested() const noexcept { return quit_requested_; }
    Phase phase() const noexcept { return platform_.phase(); }
    std::uint64_t presented_frames() const noexcept { return platform_.presented_frames(); }
    std::uint32_t window_id() const noexcept { return presentation_.window_id(); }
    const SessionDiagnostics& diagnostics() const noexcept { return diagnostics_; }
    const char* presentation_error() const noexcept { return presentation_.last_error(); }
    bool cleanup_pending() const noexcept { return input_.is_open() || audio_.opened(); }
    const PosixReadOnlyAssets* assets() const noexcept;
    SaveStore* saves() noexcept;
    static Status monotonic_now_ns(std::uint64_t& value, int& system_error) noexcept;
private:
    static Status initialize_service(void* context) noexcept;
    static void shutdown_service(void* context) noexcept;
    static Status suspend_service(void* context) noexcept;
    static Status resume_service(void* context) noexcept;
    static Status poll_service(void* context, InputSample* sample) noexcept;
    static Status present_service(void* context, std::uint64_t frame_id) noexcept;
    Status initialize_devices() noexcept;
    Status close_devices() noexcept;
    Status record(SessionStage stage, Status status, int native_error = 0) noexcept;
    Presentation presentation_{};
    Ps5Input input_{};
    Ps5AudioOutput audio_{};
    PosixReadOnlyAssets assets_{};
    SaveStore saves_{};
    Platform platform_;
    Ps5SessionConfig config_{};
    SessionDiagnostics diagnostics_{};
    char title_[256]{}, renderer_[128]{}, asset_root_[1024]{}, save_root_[1024]{};
    bool quit_requested_{}, cleanup_called_{};
};
} // namespace cedar
