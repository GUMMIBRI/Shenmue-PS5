// Original native SDL3 GPU texture upload facility, not game rendering.
#pragma once
#include "cedar/gpu_types.hpp"

namespace cedar {
enum class GpuTextureEncoding : std::uint8_t { unorm, srgb };
enum class GpuTextureFilter : std::uint8_t { nearest, linear };
enum class GpuTextureMipFilter : std::uint8_t { nearest, linear };
enum class GpuTextureAddress : std::uint8_t { repeat, mirrored_repeat, clamp_to_edge };
struct GpuTextureSampling {
    GpuTextureFilter min_filter{GpuTextureFilter::nearest};
    GpuTextureFilter mag_filter{GpuTextureFilter::nearest};
    GpuTextureAddress u{GpuTextureAddress::clamp_to_edge};
    GpuTextureAddress v{GpuTextureAddress::clamp_to_edge};
    GpuTextureMipFilter mip_filter{GpuTextureMipFilter::nearest};
    std::uint8_t anisotropy{1};
    // Required for >1x. Caller confirms the actual device was created with
    // samplerAnisotropy enabled; selection alone is not capability evidence.
    bool anisotropy_feature_confirmed{};
};

// Borrows a live device that must outlive this object and every resource use.
// All calls/destruction belong to its owning main/GPU thread. No device/window,
// event loop, shader or renderer ownership. Caller-supplied2D RGBA8 mips only. Caller
// explicitly chooses transfer encoding and sampling; alpha/origin are unchanged.
class GpuTexture final {
public:
    static constexpr std::uint32_t max_dimension = 16384;
    static constexpr std::size_t max_upload_bytes = 64u * 1024u * 1024u;
    static constexpr std::size_t max_mip_levels = 15; // Complete 16384 -> 1 chain.
    GpuTexture() noexcept = default;
    ~GpuTexture();
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    // Requires empty state. Copies source rows into its native transfer buffer
    // before returning, including BGRA->RGBA channel reorder when requested by
    // SurfaceView. Source stays immutable and is no longer needed after return.
    // Success means submission accepted, phase uploading; it is NOT ready yet.
    Status begin_upload(SDL_GPUDevice* device, const SurfaceView& surface,
                         GpuTextureEncoding encoding, const GpuTextureSampling& sampling) noexcept;
    // Exact successive max(1,dimension/2) dimensions, common source format.
    // Partial chains allowed; cannot repeat1x1 or exceed the natural chain.
    // Preflights ALL descriptors/source spans/64MiB combined budget before any
    // transfer writes. Source read spans must not overlap each other, descriptors
    // or this object. Caller retains immutable descriptors/pixels through return.
    Status begin_upload_mips(SDL_GPUDevice* device, const SurfaceView* levels,
                              std::size_t level_count, GpuTextureEncoding encoding,
                              const GpuTextureSampling& sampling) noexcept;
    // Nonblocking. unavailable means fence has not signaled; ok means ready.
    Status poll() noexcept;
    // Explicit caller-requested blocking wait for this upload's fence only.
    Status wait() noexcept;
    // Never marks a pending upload ready. Releases owned handles using SDL's
    // safe/deferred GPU disposal APIs; no wait or global device-idle operation.
    // Returned texture/sampler handles must not be referenced after shutdown.
    void shutdown() noexcept;
    GpuResourcePhase phase() const noexcept { return phase_; }
    SDL_GPUTexture* texture() const noexcept { return phase_ == GpuResourcePhase::ready ? texture_ : nullptr; }
    SDL_GPUSampler* sampler() const noexcept { return phase_ == GpuResourcePhase::ready ? sampler_ : nullptr; }
    SDL_GPUDevice* device() const noexcept { return device_; } // Borrowed; never destroyed.
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    std::uint32_t mip_count() const noexcept { return mip_count_; }
    SDL_GPUTextureFormat format() const noexcept { return format_; }
    const char* last_error() const noexcept { return error_; }
private:
    Status fail(Status status, const char* message) noexcept;
    Status sdl_failure() noexcept;
    Status check_thread() noexcept;
    void release_resources() noexcept;
    void finish_upload() noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUTexture* texture_{};
    SDL_GPUSampler* sampler_{};
    SDL_GPUTransferBuffer* transfer_{};
    SDL_GPUFence* fence_{};
    GpuResourcePhase phase_{GpuResourcePhase::empty};
    SDL_GPUTextureFormat format_{SDL_GPU_TEXTUREFORMAT_INVALID};
    std::uint32_t width_{}, height_{}, mip_count_{};
    std::uint64_t owner_thread_{};
    char error_[256]{};
};
} // namespace cedar
