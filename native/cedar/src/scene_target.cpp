#include "cedar/scene_target.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

namespace cedar {
namespace {
std::size_t bytes_per_pixel(SDL_GPUTextureFormat format) noexcept {
    if (format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM || format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) return 4;
    if (format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) return 8;
    return 0;
}
bool known_encoding(PostColorEncoding encoding) noexcept {
    return encoding == PostColorEncoding::perceptual_sdr_unorm || encoding == PostColorEncoding::linear_sdr ||
        encoding == PostColorEncoding::linear_hdr;
}
} // namespace

SceneTarget::~SceneTarget() { shutdown(); }
Status SceneTarget::fail(Status status, const char* message) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    error_[i] = '\0';
    return status;
}
Status SceneTarget::prepare(SDL_GPUDevice* device, const SceneTargetConfig& config) noexcept {
    if (!SDL_IsMainThread()) return fail(Status::invalid_state, "Scene target allocation requires the owning main/GPU thread");
    if (device_ && device != device_) return fail(Status::invalid_state, "Shut down the old scene target before changing GPU devices");
    const auto bpp = bytes_per_pixel(config.format);
    if (!device || !config.width || !config.height || config.width > max_dimension || config.height > max_dimension ||
        !bpp || !known_encoding(config.encoding) ||
        (config.encoding == PostColorEncoding::linear_hdr && config.format != SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT))
        return fail(Status::invalid_argument, "Explicit bounded scene extent, storage and known producer encoding required");
    const std::uint64_t bytes = static_cast<std::uint64_t>(config.width) * config.height * bpp;
    if (bytes > max_color_bytes) return fail(Status::invalid_argument, "Scene color target exceeds128MiB logical allocation budget");
    if (texture_ && config.width == config_.width && config.height == config_.height && config.format == config_.format) {
        // Transfer metadata is an explicit producer choice for the NEXT real
        // recording. Updating it does not classify/publish existing contents.
        config_ = config;
        error_[0] = '\0';
        return Status::ok;
    }
    const auto usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (!SDL_GPUTextureSupportsFormat(device, config.format, SDL_GPU_TEXTURETYPE_2D, usage))
        return fail(Status::unavailable, "Requested scene format does not support native sampling plus color rendering");
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = config.format;
    info.usage = usage;
    info.width = config.width;
    info.height = config.height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    auto* next = SDL_CreateGPUTexture(device, &info);
    if (!next) {
        const auto* error = SDL_GetError();
        return fail(Status::backend_failure, error && *error ? error : "Native scene color allocation failed");
    }
    // Keep the old target intact until the replacement actually exists. Its
    // pending driver use is retired through SDL's safe deferred release API.
    if (texture_) SDL_ReleaseGPUTexture(device_, texture_);
    texture_ = next;
    device_ = device;
    config_ = config;
    color_bytes_ = static_cast<std::size_t>(bytes);
    error_[0] = '\0';
    return Status::ok; // Allocation only; no PostSource or frame pixels exist yet.
}
Status SceneTarget::record_scene(SDL_GPUCommandBuffer* commands, GpuRenderer& renderer,
                                 const GpuDraw* draws, std::size_t count, const GpuPassClear& clear,
                                 FrameContent content, std::uint64_t serial, bool opaque_assertion,
                                 PostSource& source) noexcept {
    source = {};
    if (!SDL_IsMainThread() || !texture_ || !device_)
        return fail(Status::invalid_state, "A prepared scene target on its owning main/GPU thread is required");
    if (content != FrameContent::game) return fail(Status::unavailable, "This target records actual game geometry, not movie or UI producers");
    if (!commands || !serial || !draws || !count || count > 65536)
        return fail(Status::invalid_argument, "Explicit real command stream, nonzero frame serial and nonempty scene draws required");
    if (renderer.device() != device_ || renderer.color_format() != config_.format)
        return fail(Status::invalid_argument, "Scene target and actual renderer device/color format must match");
    if (renderer.lighting_mode() == GpuLightingMode::ambient_directional) {
        const auto actual = renderer.output_transfer() == GpuColorTransfer::perceptual_sdr ?
            PostColorEncoding::perceptual_sdr_unorm : PostColorEncoding::linear_sdr;
        if (config_.encoding != actual)
            return fail(Status::invalid_argument, "Scene transfer metadata must match the lit renderer output");
    }
    for (std::size_t i = 0; i < count; ++i)
        if (draws[i].texture && draws[i].texture->texture() == texture_)
            return fail(Status::invalid_argument, "Scene target cannot sample its own active color attachment");
    const auto status = renderer.record_to_texture(commands, texture_, config_.width, config_.height, draws, count, clear);
    if (status != Status::ok) return fail(status, renderer.last_error());
    PostSource recorded;
    recorded.device = device_;
    recorded.texture = texture_;
    recorded.format = config_.format;
    recorded.width = config_.width;
    recorded.height = config_.height;
    recorded.crop = {0, 0, config_.width, config_.height};
    recorded.content = FrameContent::game;
    recorded.encoding = config_.encoding;
    recorded.opaque = opaque_assertion;
    source = recorded;
    error_[0] = '\0';
    return Status::ok; // Real scene draw commands recorded, not submitted/completed.
}
void SceneTarget::shutdown() noexcept {
    if (device_ && texture_) SDL_ReleaseGPUTexture(device_, texture_);
    texture_ = nullptr;
    device_ = nullptr;
    config_ = {};
    color_bytes_ = 0;
}
} // namespace cedar
