#include "cedar/platform.hpp"
#include <limits>

namespace cedar {
namespace {
constexpr std::uint32_t button_mask = (1u << static_cast<unsigned>(Button::count)) - 1u;
bool axis_valid(float n, float low, float high) noexcept {
    return n >= low && n <= high; // Also rejects NaN and infinities.
}
bool valid_input(const InputSample& s) noexcept {
    return !(s.held & ~button_mask) && axis_valid(s.left_x, -1, 1) &&
        axis_valid(s.left_y, -1, 1) && axis_valid(s.right_x, -1, 1) &&
        axis_valid(s.right_y, -1, 1) && axis_valid(s.left_trigger, 0, 1) &&
        axis_valid(s.right_trigger, 0, 1);
}
}
Platform::Platform(Services services) noexcept : services_(services) {}
Platform::~Platform() { stop(); }
Status Platform::start() noexcept {
    if (phase_ != Phase::cold) return Status::invalid_state;
    if (!services_.initialize || !services_.shutdown || !services_.suspend ||
        !services_.resume || !services_.poll_input || !services_.present)
        return Status::unavailable;
    const auto result = services_.initialize(services_.context);
    if (result != Status::ok) {
        phase_ = Phase::failed;
        return result; // Failed initialize owns its partial-resource cleanup.
    }
    initialized_ = true;
    phase_ = Phase::ready;
    return Status::ok;
}
Status Platform::suspend() noexcept {
    if (phase_ != Phase::ready || frame_open_) return Status::invalid_state;
    const auto result = services_.suspend(services_.context);
    if (result == Status::ok) {
        phase_ = Phase::suspended;
        have_clock_ = false;
        previous_buttons_ = 0;
    }
    return result;
}
Status Platform::resume() noexcept {
    if (phase_ != Phase::suspended) return Status::invalid_state;
    const auto result = services_.resume(services_.context);
    if (result == Status::ok) phase_ = Phase::ready;
    return result;
}
void Platform::stop() noexcept {
    if (initialized_) services_.shutdown(services_.context);
    initialized_ = false;
    frame_open_ = false;
    previous_buttons_ = 0;
    have_clock_ = false;
    if (phase_ != Phase::cold) phase_ = Phase::stopped;
}
Status Platform::begin_frame(std::uint64_t now, FrameTiming& timing,
                             InputFrame& input) noexcept {
    if (phase_ != Phase::ready || frame_open_) return Status::invalid_state;
    InputSample next{};
    const auto result = services_.poll_input(services_.context, &next);
    if (result != Status::ok) return result;
    if (!valid_input(next)) return Status::invalid_argument;
    if (!next.connected) next = {}; // Disconnect generates release edges.
    FrameTiming next_timing{};
    next_timing.monotonic_ns = now;
    next_timing.discontinuity = !have_clock_ || now < last_ns_;
    if (!next_timing.discontinuity) next_timing.elapsed_ns = now - last_ns_;
    next_timing.delta_seconds = static_cast<double>(next_timing.elapsed_ns) / 1000000000.0;
    InputFrame next_input{};
    next_input.sample = next;
    next_input.pressed = next.held & ~previous_buttons_;
    next_input.released = previous_buttons_ & ~next.held;
    input = next_input;
    timing = next_timing;
    previous_buttons_ = next.held;
    last_ns_ = now;
    have_clock_ = true;
    frame_open_ = true;
    return Status::ok;
}
Status Platform::present() noexcept {
    if (phase_ != Phase::ready || !frame_open_) return Status::invalid_state;
    const auto result = services_.present(services_.context, frame_id_);
    frame_open_ = false;
    if (result == Status::ok) ++frame_id_;
    else phase_ = Phase::failed;
    return result;
}
Status AudioRing::configure(float* storage, std::size_t capacity,
                            std::uint32_t channels, std::uint32_t rate) noexcept {
    if (!storage || !capacity || !channels || channels > 8 || rate < 8000 ||
        rate > 192000 || capacity > std::numeric_limits<std::size_t>::max() /
        channels / sizeof(float)) return Status::invalid_argument;
    data_ = storage;
    capacity_ = capacity;
    channels_ = channels;
    rate_ = rate;
    clear();
    return Status::ok;
}
void AudioRing::clear() noexcept { read_ = write_ = used_ = 0; }
Status AudioRing::push(const float* src, std::size_t frames) noexcept {
    if (!data_) return Status::invalid_state;
    if (frames && !src) return Status::invalid_argument;
    if (frames > free_frames()) return Status::queue_full;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::uint32_t c = 0; c < channels_; ++c)
            data_[write_ * channels_ + c] = src[f * channels_ + c];
        if (++write_ == capacity_) write_ = 0;
    }
    used_ += frames;
    return Status::ok;
}
Status AudioRing::pop(float* dst, std::size_t frames) noexcept {
    const auto result = peek(dst, frames);
    return result == Status::ok ? discard(frames) : result;
}
Status AudioRing::peek(float* dst, std::size_t frames) const noexcept {
    if (!data_) return Status::invalid_state;
    if (frames && !dst) return Status::invalid_argument;
    if (frames > used_) return Status::unavailable;
    auto cursor = read_;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::uint32_t c = 0; c < channels_; ++c)
            dst[f * channels_ + c] = data_[cursor * channels_ + c];
        if (++cursor == capacity_) cursor = 0;
    }
    return Status::ok;
}
Status AudioRing::discard(std::size_t frames) noexcept {
    if (!data_) return Status::invalid_state;
    if (frames > used_) return Status::unavailable;
    // Avoid read_ + frames overflowing even for a very large supplied buffer.
    const auto until_wrap = capacity_ - read_;
    read_ = frames >= until_wrap ? frames - until_wrap : read_ + frames;
    used_ -= frames;
    return Status::ok;
}
Status validate_surface(const SurfaceView& s) noexcept {
    if (!s.pixels || !s.width || !s.height ||
        (s.format != PixelFormat::rgba8 && s.format != PixelFormat::bgra8))
        return Status::invalid_argument;
    const auto max = std::numeric_limits<std::size_t>::max();
    const std::size_t row = static_cast<std::size_t>(s.width) * 4;
    if (row / 4 != s.width) return Status::invalid_argument;
    if (s.pitch < row || (s.height > 1 && s.pitch > (max - row) / (s.height - 1)))
        return Status::invalid_argument;
    const std::size_t required = s.pitch * (s.height - 1) + row;
    return required <= s.bytes ? Status::ok : Status::invalid_argument;
}
} // namespace cedar
