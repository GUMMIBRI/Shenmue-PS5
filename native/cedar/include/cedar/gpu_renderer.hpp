#pragma once
#include "cedar/gpu_mesh.hpp"
#include "cedar/gpu_texture.hpp"

namespace cedar {
enum class GpuBlendMode : std::uint8_t { opaque, straight_alpha, premultiplied_alpha };
enum class GpuDepthMode : std::uint8_t { disabled, less_write, less_equal_write };
enum class GpuLightingMode : std::uint8_t { unlit, ambient_directional };
enum class GpuColorTransfer : std::uint8_t { unknown, linear_sdr, perceptual_sdr };
struct GpuLighting {
    GpuColorTransfer texture_transfer{GpuColorTransfer::unknown};
    GpuColorTransfer vertex_transfer{GpuColorTransfer::unknown};
    float direction_to_light[3]{}; // World coordinates; finite nonzero required.
    float ambient[3]{}, diffuse[3]{}; // Explicit linear-light RGB, each [0,1].
    bool has_normal_rotation{};
    float normal_rotation[9]{}; // Column-major proper orthonormal rotation only.
};
struct GpuRendererConfig {
    SDL_GPUTextureFormat color_format{SDL_GPU_TEXTUREFORMAT_INVALID};
    GpuBlendMode blend{GpuBlendMode::opaque};
    GpuDepthMode depth{GpuDepthMode::disabled};
    SDL_GPUCullMode cull{SDL_GPU_CULLMODE_NONE};
    SDL_GPUFrontFace front_face{SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE};
    GpuLightingMode lighting{GpuLightingMode::unlit};
    GpuColorTransfer output_transfer{GpuColorTransfer::unknown};
};
struct GpuDraw {
    const GpuMesh* mesh{};
    const GpuTexture* texture{};
    std::uint32_t first_vertex{}, vertex_count{};
    // Explicit column-major clip-from-uploaded-reference-world matrix, Vulkan
    // Z range [0,1]; may include an explicit caller-owned rigid world delta.
    // No default camera, coordinate flip, joint pose or projection is invented.
    float view_projection[16]{};
    GpuLighting light{}; // Used only by the explicitly lit pipeline.
};
struct GpuPassClear { float rgba[4]{}; float depth{}; };

// Textured triangles with optional explicit Lambert light; not Shenmue materials.
// Caller chooses camera/blend/depth/cull, uploaded mesh/texture, and draw order.
// Does not acquire, submit, cancel, route or present frames. All operations and
// destruction are on the owning GPU/main thread; device outlives this object.
class GpuRenderer final {
public:
    GpuRenderer() noexcept = default;
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;
    Status initialize(SDL_GPUDevice* device, const GpuRendererConfig& config) noexcept;
    Status record(const GpuFrame& frame, const GpuDraw* draws, std::size_t count,
                  const GpuPassClear& clear) noexcept;
    // Real dedicated producer target, before post-processing. Caller owns the
    // camera/projection and texture format/lifetime; no swapchain is acquired.
    Status record_to_texture(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* color,
                             std::uint32_t width, std::uint32_t height,
                             const GpuDraw* draws, std::size_t count,
                             const GpuPassClear& clear) noexcept;
    SDL_GPUDevice* device() const noexcept { return device_; }
    SDL_GPUTextureFormat color_format() const noexcept { return config_.color_format; }
    GpuLightingMode lighting_mode() const noexcept { return config_.lighting; }
    GpuColorTransfer output_transfer() const noexcept { return config_.output_transfer; }
    void shutdown() noexcept;
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    Status native_failure() noexcept;
    Status ensure_depth(std::uint32_t width, std::uint32_t height) noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUGraphicsPipeline* pipeline_{};
    SDL_GPUTexture* depth_{};
    GpuRendererConfig config_{};
    std::uint32_t depth_width_{}, depth_height_{};
    char error_[256]{};
};
} // namespace cedar
