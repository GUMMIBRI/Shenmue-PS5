// SPDX-License-Identifier: MPL-2.0
// Native movie-only facility; cleanup preset/filter mapping is source-informed
// by PE1 platform/movie_cleanup. Full provenance/notices: docs/MOVIE-EFFECTS.md
// and third_party/licenses/PE1-platform-MPL2.txt. No PE1 game code/art imported.
#include "cedar/movie_effects.hpp"
#include "cedar_post_shaders.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <cmath>

namespace cedar {
namespace {
struct alignas(16) MovieUniforms {
    float src_rect[4]{};
    float sample_clamp[4]{};
    float dst_rect[4]{};
    float texel_mode[4]{};
    float step_dim[4]{};
};
static_assert(sizeof(MovieUniforms) == 80, "Movie GLSL/std140 layout mismatch");
constexpr std::uint32_t max_extent = 16384;
constexpr std::uint64_t max_pixels = 16u * 1024u * 1024u;
bool bounded_rect(const PixelRect& rect, std::uint32_t width, std::uint32_t height) noexcept {
    return rect.width && rect.height && rect.x <= width && rect.y <= height &&
        rect.width <= width - rect.x && rect.height <= height - rect.y;
}
bool numeric_format(SDL_GPUTextureFormat format) noexcept {
    // Native sRGB storage is deliberately absent: sampling/target sRGB conversion
    // must not silently duplicate the explicit producer transfer contract.
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
        format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
        format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
}
MovieUniforms parameters(std::uint32_t width, std::uint32_t height, const PixelRect& crop,
                         const SDL_FRect& destination) noexcept {
    MovieUniforms uniforms;
    const float iw = 1.0f / static_cast<float>(width);
    const float ih = 1.0f / static_cast<float>(height);
    uniforms.src_rect[0] = static_cast<float>(crop.x) * iw;
    uniforms.src_rect[1] = static_cast<float>(crop.y) * ih;
    uniforms.src_rect[2] = static_cast<float>(crop.width) * iw;
    uniforms.src_rect[3] = static_cast<float>(crop.height) * ih;
    uniforms.sample_clamp[0] = (static_cast<float>(crop.x) + 0.5f) * iw;
    uniforms.sample_clamp[1] = (static_cast<float>(crop.y) + 0.5f) * ih;
    uniforms.sample_clamp[2] = (static_cast<float>(crop.x + crop.width) - 0.5f) * iw;
    uniforms.sample_clamp[3] = (static_cast<float>(crop.y + crop.height) - 0.5f) * ih;
    uniforms.dst_rect[0] = destination.x;
    uniforms.dst_rect[1] = destination.y;
    uniforms.dst_rect[2] = destination.w;
    uniforms.dst_rect[3] = destination.h;
    uniforms.texel_mode[0] = iw;
    uniforms.texel_mode[1] = ih;
    uniforms.step_dim[2] = 1.0f;
    return uniforms;
}
SDL_GPUGraphicsPipeline* pipeline(SDL_GPUDevice* device, SDL_GPUShader* vertex,
                                  SDL_GPUShader* fragment, SDL_GPUTextureFormat format) noexcept {
    SDL_GPUColorTargetDescription color{};
    color.format = format;
    color.blend_state.enable_blend = false;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    return SDL_CreateGPUGraphicsPipeline(device, &info);
}
bool draw_pass(SDL_GPUCommandBuffer* commands, SDL_GPUGraphicsPipeline* pipeline,
                SDL_GPUSampler* sampler, SDL_GPUTexture* source, SDL_GPUTexture* destination,
                const PixelRect& viewport, const MovieUniforms& uniforms,
                SDL_GPULoadOp load, const SDL_FColor& clear, bool cycle) noexcept {
    SDL_GPUColorTargetInfo color{};
    color.texture = destination;
    color.load_op = load;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.clear_color = clear;
    color.cycle = cycle;
    auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
    if (!pass) return false;
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    const SDL_GPUViewport view{static_cast<float>(viewport.x), static_cast<float>(viewport.y),
        static_cast<float>(viewport.width), static_cast<float>(viewport.height), 0, 1};
    const SDL_Rect scissor{static_cast<int>(viewport.x), static_cast<int>(viewport.y),
        static_cast<int>(viewport.width), static_cast<int>(viewport.height)};
    SDL_SetGPUViewport(pass, &view);
    SDL_SetGPUScissor(pass, &scissor);
    const SDL_GPUTextureSamplerBinding binding{source, sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    return true;
}
} // namespace

MovieEffects::~MovieEffects() { shutdown(); }
Status MovieEffects::fail(Status status, const char* message) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    error_[i] = '\0';
    return status;
}
Status MovieEffects::native_failure() noexcept {
    const auto* message = SDL_GetError();
    return fail(Status::backend_failure, message && *message ? message : "Movie GPU operation failed");
}
Status MovieEffects::initialize(SDL_GPUDevice* device, SDL_GPUTextureFormat format) noexcept {
    if (!SDL_IsMainThread() || device_) return fail(Status::invalid_state, "Movie effects require empty main-thread state");
    if (!device || !numeric_format(format)) return fail(Status::invalid_argument, "Explicit supported movie target format required");
    if (!SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) ||
        !SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        return fail(Status::unavailable, "Movie target/intermediate GPU format unavailable");
    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.code = shaders::post_vert;
    vertex_info.code_size = sizeof(shaders::post_vert);
    vertex_info.entrypoint = "main";
    vertex_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    auto* vertex = SDL_CreateGPUShader(device, &vertex_info);
    if (!vertex) return native_failure();
    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.code = shaders::movie_cleanup_frag;
    fragment_info.code_size = sizeof(shaders::movie_cleanup_frag);
    fragment_info.entrypoint = "main";
    fragment_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1;
    fragment_info.num_uniform_buffers = 1;
    auto* cleanup = SDL_CreateGPUShader(device, &fragment_info);
    if (!cleanup) {
        const auto status = native_failure();
        SDL_ReleaseGPUShader(device, vertex);
        return status;
    }
    fragment_info.code = shaders::movie_blur_frag;
    fragment_info.code_size = sizeof(shaders::movie_blur_frag);
    auto* blur = SDL_CreateGPUShader(device, &fragment_info);
    if (!blur) {
        const auto status = native_failure();
        SDL_ReleaseGPUShader(device, cleanup);
        SDL_ReleaseGPUShader(device, vertex);
        return status;
    }
    auto* final_pipeline = pipeline(device, vertex, cleanup, format);
    auto* work_pipeline = final_pipeline ? pipeline(device, vertex, blur, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) : nullptr;
    if (!final_pipeline || !work_pipeline) {
        const auto status = native_failure();
        if (final_pipeline) SDL_ReleaseGPUGraphicsPipeline(device, final_pipeline);
        SDL_ReleaseGPUShader(device, blur);
        SDL_ReleaseGPUShader(device, cleanup);
        SDL_ReleaseGPUShader(device, vertex);
        return status;
    }
    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = sampler_info.address_mode_v = sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.max_anisotropy = 1;
    sampler_info.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    auto* sampler = SDL_CreateGPUSampler(device, &sampler_info);
    if (!sampler) {
        const auto status = native_failure();
        SDL_ReleaseGPUGraphicsPipeline(device, work_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(device, final_pipeline);
        SDL_ReleaseGPUShader(device, blur);
        SDL_ReleaseGPUShader(device, cleanup);
        SDL_ReleaseGPUShader(device, vertex);
        return status;
    }
    SDL_ReleaseGPUShader(device, blur);
    SDL_ReleaseGPUShader(device, cleanup);
    SDL_ReleaseGPUShader(device, vertex);
    device_ = device;
    target_format_ = format;
    cleanup_pipeline_ = final_pipeline;
    blur_pipeline_ = work_pipeline;
    sampler_ = sampler;
    error_[0] = '\0';
    return Status::ok;
}
void MovieEffects::release_blur() noexcept {
    if (device_) {
        if (blur_horizontal_) SDL_ReleaseGPUTexture(device_, blur_horizontal_);
        if (blur_vertical_) SDL_ReleaseGPUTexture(device_, blur_vertical_);
    }
    blur_horizontal_ = blur_vertical_ = nullptr;
    blur_width_ = blur_height_ = 0;
}
Status MovieEffects::begin_source(std::uint64_t serial) noexcept {
    if (!SDL_IsMainThread() || !device_) return fail(Status::invalid_state, "Movie effects are not initialized on this thread");
    if (!serial) return fail(Status::invalid_argument, "A real nonzero producer lifetime serial is required");
    if (!active_ || serial_ != serial) release_blur();
    active_ = true;
    serial_ = serial;
    error_[0] = '\0';
    return Status::ok;
}
Status MovieEffects::end_source(std::uint64_t serial) noexcept {
    if (!SDL_IsMainThread() || !device_ || !active_ || serial != serial_)
        return fail(Status::invalid_state, "Movie retirement must match the active producer serial");
    active_ = false;
    serial_ = 0;
    release_blur(); // SDL defers disposal while previously recorded GPU use remains.
    error_[0] = '\0';
    return Status::ok;
}
Status MovieEffects::ensure_blur(std::uint32_t width, std::uint32_t height) noexcept {
    if (blur_horizontal_ && blur_vertical_ && width == blur_width_ && height == blur_height_) return Status::ok;
    if (!width || !height || static_cast<std::uint64_t>(width) * height > max_pixels / 2)
        return fail(Status::invalid_argument, "Combined movie blur intermediates exceed64MiB");
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    auto* horizontal = SDL_CreateGPUTexture(device_, &info);
    if (!horizontal) return native_failure();
    auto* vertical = SDL_CreateGPUTexture(device_, &info);
    if (!vertical) {
        const auto status = native_failure();
        SDL_ReleaseGPUTexture(device_, horizontal);
        return status;
    }
    release_blur();
    blur_horizontal_ = horizontal;
    blur_vertical_ = vertical;
    blur_width_ = width;
    blur_height_ = height;
    return Status::ok;
}
Status MovieEffects::record(SDL_GPUCommandBuffer* commands, std::uint64_t serial,
                            const PostSource& source, const PostTarget& target,
                            MovieSourceReadiness readiness, const MovieEffectsConfig& config,
                            MovieEffectRecord& result) noexcept {
    result = {};
    if (!SDL_IsMainThread() || !device_ || !active_ || serial != serial_)
        return fail(Status::invalid_state, "Movie frame does not own the active producer lifetime");
    if (source.content != FrameContent::movie) return fail(Status::unavailable, "Only explicitly classified movie sources are accepted");
    if (readiness == MovieSourceReadiness::not_ready) return fail(Status::unavailable, "Movie source is not ready for GPU sampling");
    if (readiness != MovieSourceReadiness::completed && readiness != MovieSourceReadiness::earlier_in_same_commands)
        return fail(Status::invalid_argument, "Explicit real movie source readiness required");
    if (!commands || source.device != device_ || target.device != device_ || !source.texture || !target.texture ||
        source.texture == target.texture || !source.width || !source.height || !target.width || !target.height ||
        source.width > max_extent || source.height > max_extent || target.width > max_extent || target.height > max_extent ||
        !bounded_rect(source.crop, source.width, source.height) || !bounded_rect(target.viewport, target.width, target.height) ||
        !numeric_format(source.format) || target.format != target_format_ ||
        (config.cleanup != MovieCleanup::off && config.cleanup != MovieCleanup::gentle && config.cleanup != MovieCleanup::strong) ||
        !(config.pixel_aspect >= 0.0625f && config.pixel_aspect <= 16) || !std::isfinite(config.pixel_aspect) ||
        (config.blur_backdrop && (!(config.blur_radius_pixels >= 0.25f && config.blur_radius_pixels <= 16) || !std::isfinite(config.blur_radius_pixels) ||
            !(config.backdrop_brightness >= 0 && config.backdrop_brightness <= 1) || !std::isfinite(config.backdrop_brightness))) ||
        (source.encoding != PostColorEncoding::unknown && source.encoding != PostColorEncoding::perceptual_sdr_unorm &&
            source.encoding != PostColorEncoding::linear_sdr && source.encoding != PostColorEncoding::linear_hdr) ||
        (target.load_op != SDL_GPU_LOADOP_LOAD && target.load_op != SDL_GPU_LOADOP_CLEAR && target.load_op != SDL_GPU_LOADOP_DONT_CARE))
        return fail(Status::invalid_argument, "Invalid explicit movie source, crop, target or configuration");
    if (static_cast<std::uint64_t>(source.crop.width) * source.crop.height > max_pixels)
        return fail(Status::invalid_argument, "Movie crop exceeds64MiB pixel implementation bound");
    const float clear[4]{target.clear_color.r, target.clear_color.g, target.clear_color.b, target.clear_color.a};
    for (float value : clear) if (!std::isfinite(value)) return fail(Status::invalid_argument, "Nonfinite movie clear color");
    const bool cleanup_enabled = config.cleanup != MovieCleanup::off;
    if ((cleanup_enabled || config.blur_backdrop) && source.encoding != PostColorEncoding::perceptual_sdr_unorm)
        return fail(Status::unavailable, "Active movie effects require explicit perceptual SDR source encoding");
    if (cleanup_enabled && !source.opaque)
        return fail(Status::unavailable, "Reference RGB-only movie cleanup requires actual producer-confirmed opacity");
    if (!cleanup_enabled && !config.blur_backdrop &&
        ((source.encoding == PostColorEncoding::unknown && source.format != target.format) ||
         (source.encoding == PostColorEncoding::linear_hdr &&
          (source.format != SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT || target.format != source.format))))
        return fail(Status::unavailable, "Off movie presentation cannot infer an unknown/HDR storage conversion");
    if (config.blur_backdrop) {
        const auto status = ensure_blur(source.crop.width, source.crop.height);
        if (status != Status::ok) return status;
    }
    const auto& viewport = target.viewport;
    const double aspect = static_cast<double>(source.crop.width) * config.pixel_aspect / source.crop.height;
    const double target_aspect = static_cast<double>(viewport.width) / viewport.height;
    double center_w = viewport.width, center_h = viewport.height;
    if (aspect > target_aspect) center_h = center_w / aspect;
    else center_w = center_h * aspect;
    const SDL_FRect center{static_cast<float>(viewport.x + (viewport.width - center_w) * 0.5),
        static_cast<float>(viewport.y + (viewport.height - center_h) * 0.5),
        static_cast<float>(center_w), static_cast<float>(center_h)};
    result.source_serial = serial;
    result.center = center;
    result.output_encoding = source.encoding;
    const SDL_FRect full_destination{static_cast<float>(viewport.x), static_cast<float>(viewport.y),
        static_cast<float>(viewport.width), static_cast<float>(viewport.height)};
    if (config.blur_backdrop) {
        const PixelRect work_view{0, 0, source.crop.width, source.crop.height};
        const SDL_FRect work_destination{0, 0, static_cast<float>(work_view.width), static_cast<float>(work_view.height)};
        const SDL_FColor work_clear{0, 0, 0, 0};
        auto horizontal = parameters(source.width, source.height, source.crop, work_destination);
        horizontal.step_dim[0] = config.blur_radius_pixels / source.width;
        if (!draw_pass(commands, blur_pipeline_, sampler_, source.texture, blur_horizontal_, work_view,
                       horizontal, SDL_GPU_LOADOP_CLEAR, work_clear, true)) return native_failure();
        ++result.passes_recorded;
        auto vertical = parameters(work_view.width, work_view.height, work_view, work_destination);
        vertical.step_dim[1] = config.blur_radius_pixels / work_view.height;
        if (!draw_pass(commands, blur_pipeline_, sampler_, blur_horizontal_, blur_vertical_, work_view,
                       vertical, SDL_GPU_LOADOP_CLEAR, work_clear, true)) return native_failure();
        ++result.passes_recorded;
        auto backdrop = parameters(work_view.width, work_view.height, work_view, full_destination);
        if (aspect > target_aspect) {
            const float fraction = static_cast<float>(target_aspect / aspect);
            backdrop.src_rect[0] = (1.0f - fraction) * 0.5f;
            backdrop.src_rect[2] = fraction;
        } else {
            const float fraction = static_cast<float>(aspect / target_aspect);
            backdrop.src_rect[1] = (1.0f - fraction) * 0.5f;
            backdrop.src_rect[3] = fraction;
        }
        backdrop.step_dim[2] = config.backdrop_brightness;
        if (!draw_pass(commands, cleanup_pipeline_, sampler_, blur_vertical_, target.texture, viewport,
                       backdrop, target.load_op, target.clear_color, false)) return native_failure();
        ++result.passes_recorded;
        result.backdrop_recorded = true;
    }
    auto foreground = parameters(source.width, source.height, source.crop, center);
    foreground.step_dim[3] = config.blur_backdrop ? 0.0f : 1.0f;
    if (cleanup_enabled) {
        foreground.texel_mode[2] = config.cleanup == MovieCleanup::gentle ? 0.30f : 0.55f;
        foreground.texel_mode[3] = config.cleanup == MovieCleanup::gentle ? 0.06f : 0.10f;
    }
    if (!draw_pass(commands, cleanup_pipeline_, sampler_, source.texture, target.texture, viewport, foreground,
                   config.blur_backdrop ? SDL_GPU_LOADOP_LOAD : target.load_op, target.clear_color, false)) return native_failure();
    ++result.passes_recorded;
    result.cleanup_recorded = cleanup_enabled;
    result.output_recorded = true;
    error_[0] = '\0';
    return Status::ok; // Recorded only; caller's actual submission/fence determines readiness.
}
void MovieEffects::shutdown() noexcept {
    release_blur();
    if (device_) {
        if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        if (cleanup_pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, cleanup_pipeline_);
        if (blur_pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, blur_pipeline_);
    }
    sampler_ = nullptr;
    cleanup_pipeline_ = blur_pipeline_ = nullptr;
    device_ = nullptr;
    target_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    active_ = false;
    serial_ = 0;
}
} // namespace cedar
