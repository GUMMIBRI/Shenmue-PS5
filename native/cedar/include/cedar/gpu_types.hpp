// Original explicit input format for the native GPU facility, not game state.
#pragma once
#include "cedar/platform.hpp"
#include <SDL3/SDL_gpu.h>

namespace cedar {
struct GpuVertex {
    float x{}, y{}, z{};
    float u{}, v{};
    std::uint8_t rgba[4]{};
};
static_assert(sizeof(GpuVertex) == 24, "GPU vertex/shader layout must stay explicit");
// Optional independent lighting stream; the original unlit vertex ABI is intact.
struct GpuNormal { float x{}, y{}, z{}; };
static_assert(sizeof(GpuNormal) == 12, "GPU normal stream must remain float3/12 bytes");
enum class GpuResourcePhase : std::uint8_t { empty, uploading, ready, failed };
// Borrowed frame handles valid only until the context submits/cancels this frame.
struct GpuFrame {
    SDL_GPUDevice* device{};
    SDL_GPUCommandBuffer* commands{};
    SDL_GPUTexture* swapchain{};
    SDL_GPUTextureFormat color_format{SDL_GPU_TEXTUREFORMAT_INVALID};
    std::uint32_t width{}, height{};
};
} // namespace cedar
