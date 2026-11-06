#pragma once
#include "cedar/gpu_types.hpp"

namespace cedar {
enum class FrameContent : std::uint8_t { game, movie, native_ui };
enum class PostColorEncoding : std::uint8_t { unknown, perceptual_sdr_unorm, linear_sdr, linear_hdr };
struct PixelRect { std::uint32_t x{}, y{}, width{}, height{}; };
// Texture/crop/content/encoding come from the actual producer, never heuristics.
// Caller retains GPU resources/device until all recorded GPU use has completed.
struct PostSource {
    SDL_GPUDevice* device{};
    SDL_GPUTexture* texture{};
    SDL_GPUTextureFormat format{SDL_GPU_TEXTUREFORMAT_INVALID};
    std::uint32_t width{}, height{};
    PixelRect crop{};
    FrameContent content{FrameContent::game};
    PostColorEncoding encoding{PostColorEncoding::unknown};
    bool opaque{}; // Actual producer assertion; required by RGB-only FSR1 output.
};
struct PostTarget {
    SDL_GPUDevice* device{};
    SDL_GPUTexture* texture{};
    SDL_GPUTextureFormat format{SDL_GPU_TEXTUREFORMAT_INVALID};
    std::uint32_t width{}, height{};
    PixelRect viewport{};
    SDL_GPULoadOp load_op{SDL_GPU_LOADOP_LOAD};
    SDL_FColor clear_color{};
};
} // namespace cedar
