#include "cedar/ps5_session.hpp"
#include <cerrno>
#include <limits>
#include <time.h>

#if !defined(CEDAR_PS5)
#error "Ps5Session requires real PS5 device adapters, not a host success stub"
#endif

namespace cedar {
namespace {
template<std::size_t N>
bool copy_text(char (&target)[N], const char* source) noexcept {
    if (!source) return false;
    for (std::size_t i = 0; i < N; ++i) {
        target[i] = source[i];
        if (!source[i]) return true;
    }
    target[0] = '\0';
    return false;
}
template<std::size_t N>
bool copy_path(char (&target)[N], std::string_view source) noexcept {
    if (source.size() >= N) return false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (!source[i]) return false;
        target[i] = source[i];
    }
    target[source.size()] = '\0';
    return true;
}
} // namespace

Ps5Session::Ps5Session() noexcept
    : platform_(Services{this, initialize_service, shutdown_service,
                         suspend_service, resume_service, poll_service, present_service}) {}
Ps5Session::~Ps5Session() { (void)shutdown(); }

Status Ps5Session::record(SessionStage stage, Status status, int native) noexcept {
    diagnostics_.stage = stage;
    diagnostics_.status = status;
    diagnostics_.native_error = native;
    return status;
}

Status Ps5Session::start(const Ps5SessionConfig& config) noexcept {
    if (platform_.phase() != Phase::cold || cleanup_pending())
        return record(SessionStage::configuration, Status::invalid_state);
    if (!config.process_apis_ready || config.user_id < 0 ||
        (config.audio_channels != 1 && config.audio_channels != 2) ||
        config.audio_grain_frames < 256 || config.audio_grain_frames > 2048 ||
        config.audio_grain_frames % 256 || config.presentation.width <= 0 ||
        config.presentation.height <= 0 ||
        !copy_text(title_, config.presentation.title) ||
        (config.presentation.renderer_name && !copy_text(renderer_, config.presentation.renderer_name)) ||
        !copy_path(asset_root_, config.read_only_asset_root) ||
        !copy_path(save_root_, config.app_owned_save_root))
        return record(SessionStage::configuration, Status::invalid_argument);

    diagnostics_ = {};
    config_ = config;
    config_.presentation.title = title_;
    config_.presentation.renderer_name = config.presentation.renderer_name ? renderer_ : nullptr;
    config_.read_only_asset_root = asset_root_;
    config_.app_owned_save_root = save_root_;
    quit_requested_ = false;
    return platform_.start();
}

Status Ps5Session::initialize_devices() noexcept {
    if (!config_.read_only_asset_root.empty()) {
        diagnostics_.asset_error = assets_.mount(config_.read_only_asset_root);
        if (!diagnostics_.asset_error) {
            record(SessionStage::assets, Status::backend_failure,
                   diagnostics_.asset_error.system_error);
            (void)close_devices();
            return diagnostics_.status;
        }
    }
    if (!config_.app_owned_save_root.empty()) {
        diagnostics_.save_error = saves_.mount(config_.app_owned_save_root);
        if (!diagnostics_.save_error) {
            record(SessionStage::saves, Status::backend_failure,
                   diagnostics_.save_error.system_error);
            (void)close_devices();
            return diagnostics_.status;
        }
    }
    auto result = input_.open(config_.user_id);
    if (result != Status::ok) {
        record(SessionStage::input, result, input_.native_error());
        (void)close_devices();
        return result;
    }
    result = audio_.open(config_.user_id, config_.audio_channels, config_.audio_grain_frames);
    if (result != Status::ok) {
        record(SessionStage::audio, result, audio_.native_error());
        (void)close_devices();
        return result;
    }
    result = presentation_.initialize(config_.presentation);
    if (result != Status::ok) {
        record(SessionStage::presentation, result);
        (void)close_devices();
        return result;
    }
    return record(SessionStage::none, Status::ok);
}

Status Ps5Session::close_devices() noexcept {
    cleanup_called_ = true;
    presentation_.shutdown();
    if (audio_.close() != Status::ok) diagnostics_.audio_close_error = audio_.native_error();
    if (input_.close() != Status::ok) diagnostics_.input_close_error = input_.native_error();
    // These existing APIs have void unmounts; descriptor-close failures cannot
    // be observed here. They do not write asset/save contents on unmount.
    saves_.unmount();
    assets_.unmount();
    return cleanup_pending() ? Status::backend_failure : Status::ok;
}

