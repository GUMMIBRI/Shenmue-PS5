// Original Cedar platform scaffold. No recovered game logic.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class Status : std::uint8_t {
    ok, invalid_argument, invalid_state, unavailable, queue_full, backend_failure
};
enum class Phase : std::uint8_t { cold, ready, suspended, stopped, failed };
enum class Button : std::uint8_t {
    confirm, cancel, action, menu, up, down, left, right, left_shoulder,
    right_shoulder, secondary_action, left_trigger_button, right_trigger_button,
    left_stick_button, right_stick_button, touchpad_button, count
};
struct InputSample {
    std::uint32_t held{};
    float left_x{}, left_y{}, right_x{}, right_y{}, left_trigger{}, right_trigger{};
    bool connected{};
};
struct InputFrame {
    InputSample sample{};
    std::uint32_t pressed{}, released{};
};
struct FrameTiming {
    std::uint64_t monotonic_ns{}, elapsed_ns{};
    double delta_seconds{};
    bool discontinuity{};
};
// Concrete adapters must supply these operations. A missing adapter is an error.
struct Services {
    void* context{};
    Status (*initialize)(void*){};
    void (*shutdown)(void*){};
    Status (*suspend)(void*){};
    Status (*resume)(void*){};
    Status (*poll_input)(void*, InputSample*){};
    Status (*present)(void*, std::uint64_t frame_id){};
};
class Platform final {
public:
    explicit Platform(Services services) noexcept;
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Status start() noexcept;
    Status suspend() noexcept;
    Status resume() noexcept;
    void stop() noexcept;
    Status begin_frame(std::uint64_t monotonic_ns, FrameTiming& timing,
                       InputFrame& input) noexcept;
    Status present() noexcept;
    Phase phase() const noexcept { return phase_; }
    std::uint64_t presented_frames() const noexcept { return frame_id_; }
private:
    Services services_;
    Phase phase_{Phase::cold};
    bool initialized_{}, frame_open_{}, have_clock_{};
    std::uint32_t previous_buttons_{};
    std::uint64_t last_ns_{}, frame_id_{};
};

// Single-threaded PCM staging. The caller owns storage and synchronization.
// Interleaved float samples; operations are all-or-nothing on whole frames.
class AudioRing final {
public:
    Status configure(float* storage, std::size_t capacity_frames,
                     std::uint32_t channels, std::uint32_t sample_rate) noexcept;
    Status push(const float* interleaved, std::size_t frames) noexcept;
    Status pop(float* interleaved, std::size_t frames) noexcept;
    Status peek(float* interleaved, std::size_t frames) const noexcept;
    Status discard(std::size_t frames) noexcept;
    void clear() noexcept;
    std::size_t available_frames() const noexcept { return used_; }
    std::size_t free_frames() const noexcept { return capacity_ - used_; }
    std::uint32_t sample_rate() const noexcept { return rate_; }
    std::uint32_t channels() const noexcept { return channels_; }
private:
    float* data_{};
    std::size_t capacity_{}, read_{}, write_{}, used_{};
    std::uint32_t channels_{}, rate_{};
};

enum class PixelFormat : std::uint8_t { rgba8, bgra8 };
struct SurfaceView {
    const std::uint8_t* pixels{};
    std::size_t bytes{}, pitch{};
    std::uint32_t width{}, height{};
    PixelFormat format{PixelFormat::rgba8};
};
// Checks a caller-owned upload view; does not upload, render or decode assets.
Status validate_surface(const SurfaceView& surface) noexcept;
} // namespace cedar
