#pragma once
#include "cedar/platform.hpp"

namespace cedar {
// Single-threaded public PS5 audio-output adapter; no resampling or mixing.
// The process owns module loading and calls initialize_api before opening ports.
class Ps5AudioOutput final {
public:
    Ps5AudioOutput() noexcept = default;
    ~Ps5AudioOutput();
    Ps5AudioOutput(const Ps5AudioOutput&) = delete;
    Ps5AudioOutput& operator=(const Ps5AudioOutput&) = delete;
    static Status initialize_api(int& native_error) noexcept;
    Status open(int explicit_user_id, std::uint32_t channels,
                std::uint32_t grain_frames) noexcept;
    // Exactly one 48 kHz interleaved float grain. Output is synchronous on the
    // calling playback thread; retain this object until the call returns.
    Status submit(const float* interleaved, std::size_t frames) noexcept;
    // Consumes one grain only after native success; on failure retains PCM.
    // A native error cannot prove that no samples reached the output device.
    Status submit_queued(AudioRing& queue) noexcept;
    Status close() noexcept;
    bool opened() const noexcept { return handle_ >= 1; }
    int native_error() const noexcept { return native_error_; }
    std::uint32_t grain_frames() const noexcept { return frames_; }
    std::uint32_t channels() const noexcept { return channels_; }
private:
    int handle_{-1}, native_error_{};
    std::uint32_t channels_{}, frames_{};
    alignas(64) float staging_[2048 * 2]{};
};
} // namespace cedar
