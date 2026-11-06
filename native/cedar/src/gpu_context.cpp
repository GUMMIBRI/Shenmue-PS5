#include "cedar/gpu_context.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>
#include <cstring>

namespace cedar {
namespace {
void copy_error(char (&output)[256], const char* message) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(output) && message[i]; ++i) output[i] = message[i];
    output[i] = '\0';
}
bool empty(const GpuFrame& frame) noexcept {
    return !frame.device && !frame.commands && !frame.swapchain &&
        frame.color_format == SDL_GPU_TEXTUREFORMAT_INVALID && !frame.width && !frame.height;
}
}

Status GpuContext::fail(Status status, const char* message) noexcept {
    copy_error(error_, message);
    return status;
}
Status GpuContext::sdl_failure() noexcept {
    const auto* message = SDL_GetError();
    return fail(Status::backend_failure, message && *message ? message : "SDL GPU operation failed");
}
Status GpuContext::check_thread() noexcept {
    if (!SDL_IsMainThread() || (owner_thread_ && SDL_GetCurrentThreadID() != owner_thread_))
        return fail(Status::invalid_state, "GPU context operations require its creating SDL main/video thread");
    return Status::ok;
}

Status GpuContext::initialization_failure(Status cause) noexcept {
    char original[256];
    copy_error(original, error_);
    const auto closed = shutdown();
    if (closed != Status::ok) copy_error(cleanup_error_, error_);
    else cleanup_error_[0] = '\0';
    copy_error(error_, original);
    return cause;
}

Status GpuContext::initialize(const GpuContextConfig& config) noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (shutdown_required() || frame_outstanding())
        return fail(Status::invalid_state, "GPU context still owns resources; explicit shutdown is required");
    if (!config.title || config.width <= 0 || config.height <= 0 ||
        (config.video != GpuVideoOwnership::acquire_reference &&
         config.video != GpuVideoOwnership::require_initialized))
        return fail(Status::invalid_argument, "Invalid GPU context configuration");
    std::size_t title_length = 0;
    while (title_length < sizeof(title_) && config.title[title_length]) ++title_length;
    if (title_length == sizeof(title_))
        return fail(Status::invalid_argument, "GPU window title exceeds the 255-byte limit");
    std::memmove(title_, config.title, title_length + 1);
    error_[0] = cleanup_error_[0] = '\0';
    owner_thread_ = SDL_GetCurrentThreadID();
    faulted_ = false;
    if (config.video == GpuVideoOwnership::acquire_reference) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            const auto cause = sdl_failure();
            owner_thread_ = 0;
            return cause;
        }
        video_owned_ = true;
    } else if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        owner_thread_ = 0;
        return fail(Status::invalid_state, "Caller-owned SDL video subsystem is not initialized");
    }
    SDL_WindowFlags flags = 0;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    window_ = SDL_CreateWindow(title_, config.width, config.height, flags);
    if (!window_) return initialization_failure(sdl_failure());
    const auto properties = SDL_CreateProperties();
    if (!properties) return initialization_failure(sdl_failure());
    const bool configured = SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan") &&
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, config.debug) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, config.enable_anisotropy);
    if (!configured) {
        const auto cause = sdl_failure(); SDL_DestroyProperties(properties);
        return initialization_failure(cause);
    }
    device_ = SDL_CreateGPUDeviceWithProperties(properties);
    if (!device_) {
        const auto cause = sdl_failure(); SDL_DestroyProperties(properties);
        return initialization_failure(cause);
    }
    SDL_DestroyProperties(properties);
    anisotropy_enabled_ = config.enable_anisotropy;
    const auto* driver = SDL_GetGPUDeviceDriver(device_);
    if (!driver || std::strcmp(driver, "vulkan") != 0 ||
        !(SDL_GetGPUShaderFormats(device_) & SDL_GPU_SHADERFORMAT_SPIRV))
        return initialization_failure(fail(Status::unavailable, "Selected GPU device does not provide Vulkan/SPIR-V"));
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) return initialization_failure(sdl_failure());
    claimed_ = true;
    if (SDL_GetGPUSwapchainTextureFormat(device_, window_) == SDL_GPU_TEXTUREFORMAT_INVALID)
        return initialization_failure(fail(Status::backend_failure, "SDL returned an invalid claimed swapchain format"));
    ready_ = true;
    return Status::ok;
}