Status Ps5Session::shutdown() noexcept {
    cleanup_called_ = false;
    platform_.stop();
    // Failed initialization leaves Platform uninitialized; its stop callback
    // does not run. Repeated shutdowns retry outstanding handles exactly once.
    if (!cleanup_called_) (void)close_devices();
    const auto result = cleanup_pending() ? Status::backend_failure : Status::ok;
    // Preserve the triggering startup/frame error and sticky cleanup failures.
    if (diagnostics_.status == Status::ok)
        record(SessionStage::shutdown, result);
    return result;
}

Status Ps5Session::monotonic_now_ns(std::uint64_t& value, int& system_error) noexcept {
    value = 0;
    system_error = 0;
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        system_error = errno;
        return Status::backend_failure;
    }
    if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L)
        return Status::backend_failure;
    const auto seconds = static_cast<std::uint64_t>(now.tv_sec);
    const auto nanos = static_cast<std::uint64_t>(now.tv_nsec);
    if (seconds > (std::numeric_limits<std::uint64_t>::max() - nanos) / 1000000000ULL)
        return Status::backend_failure;
    value = seconds * 1000000000ULL + nanos;
    return Status::ok;
}

Status Ps5Session::begin_frame(FrameTiming& timing, InputFrame& input) noexcept {
    timing = {};
    input = {};
    if (quit_requested_ || platform_.phase() != Phase::ready)
        return record(SessionStage::frame, Status::invalid_state);
    std::uint64_t now{};
    int native{};
    const auto result = monotonic_now_ns(now, native);
    if (result != Status::ok) return record(SessionStage::clock, result, native);
    record(SessionStage::frame, Status::ok);
    const auto frame = platform_.begin_frame(now, timing, input);
    // poll_service records its own input error, retaining native diagnostics.
    if (frame != Status::ok && (diagnostics_.stage != SessionStage::input ||
                                diagnostics_.status != frame))
        return record(SessionStage::frame, frame);
    return frame;
}
Status Ps5Session::upload(const SurfaceView& surface) noexcept {
    if (platform_.phase() != Phase::ready)
        return record(SessionStage::presentation, Status::invalid_state);
    return record(SessionStage::presentation, presentation_.upload(surface));
}
Status Ps5Session::present() noexcept {
    record(SessionStage::frame, Status::ok);
    const auto result = platform_.present();
    if (result != Status::ok && (diagnostics_.stage != SessionStage::presentation ||
                                diagnostics_.status != result))
        return record(SessionStage::frame, result);
    return result;
}
Status Ps5Session::submit_audio(const float* samples, std::size_t frames) noexcept {
    if (platform_.phase() != Phase::ready) return record(SessionStage::audio, Status::invalid_state);
    const auto result = audio_.submit(samples, frames);
    return record(SessionStage::audio, result,
                  result == Status::backend_failure ? audio_.native_error() : 0);
}
Status Ps5Session::submit_audio_queued(AudioRing& queue) noexcept {
    if (platform_.phase() != Phase::ready) return record(SessionStage::audio, Status::invalid_state);
    const auto result = audio_.submit_queued(queue);
    return record(SessionStage::audio, result,
                  result == Status::backend_failure ? audio_.native_error() : 0);
}
Status Ps5Session::suspend() noexcept {
    return record(SessionStage::suspend, platform_.suspend());
}
Status Ps5Session::resume() noexcept {
    return record(SessionStage::resume, platform_.resume());
}
const PosixReadOnlyAssets* Ps5Session::assets() const noexcept {
    return platform_.phase() == Phase::ready && assets_.mounted() ? &assets_ : nullptr;
}
SaveStore* Ps5Session::saves() noexcept {
    return platform_.phase() == Phase::ready && saves_.mounted() ? &saves_ : nullptr;
}
Status Ps5Session::initialize_service(void* context) noexcept {
    return static_cast<Ps5Session*>(context)->initialize_devices();
}
void Ps5Session::shutdown_service(void* context) noexcept {
    (void)static_cast<Ps5Session*>(context)->close_devices();
}
Status Ps5Session::suspend_service(void*) noexcept { return Status::unavailable; }
Status Ps5Session::resume_service(void*) noexcept { return Status::unavailable; }
Status Ps5Session::poll_service(void* context, InputSample* sample) noexcept {
    if (!sample) return Status::invalid_argument;
    auto& session = *static_cast<Ps5Session*>(context);
    const auto result = session.input_.poll(*sample);
    return session.record(SessionStage::input, result, session.input_.native_error());
}
Status Ps5Session::present_service(void* context, std::uint64_t) noexcept {
    auto& session = *static_cast<Ps5Session*>(context);
    return session.record(SessionStage::presentation, session.presentation_.present());
}
} // namespace cedar
