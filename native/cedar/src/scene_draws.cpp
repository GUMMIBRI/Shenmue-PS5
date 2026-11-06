#include "cedar/scene_draws.hpp"
#include <cmath>
#include <limits>
namespace cedar {
namespace {
using E=SceneDrawError;
struct Span {std::uintptr_t a,b;};
bool span(const void*p,std::size_t n,std::size_t size,std::size_t align,Span& s) {
    s.a=reinterpret_cast<std::uintptr_t>(p);const auto max=std::numeric_limits<std::uintptr_t>::max();
    if((n && (!p || s.a%align)) || n>max/size || n*size>max-s.a)return false;
    s.b=s.a+n*size;return true;
}
bool overlap(Span a,Span b) {return a.a<a.b && b.a<b.b && a.a<b.b && b.a<a.b;}
bool equal(const GpuRendererConfig&a,const GpuRendererConfig&b) {
    return a.color_format==b.color_format && a.blend==b.blend && a.depth==b.depth && a.cull==b.cull &&
        a.front_face==b.front_face && a.lighting==b.lighting && a.output_transfer==b.output_transfer;
}
bool sameid(const ModelTextureId&a,const ModelTextureId&b) {for(unsigned i=0;i<8;++i)if(a.bytes[i]!=b.bytes[i])return false;return true;}
bool srgb(SDL_GPUTextureFormat f) {return f==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB || f==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;}
bool unorm(SDL_GPUTextureFormat f) {return f==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM || f==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;}
bool transfer(GpuColorTransfer t) {return t==GpuColorTransfer::linear_sdr || t==GpuColorTransfer::perceptual_sdr;}
std::size_t lookup(const SceneTextureBinding*b,std::size_t n,std::uint32_t index) {
    std::size_t lo=0,hi=n;while(lo<hi){const auto m=lo+(hi-lo)/2;if(b[m].mesh_texture_index<index)lo=m+1;else hi=m;}
    return lo<n && b[lo].mesh_texture_index==index?lo:n;
}
bool light_valid(const GpuLighting& l) {
    if(!transfer(l.texture_transfer)||!transfer(l.vertex_transfer))return false;
    double length=0;for(float v:l.direction_to_light){if(!std::isfinite(v))return false;length+=double(v)*v;}if(!(length>0))return false;
    for(float v:l.ambient)if(!(v>=0 && v<=1))return false;
    for(float v:l.diffuse)if(!(v>=0 && v<=1))return false;
    if(l.has_normal_rotation) {
        const auto* m=l.normal_rotation;for(unsigned i=0;i<9;++i)if(!std::isfinite(m[i]))return false;
        for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){double dot=0;for(unsigned k=0;k<3;++k)dot+=double(m[a*3+k])*m[b*3+k];if(std::abs(dot-(a==b?1.0:0.0))>0.0001)return false;}
        const double det=double(m[0])*(double(m[4])*m[8]-double(m[7])*m[5])-double(m[3])*(double(m[1])*m[8]-double(m[7])*m[2])+double(m[6])*(double(m[1])*m[5]-double(m[4])*m[2]);
        if(std::abs(det-1)>0.0001)return false;
    }return true;
}
}
SceneDrawResult assemble_mt5_scene_draws(const Mt5TextureTable& table,const Mt5DrawRecord* source,
    std::size_t count,const GpuMesh& mesh,const SceneTextureBinding* textures,std::size_t texture_count,
    const SceneDrawMaterial* materials,const float (&camera)[16],const GpuRendererConfig& pipeline,
    GpuDraw* output,std::size_t capacity,SceneDrawBinding* bindings,std::size_t binding_capacity) noexcept {
    SceneDrawResult result;
    auto fail=[&](E e){result.error=e;return result;};
    if(!table.parsed() || !count || count>4096 || !texture_count || texture_count>65536)return fail(E::invalid_argument);
    if(capacity<count || binding_capacity<count)return fail(E::insufficient_capacity);
    Span ranges[10];
    if(!span(source,count,sizeof(*source),alignof(Mt5DrawRecord),ranges[0]) ||
       !span(textures,texture_count,sizeof(*textures),alignof(SceneTextureBinding),ranges[1]) ||
       !span(materials,count,sizeof(*materials),alignof(SceneDrawMaterial),ranges[2]) ||
       !span(&camera,1,sizeof(camera),alignof(float),ranges[3]) ||
       !span(&table,1,sizeof(table),alignof(Mt5TextureTable),ranges[4]) ||
       !span(&mesh,1,sizeof(mesh),alignof(GpuMesh),ranges[5]) ||
       !span(&pipeline,1,sizeof(pipeline),alignof(GpuRendererConfig),ranges[6]) ||
       !span(output,capacity,sizeof(*output),alignof(GpuDraw),ranges[7]) ||
       !span(bindings,binding_capacity,sizeof(*bindings),alignof(SceneDrawBinding),ranges[8]))return fail(E::invalid_buffer);
    for(unsigned i=0;i<9;++i)for(unsigned j=i+1;j<9;++j)if(overlap(ranges[i],ranges[j]))return fail(E::invalid_buffer);
    if(mesh.phase()!=GpuResourcePhase::ready)return fail(E::unavailable);
    if(!mesh.device() || !mesh.buffer())return fail(E::invalid_argument);
    bool nonzero=false;for(float f:camera){if(!std::isfinite(f))return fail(E::invalid_argument);nonzero|=f!=0;}if(!nonzero)return fail(E::invalid_argument);
    if(pipeline.color_format==SDL_GPU_TEXTUREFORMAT_INVALID || !transfer(pipeline.output_transfer) ||
       (pipeline.blend!=GpuBlendMode::opaque && pipeline.blend!=GpuBlendMode::straight_alpha && pipeline.blend!=GpuBlendMode::premultiplied_alpha) ||
       (pipeline.depth!=GpuDepthMode::disabled && pipeline.depth!=GpuDepthMode::less_write && pipeline.depth!=GpuDepthMode::less_equal_write) ||
       (pipeline.cull!=SDL_GPU_CULLMODE_NONE && pipeline.cull!=SDL_GPU_CULLMODE_FRONT && pipeline.cull!=SDL_GPU_CULLMODE_BACK) ||
       (pipeline.front_face!=SDL_GPU_FRONTFACE_CLOCKWISE && pipeline.front_face!=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE) ||
       (pipeline.lighting!=GpuLightingMode::unlit && pipeline.lighting!=GpuLightingMode::ambient_directional))return fail(E::incompatible_material);
    const bool lit=pipeline.lighting==GpuLightingMode::ambient_directional;
    if(lit && (!mesh.has_normals() || !mesh.normal_buffer()))return fail(E::unavailable);
    if(lit && (!transfer(pipeline.output_transfer) || (!srgb(pipeline.color_format)&&!unorm(pipeline.color_format)) ||
       (srgb(pipeline.color_format)&&pipeline.output_transfer!=GpuColorTransfer::perceptual_sdr) ||
       pipeline.blend==GpuBlendMode::premultiplied_alpha ||
       (pipeline.blend==GpuBlendMode::straight_alpha && pipeline.output_transfer==GpuColorTransfer::perceptual_sdr && !srgb(pipeline.color_format))))return fail(E::incompatible_material);
    for(std::size_t j=0;j<texture_count;++j) {
        if((j && textures[j-1].mesh_texture_index>=textures[j].mesh_texture_index) || !textures[j].texture)return fail(E::invalid_argument);
        if(!span(textures[j].texture,1,sizeof(GpuTexture),alignof(GpuTexture),ranges[9]) || overlap(ranges[9],ranges[7]) || overlap(ranges[9],ranges[8]))return fail(E::invalid_buffer);
        const auto& t=*textures[j].texture;
        if(t.phase()!=GpuResourcePhase::ready)return fail(E::unavailable);
        if(t.device()!=mesh.device() || !t.texture() || !t.sampler())return fail(E::invalid_argument);
        ModelTextureReference ref;result.source=table.texture(textures[j].mesh_texture_index,ref);
        if(!result.source)return fail(E::source_failure);
        if(!sameid(ref.id,textures[j].id))return fail(E::texture_mismatch);
    }
    std::size_t end=0;
    for(std::size_t i=0;i<count;++i) {
        result.draw_index=i;const auto& d=source[i];
        if(d.first_vertex!=end || !d.vertex_count || d.first_vertex%3 || d.vertex_count%3 || d.triangle_count!=d.vertex_count/3 ||
           d.first_vertex>mesh.vertex_count() || d.vertex_count>mesh.vertex_count()-d.first_vertex)return fail(E::invalid_range);
        end+=d.vertex_count;
        if(!span(d.raw_attributes,d.raw_attribute_bytes,1,1,ranges[9]) || overlap(ranges[9],ranges[7]) || overlap(ranges[9],ranges[8]))return fail(E::invalid_buffer);
        const auto index=lookup(textures,texture_count,d.texture_index);if(index==texture_count)return fail(E::missing_texture);
        const auto& m=materials[i];
        if(!equal(m.pipeline,pipeline) || !transfer(m.light.texture_transfer) || !transfer(m.light.vertex_transfer) ||
           m.alpha==SceneAlpha::unspecified ||
           (pipeline.blend==GpuBlendMode::opaque && m.alpha!=SceneAlpha::opaque) ||
           (pipeline.blend==GpuBlendMode::straight_alpha && m.alpha!=SceneAlpha::straight) ||
           (pipeline.blend==GpuBlendMode::premultiplied_alpha && m.alpha!=SceneAlpha::premultiplied))return fail(E::incompatible_material);
        if(lit && (!light_valid(m.light) || (srgb(textures[index].texture->format())!=(m.light.texture_transfer==GpuColorTransfer::perceptual_sdr)) ||
           (!srgb(textures[index].texture->format())&&!unorm(textures[index].texture->format()))))return fail(E::incompatible_material);
    }
    if(end!=mesh.vertex_count())return fail(E::invalid_range);
    // Complete preflight succeeded; immutable table/resources stay stable.
    for(std::size_t i=0;i<count;++i) {
        const auto& d=source[i];const auto index=lookup(textures,texture_count,d.texture_index);
        GpuDraw draw;draw.mesh=&mesh;draw.texture=textures[index].texture;
        draw.first_vertex=static_cast<std::uint32_t>(d.first_vertex);draw.vertex_count=static_cast<std::uint32_t>(d.vertex_count);
        for(unsigned j=0;j<16;++j)draw.view_projection[j]=camera[j];draw.light=materials[i].light;
        // Origin follows table's proven embedded-before-NAME index ordering.
        SceneDrawBinding bound;bound.source=d;bound.id=textures[index].id;
        bound.origin=d.texture_index<table.embedded_count()?ModelTextureOrigin::embedded_texn:ModelTextureOrigin::name_reference;
        bound.texture_binding_index=index;bound.package_index=textures[index].package_index;bound.package_texture_index=textures[index].package_texture_index;
        output[i]=draw;bindings[i]=bound;
    }
    result.draw_index=SIZE_MAX;result.written=count;return result;
}
}
