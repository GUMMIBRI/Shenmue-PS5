// SPDX-License-Identifier: MPL-2.0
#include "cedar/display_effects.hpp"
#include "cedar_post_shaders.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

namespace cedar {
namespace {
bool perceptual_storage(SDL_GPUTextureFormat format) noexcept {
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
}
bool unit(float value) noexcept { return value >= 0.0f && value <= 1.0f; }
bool rectangle(const PixelRect& rect, std::uint32_t width, std::uint32_t height) noexcept {
    return width && height && width <= 8192 && height <= 8192 && rect.width && rect.height &&
        rect.x <= width && rect.y <= height && rect.width <= width - rect.x &&
        rect.height <= height - rect.y;
}
struct Uniforms {
    float source[4], destination[4], effect[4], mask[4];
};
static_assert(sizeof(Uniforms) == 64, "Post fragment std140 four-vec4 contract changed");
} // namespace

DisplayEffects::~DisplayEffects() { shutdown(); }
Status DisplayEffects::fail(Status status, const char* message) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    error_[i] = '\0';
    return status;
}
Status DisplayEffects::native_failure() noexcept {
    const char* message = SDL_GetError();
    return fail(Status::backend_failure, message && *message ? message : "SDL GPU post resource/pass failed");
}
Status DisplayEffects::initialize(SDL_GPUDevice* device, SDL_GPUTextureFormat format) noexcept {
    if (!SDL_IsMainThread() || device_) return fail(Status::invalid_state, "Post facility requires uninitialized main-thread ownership");
    if (!device) return fail(Status::invalid_argument, "Explicit borrowed GPU device required");
    if (!perceptual_storage(format)) return fail(Status::unavailable, "Display effects require non-sRGB RGBA8/BGRA8 or RGBA16_FLOAT SDR target");
    device_ = device;
    format_ = format;
    error_[0] = '\0';
    return Status::ok;
}
Status DisplayEffects::prepare(SDL_GPUFilter filter) noexcept {
    if (!pipeline_) {
        SDL_GPUShaderCreateInfo info{};
        info.code = shaders::post_vert;
        info.code_size = sizeof(shaders::post_vert);
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        auto* vertex = SDL_CreateGPUShader(device_, &info);
        if (!vertex) return native_failure();
        info.code = shaders::display_filter_frag;
        info.code_size = sizeof(shaders::display_filter_frag);
        info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = 1;
        info.num_uniform_buffers = 1;
        auto* fragment = SDL_CreateGPUShader(device_, &info);
        if (!fragment) {
            const auto error = native_failure();
            SDL_ReleaseGPUShader(device_, vertex);
            return error;
        }
        SDL_GPUColorTargetDescription color{};
        color.format = format_;
        SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.vertex_shader = vertex;
        pipeline_info.fragment_shader = fragment;
        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipeline_info.target_info.color_target_descriptions = &color;
        pipeline_info.target_info.num_color_targets = 1;
        auto* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
        if (!pipeline) {
            const auto error = native_failure();
            SDL_ReleaseGPUShader(device_, fragment);
            SDL_ReleaseGPUShader(device_, vertex);
            return error;
        }
        SDL_ReleaseGPUShader(device_, fragment);
        SDL_ReleaseGPUShader(device_, vertex);
        pipeline_ = pipeline;
    }
    const unsigned index = filter == SDL_GPU_FILTER_LINEAR ? 1u : 0u;
    if (!samplers_[index]) {
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = filter;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.min_lod = info.max_lod = 0.0f;
        info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplers_[index] = SDL_CreateGPUSampler(device_, &info);
        if (!samplers_[index]) return native_failure();
    }
    return Status::ok;
}
Status DisplayEffects::record(SDL_GPUCommandBuffer* commands, const PostSource& source,
    const PostTarget& target, const DisplayEffectSettings& settings, bool& applied) noexcept {
    applied = false;
    if (settings.mode != DisplayEffectMode::off && settings.mode != DisplayEffectMode::scanlines &&
        settings.mode != DisplayEffectMode::crt)
        return fail(Status::invalid_argument, "Unsupported display effect mode");
    if (settings.mode == DisplayEffectMode::off) { error_[0] = '\0'; return Status::ok; }
    if (!SDL_IsMainThread() || !device_)
        return fail(Status::invalid_state, "Active display effects require initialized main-thread GPU ownership");
    if (!commands || !source.texture || !target.texture || source.texture == target.texture ||
        source.device != device_ || target.device != device_ || target.format != format_ ||
        !rectangle(source.crop, source.width, source.height) ||
        !rectangle(target.viewport, target.width, target.height))
        return fail(Status::invalid_argument, "Explicit same-device distinct source/target crop and viewport required (max8192)");
    if (source.content != FrameContent::game && source.content != FrameContent::movie)
        return fail(Status::unavailable, "Caller-native UI is not a display-effect source");
    if (source.encoding != PostColorEncoding::perceptual_sdr_unorm || !perceptual_storage(source.format))
        return fail(Status::unavailable, "Actual producer perceptual SDR non-sRGB encoding required; no gamma guessing");
    if (!settings.logical_rows || settings.logical_rows > 8192 ||
        !(settings.scanline_strength >= 0 && settings.scanline_strength <= 0.25f) ||
        !(settings.mask_strength >= 0 && settings.mask_strength <= 0.15f) ||
        !unit(settings.mask_color[0]) || !unit(settings.mask_color[1]) || !unit(settings.mask_color[2]) ||
        (settings.filter != SDL_GPU_FILTER_NEAREST && settings.filter != SDL_GPU_FILTER_LINEAR) ||
        (target.load_op != SDL_GPU_LOADOP_LOAD && target.load_op != SDL_GPU_LOADOP_CLEAR))
        return fail(Status::invalid_argument, "Explicit logical rows and bounded effect/sampling/load settings required");
    if (target.load_op == SDL_GPU_LOADOP_CLEAR &&
        (!unit(target.clear_color.r) || !unit(target.clear_color.g) ||
         !unit(target.clear_color.b) || !unit(target.clear_color.a)))
        return fail(Status::invalid_argument, "Post clear color must be finite SDR [0,1]");
    const auto prepared = prepare(settings.filter);
    if (prepared != Status::ok) return prepared; // No caller render pass was begun.
    SDL_GPUColorTargetInfo color{};
    color.texture = target.texture;
    color.load_op = target.load_op;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.clear_color = target.clear_color;
    auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
    if (!pass) return native_failure();
    const auto& destination = target.viewport;
    const auto& crop = source.crop;
    const SDL_GPUViewport viewport{static_cast<float>(destination.x), static_cast<float>(destination.y),
        static_cast<float>(destination.width), static_cast<float>(destination.height), 0, 1};
    const SDL_Rect scissor{static_cast<int>(destination.x), static_cast<int>(destination.y),
        static_cast<int>(destination.width), static_cast<int>(destination.height)};
    const SDL_GPUTextureSamplerBinding binding{source.texture,
        samplers_[settings.filter == SDL_GPU_FILTER_LINEAR ? 1 : 0]};
    const Uniforms uniforms{{static_cast<float>(crop.x), static_cast<float>(crop.y),
                            static_cast<float>(crop.width), static_cast<float>(crop.height)},
        {viewport.x, viewport.y, viewport.w, viewport.h},
        {static_cast<float>(settings.logical_rows), static_cast<float>(settings.mode),
         settings.scanline_strength, settings.mask_strength},
        {settings.mask_color[0], settings.mask_color[1], settings.mask_color[2], 0}};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_SetGPUScissor(pass, &scissor);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    applied = true;
    error_[0] = '\0';
    return Status::ok;
}
void DisplayEffects::shutdown() noexcept {
    if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    for (auto& sampler : samplers_) {
        if (sampler) SDL_ReleaseGPUSampler(device_, sampler);
        sampler = nullptr;
    }
    pipeline_ = nullptr;
    device_ = nullptr;
    format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
}
} // namespace cedar
