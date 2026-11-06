// Original native SDL3 GPU upload composition; no SDL implementation copied.
#include "cedar/gpu_texture.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_thread.h>
#include <cstring>
#include <limits>

namespace cedar {
namespace {
bool valid_filter(GpuTextureFilter value) noexcept {
    return value == GpuTextureFilter::nearest || value == GpuTextureFilter::linear;
}
bool valid_mip_filter(GpuTextureMipFilter value) noexcept {
    return value == GpuTextureMipFilter::nearest || value == GpuTextureMipFilter::linear;
}
bool valid_address(GpuTextureAddress value) noexcept {
    return value == GpuTextureAddress::repeat || value == GpuTextureAddress::mirrored_repeat ||
        value == GpuTextureAddress::clamp_to_edge;
}
SDL_GPUFilter native_filter(GpuTextureFilter value) noexcept {
    return value == GpuTextureFilter::linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
}
SDL_GPUSamplerAddressMode native_address(GpuTextureAddress value) noexcept {
    if (value == GpuTextureAddress::repeat) return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    if (value == GpuTextureAddress::mirrored_repeat) return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}
bool address_range(const void* pointer, std::size_t length, std::uintptr_t& begin,
                    std::uintptr_t& end) noexcept {
    begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    end = begin + length;
    return true;
}
bool overlap(std::uintptr_t a, std::uintptr_t ae, std::uintptr_t b, std::uintptr_t be) noexcept {
    return a < be && b < ae;
}
struct UploadLevel {
    SurfaceView surface{};
    std::size_t row_bytes{}, bytes{}, offset{};
    std::uintptr_t source_begin{}, source_end{};
};
} // namespace

GpuTexture::~GpuTexture() { shutdown(); }
Status GpuTexture::fail(Status status, const char* message) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    error_[i] = '\0';
    return status;
}
Status GpuTexture::sdl_failure() noexcept {
    const char* message = SDL_GetError();
    return fail(Status::backend_failure, message && *message ? message : "SDL GPU texture operation failed");
}
Status GpuTexture::check_thread() noexcept {
    if (!SDL_IsMainThread() || (owner_thread_ && SDL_GetCurrentThreadID() != owner_thread_))
        return fail(Status::invalid_state, "GPU texture requires its owning main/GPU thread");
    return Status::ok;
}
void GpuTexture::release_resources() noexcept {
    if (device_) {
        // Fence observation is no longer needed on disposal. SDL resource release
        // calls defer actual destruction until safe; no completion is fabricated.
        if (fence_) SDL_ReleaseGPUFence(device_, fence_);
        if (transfer_) SDL_ReleaseGPUTransferBuffer(device_, transfer_);
        if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        if (texture_) SDL_ReleaseGPUTexture(device_, texture_);
    }
    fence_ = nullptr;
    transfer_ = nullptr;
    sampler_ = nullptr;
    texture_ = nullptr;
}
void GpuTexture::shutdown() noexcept {
    if (device_ && check_thread() != Status::ok) return; // Retain ownership on wrong-thread calls.
    release_resources();
    device_ = nullptr;
    phase_ = GpuResourcePhase::empty;
    format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    width_ = height_ = mip_count_ = 0;
    owner_thread_ = 0;
}
void GpuTexture::finish_upload() noexcept {
    // Called only after query/wait positively confirms this submission's fence.
    SDL_ReleaseGPUFence(device_, fence_);
    fence_ = nullptr;
    SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    transfer_ = nullptr;
    phase_ = GpuResourcePhase::ready;
    error_[0] = '\0';
}
Status GpuTexture::begin_upload(SDL_GPUDevice* device, const SurfaceView& surface,
                                GpuTextureEncoding encoding, const GpuTextureSampling& sampling) noexcept {
    return begin_upload_mips(device, &surface, 1, encoding, sampling);
}
Status GpuTexture::begin_upload_mips(SDL_GPUDevice* device, const SurfaceView* surfaces,
    std::size_t level_count, GpuTextureEncoding encoding, const GpuTextureSampling& sampling) noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (phase_ != GpuResourcePhase::empty)
        return fail(Status::invalid_state, "Texture must be shut down before another upload");
    if (!device || !surfaces || !level_count || level_count > max_mip_levels ||
        (encoding != GpuTextureEncoding::unorm && encoding != GpuTextureEncoding::srgb) ||
        !valid_filter(sampling.min_filter) || !valid_filter(sampling.mag_filter) ||
        !valid_mip_filter(sampling.mip_filter) || !valid_address(sampling.u) || !valid_address(sampling.v) ||
        sampling.anisotropy < 1 || sampling.anisotropy > 16)
        return fail(Status::invalid_argument, "Invalid GPU texture upload arguments");
    if (sampling.anisotropy > 1 && !sampling.anisotropy_feature_confirmed)
        return fail(Status::unavailable, "Anisotropic sampling requires confirmed enabled device feature");
    // Snapshot all descriptors and sampling before constructing native resources.
    // Source image storage remains immutable until this function returns.
    std::uintptr_t descriptor_begin, descriptor_end, self_begin, self_end, sampling_begin, sampling_end;
    if (!address_range(surfaces, level_count * sizeof(SurfaceView), descriptor_begin, descriptor_end) ||
        !address_range(this, sizeof(*this), self_begin, self_end) ||
        !address_range(&sampling, sizeof(sampling), sampling_begin, sampling_end) ||
        overlap(descriptor_begin, descriptor_end, self_begin, self_end) ||
        overlap(sampling_begin, sampling_end, self_begin, self_end))
        return fail(Status::invalid_argument, "Invalid or overlapping GPU texture descriptors");
    const auto policy = sampling;
    UploadLevel levels[max_mip_levels];
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < level_count; ++i) {
        auto& level = levels[i];
        level.surface = surfaces[i];
        const auto& surface = level.surface;
        if (validate_surface(surface) != Status::ok || surface.width > max_dimension || surface.height > max_dimension)
            return fail(Status::invalid_argument, "Invalid GPU mip surface bounds, format or dimensions");
        if (i) {
            const auto& previous = levels[i - 1].surface;
            if ((previous.width == 1 && previous.height == 1) ||
                surface.width != (previous.width > 1 ? previous.width / 2 : 1) ||
                surface.height != (previous.height > 1 ? previous.height / 2 : 1) ||
                surface.format != levels[0].surface.format)
                return fail(Status::invalid_argument, "GPU mip chain must halve dimensions exactly with one common format");
        }
        level.row_bytes = static_cast<std::size_t>(surface.width) * 4;
        if (level.row_bytes / 4 != surface.width || level.row_bytes > max_upload_bytes / surface.height)
            return fail(Status::invalid_argument, "GPU mip upload exceeds the64MiB combined limit");
        level.bytes = level.row_bytes * surface.height;
        if (level.bytes > max_upload_bytes - bytes)
            return fail(Status::invalid_argument, "GPU mip upload exceeds the64MiB combined limit");
        // SDL_UploadToGPUTexture requires texel-size alignment. Every tightly
        // repacked RGBA8 level has4-byte texels/length, so its offset is aligned.
        level.offset = bytes;
        if (level.offset % 4)
            return fail(Status::invalid_argument, "GPU mip transfer offset is not RGBA8 texel aligned");
        bytes += level.bytes;
        const auto read_bytes = surface.pitch * (surface.height - 1) + level.row_bytes;
        std::uintptr_t declared_begin, declared_end;
        if (!address_range(surface.pixels, surface.bytes, declared_begin, declared_end) ||
            !address_range(surface.pixels, read_bytes, level.source_begin, level.source_end) ||
            overlap(level.source_begin, level.source_end, self_begin, self_end) ||
            overlap(level.source_begin, level.source_end, descriptor_begin, descriptor_end) ||
            overlap(level.source_begin, level.source_end, sampling_begin, sampling_end))
            return fail(Status::invalid_argument, "Invalid or aliased GPU mip source storage");
        for (std::size_t previous = 0; previous < i; ++previous)
            if (overlap(level.source_begin, level.source_end, levels[previous].source_begin, levels[previous].source_end))
                return fail(Status::invalid_argument, "GPU mip source read spans overlap or duplicate another level");
    }
    const auto format = encoding == GpuTextureEncoding::srgb ?
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (!SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER))
        return fail(Status::unavailable, "GPU device does not support the requested RGBA8 sampling format");

    device_ = device;
    owner_thread_ = SDL_GetCurrentThreadID();
    width_ = levels[0].surface.width;
    height_ = levels[0].surface.height;
    mip_count_ = static_cast<std::uint32_t>(level_count);
    format_ = format;
    error_[0] = '\0';
    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width_;
    texture_info.height = height_;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = mip_count_;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ = SDL_CreateGPUTexture(device_, &texture_info);
    if (!texture_) {
        const auto status = sdl_failure();
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = native_filter(policy.min_filter);
    sampler_info.mag_filter = native_filter(policy.mag_filter);
    sampler_info.mipmap_mode = policy.mip_filter == GpuTextureMipFilter::linear ?
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = native_address(policy.u);
    sampler_info.address_mode_v = native_address(policy.v);
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.mip_lod_bias = 0;
    sampler_info.min_lod = 0;
    sampler_info.max_lod = static_cast<float>(mip_count_ - 1);
    sampler_info.max_anisotropy = static_cast<float>(policy.anisotropy);
    sampler_info.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    sampler_info.enable_anisotropy = policy.anisotropy > 1;
    sampler_info.enable_compare = false;
    sampler_ = SDL_CreateGPUSampler(device_, &sampler_info);
    if (!sampler_) {
        const auto status = sdl_failure();
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(bytes); // Bounded to64MiB above.
    transfer_ = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (!transfer_) {
        const auto status = sdl_failure();
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    auto* mapped = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device_, transfer_, false));
    if (!mapped) {
        const auto status = sdl_failure();
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    std::uintptr_t mapped_begin, mapped_end;
    if (!address_range(mapped, bytes, mapped_begin, mapped_end) ||
        overlap(mapped_begin, mapped_end, self_begin, self_end) ||
        overlap(mapped_begin, mapped_end, descriptor_begin, descriptor_end) ||
        overlap(mapped_begin, mapped_end, sampling_begin, sampling_end)) {
        SDL_UnmapGPUTransferBuffer(device_, transfer_);
        const auto status = fail(Status::invalid_argument, "Mapped GPU transfer storage overlaps upload metadata");
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    // Check EVERY mapped/source pair before writing even the first mip. This
    // prevents an early copy corrupting a later caller level on alias failure.
    for (std::size_t i = 0; i < level_count; ++i) {
        if (overlap(mapped_begin, mapped_end, levels[i].source_begin, levels[i].source_end)) {
            SDL_UnmapGPUTransferBuffer(device_, transfer_);
            const auto status = fail(Status::invalid_argument, "Mapped GPU transfer storage overlaps a mip source");
            release_resources();
            phase_ = GpuResourcePhase::failed;
            return status;
        }
    }
    for (std::size_t i = 0; i < level_count; ++i) {
        const auto& level = levels[i];
        const auto& surface = level.surface;
        for (std::uint32_t y = 0; y < surface.height; ++y) {
            const auto* source = surface.pixels + static_cast<std::size_t>(y) * surface.pitch;
            auto* destination = mapped + level.offset + static_cast<std::size_t>(y) * level.row_bytes;
            if (surface.format == PixelFormat::rgba8) std::memcpy(destination, source, level.row_bytes);
            else for (std::uint32_t x = 0; x < surface.width; ++x) {
                const std::size_t at = static_cast<std::size_t>(x) * 4;
                destination[at] = source[at + 2];
                destination[at + 1] = source[at + 1];
                destination[at + 2] = source[at];
                destination[at + 3] = source[at + 3];
            }
        }
    }
    SDL_UnmapGPUTransferBuffer(device_, transfer_); // Required before upload encoding.
    auto* commands = SDL_AcquireGPUCommandBuffer(device_);
    if (!commands) {
        const auto status = sdl_failure();
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    auto* pass = SDL_BeginGPUCopyPass(commands);
    if (!pass) {
        auto status = sdl_failure(); // Copy before cancellation can replace SDL error.
        if (!SDL_CancelGPUCommandBuffer(commands))
            status = fail(Status::backend_failure, "GPU copy-pass creation and command cancellation failed");
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    for (std::size_t i = 0; i < level_count; ++i) {
        SDL_GPUTextureTransferInfo source_info{};
        source_info.transfer_buffer = transfer_;
        source_info.offset = static_cast<Uint32>(levels[i].offset);
        source_info.pixels_per_row = levels[i].surface.width;
        source_info.rows_per_layer = levels[i].surface.height;
        SDL_GPUTextureRegion region{};
        region.texture = texture_;
        region.mip_level = static_cast<Uint32>(i);
        region.w = levels[i].surface.width;
        region.h = levels[i].surface.height;
        region.d = 1;
        SDL_UploadToGPUTexture(pass, &source_info, &region, false);
    }
    SDL_EndGPUCopyPass(pass);
    // Command buffer becomes invalid after this call, even on submission error.
    fence_ = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (!fence_) {
        const auto status = sdl_failure();
        release_resources();
        phase_ = GpuResourcePhase::failed;
        return status;
    }
    phase_ = GpuResourcePhase::uploading;
    return Status::ok;
}
Status GpuTexture::poll() noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (phase_ == GpuResourcePhase::ready) return Status::ok;
    if (phase_ != GpuResourcePhase::uploading || !device_ || !fence_)
        return fail(Status::invalid_state, "GPU texture has no pending upload");
    if (!SDL_QueryGPUFence(device_, fence_)) return Status::unavailable;
    finish_upload();
    return Status::ok;
}
Status GpuTexture::wait() noexcept {
    if (check_thread() != Status::ok) return Status::invalid_state;
    if (phase_ == GpuResourcePhase::ready) return Status::ok;
    if (phase_ != GpuResourcePhase::uploading || !device_ || !fence_)
        return fail(Status::invalid_state, "GPU texture has no pending upload");
    SDL_GPUFence* fences[]{fence_};
    if (!SDL_WaitForGPUFences(device_, true, fences, 1)) {
        phase_ = GpuResourcePhase::failed;
        return sdl_failure(); // Owned handles remain safely disposable by shutdown.
    }
    finish_upload();
    return Status::ok;
}
} // namespace cedar
