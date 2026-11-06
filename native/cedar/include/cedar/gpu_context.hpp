#pragma once

#include "cedar/gpu_types.hpp"

struct SDL_Window;

namespace cedar {

enum class GpuVideoOwnership : std::uint8_t { acquire_reference, require_initialized };
struct GpuContextConfig {
    const char* title{"Cedar native GPU"};
    int width{1280}, height{720};
    GpuVideoOwnership video{GpuVideoOwnership::acquire_reference};
    bool fullscreen{}, resizable{true}, debug{};
    bool enable_anisotropy{true}; // Explicit Vulkan feature request, not a sampler claim.
};

// Explicit-close, main/video-thread-only ownership. Call shutdown successfully
// before destruction. On idle failure retain context, device and child-resource
// lifetimes for explicit retry; destructor never forces idle/destroys resources.
// Release every externally owned device child before shutdown. A borrowed device
// must not be destroyed or have this context's window/swapchain managed elsewhere.
class GpuContext final {
public:
    static constexpr std::size_t title_bytes = 256;
    GpuContext() noexcept = default;
    ~GpuContext() = default; // Explicit shutdown is a lifetime precondition.
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    Status initialize(const GpuContextConfig& config) noexcept;
    // Caller supplies an empty frame object that stays alive/unmoved until
    // consumed. One outstanding frame; copies/foreign/tampered frames rejected.
    // A successful null swapchain yields drawable=false and an empty frame.
    // An acquire error can leave an owned command in frame: inspect
    // frame_outstanding() even on failure and explicitly submit/cancel it.
    Status begin_frame(GpuFrame& frame, bool& drawable) noexcept;
    // Close all render/copy/compute passes before either operation. A native
    // submit/cancel call consumes its command reference even when it fails.
    Status submit(GpuFrame& frame) noexcept;
    // SDL forbids cancellation after acquiring a swapchain texture. In that
    // case this returns invalid_state WITHOUT consuming the frame; submit is
    // required. No implicit submission/presentation is hidden inside cancel.
    Status cancel(GpuFrame& frame) noexcept;
    Status wait_idle() noexcept;
    Status shutdown() noexcept;

    bool initialized() const noexcept { return ready_ && !faulted_; }
    bool shutdown_required() const noexcept { return device_ || window_ || video_owned_; }
    bool frame_outstanding() const noexcept { return active_.commands != nullptr; }
    bool faulted() const noexcept { return faulted_; }
    SDL_GPUDevice* device() const noexcept { return device_; } // borrowed, child creation only
    SDL_GPUTextureFormat swapchain_format() const noexcept;
    bool anisotropy_enabled() const noexcept { return initialized() && anisotropy_enabled_; }
    std::uint32_t window_id() const noexcept;
    const char* last_error() const noexcept { return error_; }
    // Initialization rollback can fail to establish idle. Retain both causes.
    const char* last_cleanup_error() const noexcept { return cleanup_error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    Status sdl_failure() noexcept;
    Status check_thread() noexcept;
    Status validate_frame(const GpuFrame& frame) noexcept;
    Status initialization_failure(Status cause) noexcept;
    void consume_frame(GpuFrame& frame) noexcept;
    SDL_Window* window_{};
    SDL_GPUDevice* device_{};
    GpuFrame active_{};
    GpuFrame* active_frame_{};
    std::uint64_t owner_thread_{};
    bool video_owned_{}, claimed_{}, ready_{}, faulted_{};
    bool anisotropy_enabled_{};
    char title_[title_bytes]{}, error_[256]{}, cleanup_error_[256]{};
};

} // namespace cedar
