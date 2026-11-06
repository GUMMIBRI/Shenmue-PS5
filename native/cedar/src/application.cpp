#include "cedar/application.hpp"

namespace cedar {
Application::~Application() { (void)stop(); }
Status Application::fail(ApplicationStage stage, Status status) noexcept {
    error_ = {stage, status};
    (void)stop(); // Keep the triggering error even if cleanup also fails.
    return status;
}
Status Application::start(const Ps5SessionConfig& config, EngineHooks engine,
                           float* pcm, std::size_t capacity) noexcept {
    if (session_.phase() != Phase::cold || running_ || engine_attempted_) {
        error_ = {ApplicationStage::configuration, Status::invalid_state};
        return Status::invalid_state;
    }
    if (!engine.initialize || !engine.shutdown || !engine.update || !engine.render || !engine.audio) {
        error_ = {ApplicationStage::configuration, Status::unavailable};
        return Status::unavailable; // Nothing initialized or opened.
    }
    if (capacity < config.audio_grain_frames || capacity > 96000) {
        error_ = {ApplicationStage::configuration, Status::invalid_argument};
        return Status::invalid_argument;
    }
    const auto queue = audio_.configure(pcm, capacity, config.audio_channels, 48000);
    if (queue != Status::ok) {
        error_ = {ApplicationStage::configuration, queue};
        return queue;
    }
    engine_ = engine;
    channels_ = config.audio_channels;
    grain_ = config.audio_grain_frames;
    auto result = session_.start(config);
    if (result != Status::ok) return fail(ApplicationStage::session_start, result);
    engine_attempted_ = true;
    result = engine_.initialize(engine_.context,
                                EngineEnvironment{session_.assets(), session_.saves(), 48000, channels_});
    if (result != Status::ok) return fail(ApplicationStage::engine_start, result);
    error_ = {};
    running_ = true;
    return Status::ok;
}
Status Application::step() noexcept {
    if (!running_) return Status::invalid_state;
    if (session_.quit_requested()) {
        const auto cleanup = stop();
        return cleanup == Status::ok ? Status::unavailable : cleanup;
    }
    FrameTiming timing;
    InputFrame input;
    auto result = session_.begin_frame(timing, input);
    if (result != Status::ok) return fail(ApplicationStage::frame, result);
    bool quit = false;
    result = engine_.update(engine_.context, timing, input, quit);
    if (result != Status::ok) return fail(ApplicationStage::update, result);
    if (quit) {
        session_.request_quit();
        const auto cleanup = stop();
        return cleanup == Status::ok ? Status::unavailable : cleanup;
    }
    SurfaceView surface;
    result = engine_.render(engine_.context, surface);
    if (result != Status::ok) return fail(ApplicationStage::render, result);
    result = session_.upload(surface);
    if (result != Status::ok) return fail(ApplicationStage::render, result);
    EngineAudio chunk;
    result = engine_.audio(engine_.context, chunk);
    if (result != Status::ok) return fail(ApplicationStage::audio, result);
    if (chunk.frames) {
        if (!chunk.samples || chunk.channels != channels_ || chunk.sample_rate != 48000)
            return fail(ApplicationStage::audio, Status::invalid_argument);
        if (chunk.frames > audio_.free_frames()) return fail(ApplicationStage::audio, Status::queue_full);
        const auto samples = chunk.frames * channels_; // Bounded by configured ring capacity.
        for (std::size_t i = 0; i < samples; ++i)
            if (!(chunk.samples[i] >= -1.0f && chunk.samples[i] <= 1.0f))
                return fail(ApplicationStage::audio, Status::invalid_argument);
        result = audio_.push(chunk.samples, chunk.frames);
        if (result != Status::ok) return fail(ApplicationStage::audio, result);
    }
    while (audio_.available_frames() >= grain_) {
        result = session_.submit_audio_queued(audio_);
        if (result != Status::ok) return fail(ApplicationStage::audio, result);
    }
    result = session_.present();
    if (result != Status::ok) return fail(ApplicationStage::present, result);
    return Status::ok;
}
Status Application::stop() noexcept {
    running_ = false;
    if (engine_attempted_) {
        const auto cleanup = engine_.shutdown(engine_.context);
        error_.engine_cleanup_error = cleanup;
        if (cleanup != Status::ok) {
            // In particular a failed streaming-worker join must not be followed
            // by unmounting the assets it still borrows. Retain the full session.
            if (error_.status == Status::ok) {
                error_.stage = ApplicationStage::shutdown;
                error_.status = cleanup;
            }
            return cleanup;
        }
        engine_attempted_ = false;
    }
    audio_.clear();
    const auto result = session_.shutdown();
    if (result != Status::ok && error_.status == Status::ok)
        error_ = {ApplicationStage::shutdown, result};
    return result;
}
} // namespace cedar