Status GpuContext::begin_frame(GpuFrame& frame, bool& drawable) noexcept {
    drawable = false;
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (!initialized()) return fail(Status::invalid_state, "GPU context is not ready for frames");
    if (frame_outstanding()) return fail(Status::invalid_state, "A GPU frame is already outstanding");
    if (!empty(frame)) return fail(Status::invalid_argument, "begin_frame requires an empty caller frame");
    auto* commands = SDL_AcquireGPUCommandBuffer(device_);
    if (!commands) return sdl_failure();
    active_ = {device_, commands, nullptr, SDL_GPU_TEXTUREFORMAT_INVALID, 0, 0};
    active_frame_ = &frame;
    // Publish ownership immediately: failed acquisition must not hide a live
    // command reference from the owner, even though it cannot draw that frame.
    frame = active_;
    const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(commands, window_,
        &active_.swapchain, &active_.width, &active_.height);
    if (!acquired) {
        const auto status = sdl_failure();
        frame = active_;
        faulted_ = true;
        return status; // Explicit cancel if no swapchain, otherwise explicit submit.
    }
    if (!active_.swapchain) {
        // Minimized/not drawable is a legitimate result. Nothing was acquired
        // for presentation, so this is the SDL-permitted cancellation path.
        const bool cancelled = SDL_CancelGPUCommandBuffer(commands);
        if (!cancelled) {
            const auto status = sdl_failure();
            consume_frame(frame);
            faulted_ = true;
            return status;
        }
        consume_frame(frame);
        error_[0] = '\0';
        return Status::ok;
    }
    active_.color_format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    frame = active_;
    if (!active_.width || !active_.height || active_.color_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        faulted_ = true;
        return fail(Status::backend_failure, "Acquired swapchain has invalid dimensions or format; explicit submit is required");
    }
    drawable = true;
    error_[0] = '\0';
    return Status::ok;
}

Status GpuContext::validate_frame(const GpuFrame& frame) noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (!device_ || !frame_outstanding() || active_frame_ != &frame ||
        frame.device != device_ || frame.device != active_.device ||
        frame.commands != active_.commands || frame.swapchain != active_.swapchain ||
        frame.color_format != active_.color_format || frame.width != active_.width || frame.height != active_.height)
        return fail(Status::invalid_argument, "GPU frame does not match this context's exact outstanding frame");
    return Status::ok;
}
void GpuContext::consume_frame(GpuFrame& frame) noexcept {
    frame = {};
    active_ = {};
    active_frame_ = nullptr;
}
Status GpuContext::submit(GpuFrame& frame) noexcept {
    const auto validated = validate_frame(frame);
    if (validated != Status::ok) return validated;
    const bool submitted = SDL_SubmitGPUCommandBuffer(active_.commands);
    if (!submitted) {
        const auto status = sdl_failure();
        consume_frame(frame); // SDL forbids referencing the command after calling submit.
        faulted_ = true;
        return status;
    }
    consume_frame(frame);
    error_[0] = '\0';
    return Status::ok;
}
Status GpuContext::cancel(GpuFrame& frame) noexcept {
    const auto validated = validate_frame(frame);
    if (validated != Status::ok) return validated;
    if (active_.swapchain)
        return fail(Status::invalid_state, "SDL forbids cancelling a swapchain-acquired frame; submit it explicitly");
    const bool cancelled = SDL_CancelGPUCommandBuffer(active_.commands);
    if (!cancelled) {
        const auto status = sdl_failure();
        consume_frame(frame);
        faulted_ = true;
        return status;
    }
    consume_frame(frame);
    error_[0] = '\0';
    return Status::ok;
}

Status GpuContext::wait_idle() noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (!device_) return fail(Status::invalid_state, "GPU device is not initialized");
    if (frame_outstanding()) return fail(Status::invalid_state, "Consume the outstanding GPU frame before waiting idle");
    if (!SDL_WaitForGPUIdle(device_)) {
        faulted_ = true;
        return sdl_failure(); // Retain every resource for explicit close/retry.
    }
    error_[0] = '\0';
    return Status::ok; // Does not clear a prior fault to pretend recovery.
}
Status GpuContext::shutdown() noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (frame_outstanding()) return fail(Status::invalid_state, "Explicitly submit/cancel the outstanding GPU frame before shutdown");
    if (device_ && !SDL_WaitForGPUIdle(device_)) {
        faulted_ = true;
        return sdl_failure(); // No window/device/video ownership is released.
    }
    if (claimed_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
    claimed_ = false;
    if (device_) SDL_DestroyGPUDevice(device_);
    device_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (video_owned_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    video_owned_ = ready_ = faulted_ = anisotropy_enabled_ = false;
    owner_thread_ = 0;
    error_[0] = cleanup_error_[0] = '\0';
    return Status::ok;
}

std::uint32_t GpuContext::window_id() const noexcept {
    if (!window_ || !SDL_IsMainThread() || SDL_GetCurrentThreadID() != owner_thread_) return 0;
    return SDL_GetWindowID(window_);
}

SDL_GPUTextureFormat GpuContext::swapchain_format() const noexcept {
    if (!initialized() || !window_ || !SDL_IsMainThread() || SDL_GetCurrentThreadID() != owner_thread_)
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    return SDL_GetGPUSwapchainTextureFormat(device_, window_);
}

} // namespace cedar
