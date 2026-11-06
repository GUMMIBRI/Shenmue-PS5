#pragma once
#include "cedar/ps5_session.hpp"

namespace cedar {
struct EngineEnvironment {
    const PosixReadOnlyAssets* assets{};
    SaveStore* saves{};
    std::uint32_t audio_rate{48000}, audio_channels{};
};
struct EngineAudio {
    const float* samples{};
    std::size_t frames{};
    std::uint32_t sample_rate{48000}, channels{};
};
struct EngineHooks {
    void* context{};
    // These hooks are supplied by a real authorized engine. There is no default
    // implementation, demo engine, recovered logic, or successful missing-engine path.
    Status (*initialize)(void*, const EngineEnvironment&) noexcept{};
    // Releases partial initialization, joins engine workers and releases borrowed
    // files/device resources. Failure retains the session for an explicit retry;
    // this must be idempotent. Return ok only after all borrows/workers are gone.
    Status (*shutdown)(void*) noexcept{};
    Status (*update)(void*, const FrameTiming&, const InputFrame&, bool& request_quit) noexcept{};
    Status (*render)(void*, SurfaceView&) noexcept{};
    // May return a zero-frame slice. Buffers stay valid until step() returns.
    Status (*audio)(void*, EngineAudio&) noexcept{};
};
enum class ApplicationStage : std::uint8_t {
    none, configuration, session_start, engine_start, frame, update, render,
    audio, present, shutdown
};
struct ApplicationError {
    ApplicationStage stage{ApplicationStage::none};
    Status status{Status::ok};
    Status engine_cleanup_error{Status::ok};
};

// Main-thread orchestration only, never a process entry point or game boot stub.
// Caller loads modules/initializes APIs, owns event pumping and synchronizes all
// calls. Native audio submission may block this thread; no performance claim.
// Successful stop is REQUIRED before destruction or releasing engine/context/
// buffers. A failed engine shutdown preserves native resources for retry, not a
// safe destruction promise. The destructor cannot repair a violated lifetime.
class Application final {
public:
    Application() noexcept = default;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    // Caller-owned PCM storage must outlive this object and not overlap any
    // engine audio slice. Capacity is in frames, with channels from config.
    Status start(const Ps5SessionConfig& config, EngineHooks engine,
                 float* pcm_storage, std::size_t capacity_frames) noexcept;
    Status step() noexcept;
    Status stop() noexcept;
    void request_quit() noexcept { session_.request_quit(); }
    bool quit_requested() const noexcept { return session_.quit_requested(); }
    bool running() const noexcept { return running_; }
    bool cleanup_pending() const noexcept { return engine_attempted_ || session_.cleanup_pending(); }
    const ApplicationError& error() const noexcept { return error_; }
    const Ps5Session& session() const noexcept { return session_; }
private:
    Status fail(ApplicationStage stage, Status status) noexcept;
    Ps5Session session_{};
    EngineHooks engine_{};
    AudioRing audio_{};
    ApplicationError error_{};
    std::uint32_t channels_{}, grain_{};
    bool engine_attempted_{}, running_{};
};
} // namespace cedar
