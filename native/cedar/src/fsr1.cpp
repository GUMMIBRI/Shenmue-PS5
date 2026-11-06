// SPDX-License-Identifier: MPL-2.0
// Adapted behavior/ownership from PE1 platform/fsr1/cpe_fsr1 + present_fsr1;
// source policy MPL2 retained. AMD algorithm headers retain their MIT notices.
#include "cedar/fsr1.hpp"
#include "cedar_post_shaders.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <cmath>
#include <cstring>
#define A_CPU 1
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "ffx_a.h"
#include "ffx_fsr1.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace cedar {
namespace {
SDL_GPUShader* make_shader(SDL_GPUDevice* device, const unsigned char* code,
    std::size_t bytes, SDL_GPUShaderStage stage) noexcept {
    SDL_GPUShaderCreateInfo info{};
    info.code = code; info.code_size = bytes; info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV; info.stage = stage;
    if (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) { info.num_samplers = 1; info.num_uniform_buffers = 1; }
    return SDL_CreateGPUShader(device, &info);
}
SDL_GPUGraphicsPipeline* make_pipeline(SDL_GPUDevice* device, SDL_GPUShader* vertex,
                                      SDL_GPUShader* fragment) noexcept {
    SDL_GPUColorTargetDescription target{}; target.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex; info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target; info.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(device, &info);
}
SDL_GPUTexture* target(SDL_GPUDevice* device, Uint32 w, Uint32 h, SDL_GPUTextureFormat format) noexcept {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D; info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(device, &info);
}
}
Fsr1::~Fsr1() { shutdown(); }
Status Fsr1::fail(Status status, const char* message, bool latch) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    error_[i] = '\0'; failure_latched_ |= latch; return status;
}
Status Fsr1::native_failure() noexcept { return fail(Status::backend_failure, SDL_GetError(), true); }
Status Fsr1::initialize(SDL_GPUDevice* device) noexcept {
    if (!SDL_IsMainThread() || device_) return fail(Status::invalid_state, "FSR1 device ownership state invalid");
    if (!device) return fail(Status::invalid_argument, "FSR1 requires a real borrowed GPU device");
    device_ = device; return Status::ok;
}
Status Fsr1::configure(Fsr1Options options) noexcept {
    if (!(options.sharpness_stops >= 0 && options.sharpness_stops <= 2))
        return fail(Status::invalid_argument, "RCAS stops must be finite and within [0,2]");
    if (options.enabled != options_.enabled || options.rcas_enabled != options_.rcas_enabled ||
        options.sharpness_stops != options_.sharpness_stops) failure_latched_ = false;
    options_ = options; return Status::ok;
}
Status Fsr1::prepare(Uint32 iw, Uint32 ih, Uint32 ow, Uint32 oh) noexcept {
    if (!easu_ || !rcas_ || !sampler_) {
        auto* vertex = make_shader(device_, shaders::post_vert, sizeof(shaders::post_vert), SDL_GPU_SHADERSTAGE_VERTEX);
        auto* easu_shader = make_shader(device_, shaders::fsr1_easu_frag, sizeof(shaders::fsr1_easu_frag), SDL_GPU_SHADERSTAGE_FRAGMENT);
        auto* rcas_shader = make_shader(device_, shaders::fsr1_rcas_frag, sizeof(shaders::fsr1_rcas_frag), SDL_GPU_SHADERSTAGE_FRAGMENT);
        auto* next_easu = vertex && easu_shader ? make_pipeline(device_, vertex, easu_shader) : nullptr;
        auto* next_rcas = vertex && rcas_shader ? make_pipeline(device_, vertex, rcas_shader) : nullptr;
        SDL_GPUSamplerCreateInfo sampling{};
        sampling.min_filter = sampling.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampling.address_mode_u = sampling.address_mode_v = sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        auto* next_sampler = SDL_CreateGPUSampler(device_, &sampling);
        const bool good = next_easu && next_rcas && next_sampler;
        if (!good) native_failure();
        if (vertex) SDL_ReleaseGPUShader(device_, vertex);
        if (easu_shader) SDL_ReleaseGPUShader(device_, easu_shader);
        if (rcas_shader) SDL_ReleaseGPUShader(device_, rcas_shader);
        if (!good) {
            if (next_easu) SDL_ReleaseGPUGraphicsPipeline(device_, next_easu);
            if (next_rcas) SDL_ReleaseGPUGraphicsPipeline(device_, next_rcas);
            if (next_sampler) SDL_ReleaseGPUSampler(device_, next_sampler);
            return Status::backend_failure;
        }
        easu_ = next_easu; rcas_ = next_rcas; sampler_ = next_sampler;
    }
    if (compact_ && intermediate_ && output_ && iw == iw_ && ih == ih_ && ow == ow_ && oh == oh_) return Status::ok;
    auto* compact = target(device_, iw, ih, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    auto* intermediate = target(device_, ow, oh, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    auto* output = target(device_, ow, oh, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    if (!compact || !intermediate || !output) {
        native_failure();
        if (compact) SDL_ReleaseGPUTexture(device_, compact);
        if (intermediate) SDL_ReleaseGPUTexture(device_, intermediate);
        if (output) SDL_ReleaseGPUTexture(device_, output);
        return Status::backend_failure;
    }
    if (compact_) SDL_ReleaseGPUTexture(device_, compact_);
    if (intermediate_) SDL_ReleaseGPUTexture(device_, intermediate_);
    if (output_) SDL_ReleaseGPUTexture(device_, output_);
    compact_ = compact; intermediate_ = intermediate; output_ = output;
    iw_ = iw; ih_ = ih; ow_ = ow; oh_ = oh; return Status::ok;
}
bool Fsr1::pass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* input, SDL_GPUTexture* output,
    SDL_GPUGraphicsPipeline* pipeline, const void* uniforms, Uint32 bytes) noexcept {
    SDL_GPUColorTargetInfo target_info{};
    target_info.texture = output; target_info.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target_info.store_op = SDL_GPU_STOREOP_STORE; target_info.cycle = true;
    auto* render = SDL_BeginGPURenderPass(commands, &target_info, 1, nullptr);
    if (!render) return false;
    const SDL_GPUViewport viewport{0, 0, static_cast<float>(ow_), static_cast<float>(oh_), 0, 1};
    SDL_SetGPUViewport(render, &viewport);
    SDL_BindGPUGraphicsPipeline(render, pipeline);
    const SDL_GPUTextureSamplerBinding binding{input, sampler_};
    SDL_BindGPUFragmentSamplers(render, 0, &binding, 1);
    SDL_PushGPUFragmentUniformData(commands, 0, uniforms, bytes);
    SDL_DrawGPUPrimitives(render, 3, 1, 0, 0); SDL_EndGPURenderPass(render); return true;
}
Status Fsr1::record(SDL_GPUCommandBuffer* commands, const PostSource& source,
    Uint32 ow, Uint32 oh, Fsr1Record& result) noexcept {
    result = {};
    if (!options_.enabled) return Status::ok;
    if (!SDL_IsMainThread() || !device_) return fail(Status::invalid_state, "FSR1 not initialized on the owning main thread");
    if (failure_latched_) return Status::unavailable; // Preserve the actual captured native cause.
    if (!commands || source.device != device_ || !source.texture || !source.opaque ||
        source.content == FrameContent::native_ui || source.encoding != PostColorEncoding::perceptual_sdr_unorm ||
        (source.format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM && source.format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM))
        return fail(Status::unavailable, "FSR1 requires explicit opaque perceptual SDR UNORM game/movie input; UI untouched");
    const auto r = source.crop;
    if (!r.width || !r.height || r.x > source.width || r.width > source.width - r.x ||
        r.y > source.height || r.height > source.height - r.y || !ow || !oh ||
        source.width > 8192 || source.height > 8192 || ow > 8192 || oh > 8192)
        return fail(Status::invalid_argument, "Invalid FSR1 source crop/output extent");
    if (r.width > ow || r.height > oh) return fail(Status::unavailable, "FSR1 bypassed for downscaling; original presentation required");
    if (source.content == FrameContent::movie && std::uint64_t{ow} * r.height != std::uint64_t{oh} * r.width)
        return fail(Status::unavailable, "FSR1 movie output must preserve the source raster aspect; original movie pixel aspect remains caller-owned");
    if (std::uint64_t{r.width} * r.height * 4 + std::uint64_t{ow} * oh * 16 > 160u * 1024u * 1024u)
        return fail(Status::unavailable, "FSR1 targets exceed the explicit 160MiB budget");
    if (source.texture == compact_ || source.texture == intermediate_ || source.texture == output_)
        return fail(Status::invalid_argument, "FSR1 input cannot alias owned outputs");
    const auto prepared = prepare(r.width, r.height, ow, oh);
    if (prepared != Status::ok) return prepared;
    SDL_GPUBlitInfo crop{};
    crop.source.texture = source.texture; crop.source.x = r.x; crop.source.y = r.y;
    crop.source.w = r.width; crop.source.h = r.height;
    crop.destination.texture = compact_; crop.destination.w = r.width; crop.destination.h = r.height;
    crop.load_op = SDL_GPU_LOADOP_DONT_CARE; crop.filter = SDL_GPU_FILTER_NEAREST; crop.cycle = true;
    SDL_BlitGPUTexture(commands, &crop); // Actual crop first; gathers cannot bleed into an atlas.
    AU1 c0[4], c1[4], c2[4], c3[4], rcas[4]; Uint32 constants[16];
    FsrEasuCon(c0,c1,c2,c3,float(r.width),float(r.height),float(r.width),float(r.height),float(ow),float(oh));
    std::memcpy(constants,c0,16); std::memcpy(constants+4,c1,16);
    std::memcpy(constants+8,c2,16); std::memcpy(constants+12,c3,16);
    if (!pass(commands, compact_, intermediate_, easu_, constants, sizeof(constants))) return native_failure();
    result.passes_recorded = 1;
    SDL_GPUTexture* final = intermediate_;
    if (options_.rcas_enabled) {
        FsrRcasCon(rcas, options_.sharpness_stops);
        if (!pass(commands, intermediate_, output_, rcas_, rcas, sizeof(rcas))) return native_failure();
        result.passes_recorded = 2; final = output_;
    }
    result.output = {device_, final, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, ow, oh,
                     {0,0,ow,oh}, source.content, source.encoding, true};
    result.applied = true; error_[0] = '\0'; return Status::ok;
}
void Fsr1::shutdown() noexcept {
    if (device_) {
        if (compact_) SDL_ReleaseGPUTexture(device_, compact_);
        if (intermediate_) SDL_ReleaseGPUTexture(device_, intermediate_);
        if (output_) SDL_ReleaseGPUTexture(device_, output_);
        if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        if (easu_) SDL_ReleaseGPUGraphicsPipeline(device_, easu_);
        if (rcas_) SDL_ReleaseGPUGraphicsPipeline(device_, rcas_);
    }
    device_ = nullptr; compact_ = intermediate_ = output_ = nullptr;
    sampler_ = nullptr; easu_ = rcas_ = nullptr; iw_ = ih_ = ow_ = oh_ = 0; failure_latched_ = false;
}
} // namespace cedar
