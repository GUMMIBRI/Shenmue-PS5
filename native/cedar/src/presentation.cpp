// Original Cedar adapter using public SDL3 APIs, not SDL implementation code.
#include "cedar/presentation.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <limits>

namespace cedar {
Presentation::~Presentation() { shutdown(); }
std::uint32_t Presentation::window_id() const noexcept {
    return window_ ? SDL_GetWindowID(window_) : 0;
}
Status Presentation::fail(Status status, const char* message) noexcept {
    std::size_t i = 0;
    if (message) {
        for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    }
    error_[i] = '\0';
    return status;
}
Status Presentation::sdl_failure() noexcept {
    const char* message = SDL_GetError();
    return fail(Status::backend_failure,
                message && *message ? message : "SDL presentation operation failed");
}
Status Presentation::initialize(const PresentationConfig& config) noexcept {
    if (video_owned_ || window_ || renderer_)
        return fail(Status::invalid_state, "Presentation is already initialized");
    if (!config.title || config.width <= 0 || config.height <= 0)
        return fail(Status::invalid_argument, "Invalid presentation configuration");
    error_[0] = '\0';
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return sdl_failure();
    video_owned_ = true;
    const SDL_WindowFlags flags = config.fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_RESIZABLE;
    window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
    if (!window_) {
        const auto status = sdl_failure();
        shutdown();
        return status;
    }
    renderer_ = SDL_CreateRenderer(window_, config.renderer_name);
    if (!renderer_) {
        const auto status = sdl_failure();
        shutdown();
        return status;
    }
    linear_filter_ = config.linear_filter;
    return Status::ok;
}
Status Presentation::upload(const SurfaceView& surface) noexcept {
    if (!renderer_) return fail(Status::invalid_state, "Presentation is not initialized");
    if (validate_surface(surface) != Status::ok ||
        surface.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        surface.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        surface.pitch > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return fail(Status::invalid_argument, "Invalid presentation surface");
    const bool replace = !texture_ || width_ != surface.width ||
        height_ != surface.height || format_ != surface.format;
    SDL_Texture* target = texture_;
    if (replace) {
        // Array formats describe byte order independently of host endianness.
        const SDL_PixelFormat native_format = surface.format == PixelFormat::rgba8 ?
            SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_BGRA32;
        target = SDL_CreateTexture(renderer_, native_format, SDL_TEXTUREACCESS_STREAMING,
                                   static_cast<int>(surface.width), static_cast<int>(surface.height));
        if (!target) return sdl_failure();
        if (!SDL_SetTextureScaleMode(target, linear_filter_ ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST) ||
            !SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE)) {
            const auto status = sdl_failure();
            SDL_DestroyTexture(target);
            return status;
        }
    }
    if (!SDL_UpdateTexture(target, nullptr, surface.pixels, static_cast<int>(surface.pitch))) {
        const auto status = sdl_failure();
        if (replace) SDL_DestroyTexture(target);
        else uploaded_ = false; // A failed partial update must not be presented.
        return status;
    }
    if (replace) {
        // Keep the previous valid texture until its replacement is fully uploaded.
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = target;
        width_ = surface.width;
        height_ = surface.height;
        format_ = surface.format;
    }
    uploaded_ = true;
    error_[0] = '\0';
    return Status::ok;
}
Status Presentation::present() noexcept {
    if (!renderer_ || !texture_ || !uploaded_)
        return fail(Status::invalid_state, "No complete presentation surface uploaded");
    int output_w = 0, output_h = 0;
    if (!SDL_GetRenderOutputSize(renderer_, &output_w, &output_h)) return sdl_failure();
    if (output_w <= 0 || output_h <= 0)
        return fail(Status::unavailable, "Presentation output is not drawable");
    const double scale_x = static_cast<double>(output_w) / width_;
    const double scale_y = static_cast<double>(output_h) / height_;
    const double scale = scale_x < scale_y ? scale_x : scale_y;
    const float w = static_cast<float>(width_ * scale);
    const float h = static_cast<float>(height_ * scale);
    const SDL_FRect destination{(static_cast<float>(output_w) - w) * 0.5f,
                               (static_cast<float>(output_h) - h) * 0.5f, w, h};
    if (!SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) || !SDL_RenderClear(renderer_) ||
        !SDL_RenderTexture(renderer_, texture_, nullptr, &destination) ||
        !SDL_RenderPresent(renderer_)) return sdl_failure();
    error_[0] = '\0';
    return Status::ok;
}
void Presentation::shutdown() noexcept {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
    width_ = height_ = 0;
    uploaded_ = false;
    if (video_owned_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    video_owned_ = false;
}
} // namespace cedar
