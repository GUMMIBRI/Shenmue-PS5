// Original Cedar SDL3 presentation facility; no game renderer or recovered logic.
#pragma once
#include "cedar/platform.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace cedar {
struct PresentationConfig {
    const char* title{"Cedar platform"};
    int width{1280};
    int height{720};
    // Null requests SDL's available renderer. A named unavailable driver fails.
    const char* renderer_name{};
    bool fullscreen{};
    bool linear_filter{};
};

// All operations, including destruction, belong to the SDL video/main thread.
// Owns its window/renderer and one ref-counted SDL video initialization only.
// Callers retain their surface storage until upload() returns. No event pumping,
// game simulation, asset decoding, native launch or SDL global shutdown is done.
class Presentation final {
public:
    Presentation() noexcept = default;
    ~Presentation();
    Presentation(const Presentation&) = delete;
    Presentation& operator=(const Presentation&) = delete;
    Status initialize(const PresentationConfig& config) noexcept;
    Status upload(const SurfaceView& surface) noexcept;
    Status present() noexcept;
    void shutdown() noexcept;
    bool initialized() const noexcept { return renderer_ != nullptr; }
    std::uint32_t window_id() const noexcept;
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    Status sdl_failure() noexcept;
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    bool video_owned_{}, linear_filter_{}, uploaded_{};
    std::uint32_t width_{}, height_{};
    PixelFormat format_{PixelFormat::rgba8};
    char error_[256]{};
};
} // namespace cedar
