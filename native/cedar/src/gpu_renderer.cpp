#include "cedar/gpu_renderer.hpp"
#include "cedar_mesh_shaders.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <cmath>
#include <cstddef>

namespace cedar {
namespace {
bool known_transfer(GpuColorTransfer t) { return t==GpuColorTransfer::linear_sdr || t==GpuColorTransfer::perceptual_sdr; }
bool srgb_format(SDL_GPUTextureFormat f) { return f==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB || f==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB; }
bool sdr_format(SDL_GPUTextureFormat f) { return srgb_format(f) || f==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM || f==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM; }
struct LightUniform { float direction[4], ambient[4], diffuse[4], flags[4]; };
static_assert(sizeof(LightUniform)==64);
}

GpuRenderer::~GpuRenderer() { shutdown(); }
Status GpuRenderer::fail(Status status, const char* message) noexcept {
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(error_) && message[i]; ++i) error_[i] = message[i];
    error_[i] = '\0';
    return status;
}
Status GpuRenderer::native_failure() noexcept { return fail(Status::backend_failure, SDL_GetError()); }
Status GpuRenderer::initialize(SDL_GPUDevice* device, const GpuRendererConfig& config) noexcept {
    if (!SDL_IsMainThread() || device_ || pipeline_) return fail(Status::invalid_state, "Invalid GPU initialization state");
    if (!device || config.color_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
        (config.blend != GpuBlendMode::opaque && config.blend != GpuBlendMode::straight_alpha && config.blend != GpuBlendMode::premultiplied_alpha) ||
        (config.depth != GpuDepthMode::disabled && config.depth != GpuDepthMode::less_write && config.depth != GpuDepthMode::less_equal_write) ||
        (config.cull != SDL_GPU_CULLMODE_NONE && config.cull != SDL_GPU_CULLMODE_FRONT && config.cull != SDL_GPU_CULLMODE_BACK) ||
        (config.front_face != SDL_GPU_FRONTFACE_CLOCKWISE && config.front_face != SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE))
        return fail(Status::invalid_argument, "Explicit supported GPU pipeline configuration required");
    if (config.lighting != GpuLightingMode::unlit && config.lighting != GpuLightingMode::ambient_directional)
        return fail(Status::invalid_argument, "Unknown lighting mode");
    const bool lit = config.lighting == GpuLightingMode::ambient_directional;
    if (lit && (!known_transfer(config.output_transfer) || !sdr_format(config.color_format) ||
        (srgb_format(config.color_format) && config.output_transfer != GpuColorTransfer::perceptual_sdr) ||
        config.blend == GpuBlendMode::premultiplied_alpha))
        return fail(Status::invalid_argument, "Lit mode requires explicit SDR target transfer and opaque/straight alpha");
    if (lit && config.blend == GpuBlendMode::straight_alpha &&
        config.output_transfer == GpuColorTransfer::perceptual_sdr && !srgb_format(config.color_format))
        return fail(Status::invalid_argument, "Perceptual straight-alpha lighting requires native SRGB target blending");
    if (config.depth != GpuDepthMode::disabled && !SDL_GPUTextureSupportsFormat(device,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        return fail(Status::unavailable, "D32 depth target unavailable");
    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.code = lit ? shaders::lit_mesh_vert : shaders::mesh_vert;
    vertex_info.code_size = lit ? sizeof(shaders::lit_mesh_vert) : sizeof(shaders::mesh_vert);
    vertex_info.entrypoint = "main";
    vertex_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.num_uniform_buffers = lit ? 2 : 1;
    auto* vertex = SDL_CreateGPUShader(device, &vertex_info);
    if (!vertex) return native_failure();
    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.code = lit ? shaders::lit_mesh_frag : shaders::mesh_frag;
    fragment_info.code_size = lit ? sizeof(shaders::lit_mesh_frag) : sizeof(shaders::mesh_frag);
    fragment_info.entrypoint = "main";
    fragment_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1;
    fragment_info.num_uniform_buffers = lit ? 1 : 0;
    auto* fragment = SDL_CreateGPUShader(device, &fragment_info);
    if (!fragment) {
        const auto failure = native_failure();
        SDL_ReleaseGPUShader(device, vertex);
        return failure;
    }
    SDL_GPUVertexBufferDescription buffers[2]{};
    buffers[0].pitch = sizeof(GpuVertex);
    buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffers[1].slot = 1; buffers[1].pitch = 12;
    buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[4]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, x))};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(GpuVertex, u))};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<Uint32>(offsetof(GpuVertex, rgba))};
    attributes[3] = {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0};
    SDL_GPUColorTargetDescription color{};
    color.format = config.color_format;
    color.blend_state.enable_blend = config.blend != GpuBlendMode::opaque;
    color.blend_state.src_color_blendfactor = config.blend == GpuBlendMode::straight_alpha ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.vertex_input_state = {buffers, lit ? 2u : 1u, attributes, lit ? 4u : 3u};
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = config.cull;
    info.rasterizer_state.front_face = config.front_face;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info = {&color, 1, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, config.depth != GpuDepthMode::disabled, 0, 0, 0};
    info.depth_stencil_state.enable_depth_test = config.depth != GpuDepthMode::disabled;
    info.depth_stencil_state.enable_depth_write = config.depth != GpuDepthMode::disabled;
    info.depth_stencil_state.compare_op = config.depth == GpuDepthMode::less_equal_write ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL : SDL_GPU_COMPAREOP_LESS;
    auto* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!pipeline) {
        const auto failure = native_failure();
        SDL_ReleaseGPUShader(device, fragment);
        SDL_ReleaseGPUShader(device, vertex);
        return failure;
    }
    SDL_ReleaseGPUShader(device, fragment);
    SDL_ReleaseGPUShader(device, vertex);
    device_ = device;
    pipeline_ = pipeline;
    config_ = config;
    error_[0] = '\0';
    return Status::ok;
}
Status GpuRenderer::ensure_depth(std::uint32_t width, std::uint32_t height) noexcept {
    if (config_.depth == GpuDepthMode::disabled) return Status::ok;
    if (depth_ && width == depth_width_ && height == depth_height_) return Status::ok;
    if (!width || !height || width > 16384 || height > 16384 ||
        static_cast<std::uint64_t>(width) * height > 16u * 1024u * 1024u)
        return fail(Status::invalid_argument, "Depth target exceeds 64MiB implementation limit");
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = width; info.height = height;
    info.layer_count_or_depth = 1; info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    auto* next = SDL_CreateGPUTexture(device_, &info);
    if (!next) return native_failure();
    if (depth_) SDL_ReleaseGPUTexture(device_, depth_);
    depth_ = next; depth_width_ = width; depth_height_ = height;
    return Status::ok;
}
Status GpuRenderer::record(const GpuFrame& frame, const GpuDraw* draws,
                           std::size_t count, const GpuPassClear& clear) noexcept {
    if (!SDL_IsMainThread() || !pipeline_) return fail(Status::invalid_state, "GPU renderer is not initialized on this thread");
    if (!draws || !count || count > 65536 || frame.device != device_ || !frame.commands ||
        !frame.swapchain || !frame.width || !frame.height || frame.color_format != config_.color_format)
        return fail(Status::invalid_argument, "Explicit active frame and draw list required");
    for (float channel : clear.rgba) if (!std::isfinite(channel)) return fail(Status::invalid_argument, "Nonfinite clear color");
    if (!(clear.depth >= 0 && clear.depth <= 1)) return fail(Status::invalid_argument, "Invalid clear depth");
    for (std::size_t i = 0; i < count; ++i) {
        const auto& draw = draws[i];
        if (!draw.mesh || !draw.texture || draw.mesh->phase() != GpuResourcePhase::ready || draw.texture->phase() != GpuResourcePhase::ready)
            return fail(Status::unavailable, "All actual mesh/texture uploads must complete before rendering");
        if (draw.mesh->device() != device_ || draw.texture->device() != device_ ||
            !draw.vertex_count || draw.first_vertex % 3 || draw.vertex_count % 3 ||
            draw.first_vertex > draw.mesh->vertex_count() || draw.vertex_count > draw.mesh->vertex_count() - draw.first_vertex)
            return fail(Status::invalid_argument, "Invalid explicit GPU draw range/device");
        if (config_.lighting == GpuLightingMode::ambient_directional) {
            const auto& l=draw.light;
            if(l.has_normal_rotation) {
                for(float v:l.normal_rotation) if(!std::isfinite(v)) return fail(Status::invalid_argument,"Nonfinite normal rotation");
                for(unsigned a=0;a<3;++a) for(unsigned b=0;b<3;++b) {
                    double dot=0;for(unsigned k=0;k<3;++k) dot+=double(l.normal_rotation[a*3+k])*l.normal_rotation[b*3+k];
                    if(std::abs(dot-(a==b?1.0:0.0))>0.0001) return fail(Status::invalid_argument,"Normal transform must be orthonormal");
                }
                const auto* m=l.normal_rotation;
                const double det=double(m[0])*(double(m[4])*m[8]-double(m[7])*m[5])-double(m[3])*(double(m[1])*m[8]-double(m[7])*m[2])+double(m[6])*(double(m[1])*m[5]-double(m[4])*m[2]);
                if(std::abs(det-1)>0.0001) return fail(Status::invalid_argument,"Normal transform must be proper rotation");
            }
            if (!draw.mesh->has_normals() || !draw.mesh->normal_buffer())
                return fail(Status::unavailable, "Lit draw requires an uploaded world-normal stream");
            if (!known_transfer(l.texture_transfer) || !known_transfer(l.vertex_transfer) ||
                !sdr_format(draw.texture->format()) ||
                (srgb_format(draw.texture->format()) != (l.texture_transfer == GpuColorTransfer::perceptual_sdr)))
                return fail(Status::invalid_argument, "Lit draw requires matching explicit SDR texture/vertex transfer");
            double magnitude=0;
            for (float v:l.direction_to_light) { if(!std::isfinite(v)) return fail(Status::invalid_argument,"Nonfinite light direction"); magnitude+=double(v)*v; }
            if (!(magnitude>0)) return fail(Status::invalid_argument,"Nonzero light direction required");
            for (float v:l.ambient) if(!(v>=0 && v<=1)) return fail(Status::invalid_argument,"Ambient RGB outside [0,1]");
            for (float v:l.diffuse) if(!(v>=0 && v<=1)) return fail(Status::invalid_argument,"Diffuse RGB outside [0,1]");
        }
        bool nonzero_matrix = false;
        for (float value : draw.view_projection) {
            if (!std::isfinite(value)) return fail(Status::invalid_argument, "Nonfinite camera matrix");
            nonzero_matrix |= value != 0;
        }
        if (!nonzero_matrix) return fail(Status::invalid_argument, "No camera matrix supplied");
    }
    const auto depth_status = ensure_depth(frame.width, frame.height);
    if (depth_status != Status::ok) return depth_status;
    SDL_GPUColorTargetInfo color{};
    color.texture = frame.swapchain;
    color.clear_color = {clear.rgba[0], clear.rgba[1], clear.rgba[2], clear.rgba[3]};
    color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_; depth.clear_depth = clear.depth;
    depth.load_op = SDL_GPU_LOADOP_CLEAR; depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE; depth.cycle = true;
    auto* pass = SDL_BeginGPURenderPass(frame.commands, &color, 1,
        config_.depth == GpuDepthMode::disabled ? nullptr : &depth);
    if (!pass) return native_failure(); // Caller retains frame; no implicit present/cancel.
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    const SDL_GPUViewport viewport{0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height), 0, 1};
    SDL_SetGPUViewport(pass, &viewport);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& draw = draws[i];
        const SDL_GPUBufferBinding vertex{draw.mesh->buffer(), 0};
        const SDL_GPUTextureSamplerBinding texture{draw.texture->texture(), draw.texture->sampler()};
        SDL_BindGPUVertexBuffers(pass, 0, &vertex, 1);
        if (config_.lighting == GpuLightingMode::ambient_directional) {
            const SDL_GPUBufferBinding normal{draw.mesh->normal_buffer(),0};
            SDL_BindGPUVertexBuffers(pass,1,&normal,1);
            const auto& l=draw.light;
            double sum=0; for(float v:l.direction_to_light) sum+=double(v)*v;
            const double length=std::sqrt(sum);
            LightUniform u{};
            for(unsigned c=0;c<3;++c) { u.direction[c]=static_cast<float>(double(l.direction_to_light[c])/length); u.ambient[c]=l.ambient[c]; u.diffuse[c]=l.diffuse[c]; }
            float vertex_transfer[20]={l.vertex_transfer==GpuColorTransfer::perceptual_sdr ? 1.0f:0.0f,0,0,0};
            vertex_transfer[4]=vertex_transfer[9]=vertex_transfer[14]=vertex_transfer[19]=1;
            if(l.has_normal_rotation) for(unsigned col=0;col<3;++col) for(unsigned row=0;row<3;++row)
                vertex_transfer[4+col*4+row]=l.normal_rotation[col*3+row];
            SDL_PushGPUVertexUniformData(frame.commands,1,vertex_transfer,sizeof(vertex_transfer));
            u.flags[2]=config_.output_transfer==GpuColorTransfer::perceptual_sdr && !srgb_format(config_.color_format) ? 1.0f:0.0f;
            SDL_PushGPUFragmentUniformData(frame.commands,0,&u,sizeof(u));
        }
        SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);
        SDL_PushGPUVertexUniformData(frame.commands, 0, draw.view_projection, sizeof(draw.view_projection));
        SDL_DrawGPUPrimitives(pass, draw.vertex_count, 1, draw.first_vertex, 0);
    }
    SDL_EndGPURenderPass(pass);
    error_[0] = '\0';
    return Status::ok; // Command recording only, not execution or gameplay proof.
}
Status GpuRenderer::record_to_texture(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* color,
    std::uint32_t width, std::uint32_t height, const GpuDraw* draws,
    std::size_t count, const GpuPassClear& clear) noexcept {
    const GpuFrame target{device_, commands, color, config_.color_format, width, height};
    return record(target, draws, count, clear);
}
void GpuRenderer::shutdown() noexcept {
    if (device_) {
        if (depth_) SDL_ReleaseGPUTexture(device_, depth_);
        if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    }
    depth_ = nullptr; pipeline_ = nullptr; device_ = nullptr;
    depth_width_ = depth_height_ = 0;
}
} // namespace cedar
