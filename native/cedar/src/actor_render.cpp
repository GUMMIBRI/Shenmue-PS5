#include "cedar/actor_render.hpp"
#include <cmath>
#include <limits>
namespace cedar {
namespace {
using S=ActorRenderStatus;
bool same(const GameActorKey&a,const GameActorKey&b) {if(a.instance!=b.instance)return false;for(unsigned i=0;i<4;++i)if(a.code[i]!=b.code[i])return false;return true;}
bool separate(const void*p,std::size_t n,const void*q,std::size_t m) {
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(q),max=std::numeric_limits<std::uintptr_t>::max();
    return p && q && n<=max-a && m<=max-b && (a+n<=b || b+m<=a);
}
bool camera_valid(const float* camera) {bool nonzero=false;for(unsigned i=0;i<16;++i){if(!std::isfinite(camera[i]))return false;nonzero|=camera[i]!=0;}return nonzero;}
}
std::size_t ActorRenderPoses::find(const GameActorKey& key) const noexcept {for(std::size_t i=0;i<count_;++i)if(same(entries_[i].value.key,key))return i;return count_;}
S ActorRenderPoses::commit(const Entry& e) noexcept {
    const auto& v=e.value;
    // Revalidate ALL actual resources/ranges before any draw-state write.
    for(std::size_t i=0;i<v.draw_count;++i) {
        const auto& d=v.draws[i];
        if(!d.mesh || !d.texture)return S::invalid_argument;
        // A pose commit writes every draw. Protect all resource owners before
        // dereferencing them, including resources shared by multiple draws.
        if(!separate(v.draws,v.draw_count*sizeof(GpuDraw),d.mesh,sizeof(*d.mesh)) ||
           !separate(v.draws,v.draw_count*sizeof(GpuDraw),d.texture,sizeof(*d.texture)))return S::invalid_argument;
        if(d.mesh->phase()!=GpuResourcePhase::ready || d.texture->phase()!=GpuResourcePhase::ready)
            return S::unavailable;
        if(!d.mesh->device() || d.mesh->device()!=d.texture->device() ||
           !d.mesh->buffer() || !d.texture->texture() || !d.texture->sampler() ||
           !d.vertex_count || d.first_vertex%3 || d.vertex_count%3 ||
           d.first_vertex>d.mesh->vertex_count() || d.vertex_count>d.mesh->vertex_count()-d.first_vertex)
            return S::invalid_argument;
    }
    if(!camera_valid(v.camera) || !std::isfinite(v.reference_yaw) || !std::isfinite(v.base_yaw) ||
       (e.overridden && !std::isfinite(e.override_yaw)))return S::invalid_argument;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(v.position[i]) || !std::isfinite(v.reference_position[i]))return S::invalid_argument;
    // Reduce each finite angle before subtraction to avoid extreme-angle overflow.
    constexpr double tau=6.283185307179586476925286766559;
    const double angle=std::remainder(e.overridden?e.override_yaw:v.base_yaw,tau)-std::remainder(v.reference_yaw,tau);
    const double c=std::cos(angle),s=std::sin(angle);
    const double rotation[9]={c,0,-s,0,1,0,s,0,c};
    double delta[16]={c,0,-s,0,0,1,0,0,s,0,c,0,0,0,0,1};
    for(unsigned row=0;row<3;++row) {
        double transformed=0;for(unsigned col=0;col<3;++col)transformed+=rotation[col*3+row]*v.reference_position[col];
        delta[12+row]=double(v.position[row])-transformed;
    }
    float matrix[16];
    for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row) {
        double x=0;for(unsigned k=0;k<4;++k)x+=double(v.camera[k*4+row])*delta[col*4+k];
        if(!std::isfinite(x) || std::abs(x)>std::numeric_limits<float>::max())return S::overflow;
        matrix[col*4+row]=static_cast<float>(x);
    }
    // Every computation above succeeds before the first real draw-state write.
    for(std::size_t i=0;i<v.draw_count;++i) {
        auto& draw=v.draws[i];for(unsigned j=0;j<16;++j)draw.view_projection[j]=matrix[j];
        draw.light.has_normal_rotation=true;
        for(unsigned j=0;j<9;++j)draw.light.normal_rotation[j]=static_cast<float>(rotation[j]);
    }
    return S::ok;
}
S ActorRenderPoses::register_actor(const ActorRenderRegistration& input) noexcept {
    if(axes_!=ActorWorldAxes::y_up_yaw_z_toward_x || !input.key.instance || !input.draws ||
       !input.draw_count || input.draw_count>4096 || reinterpret_cast<std::uintptr_t>(input.draws)%alignof(GpuDraw))return S::invalid_argument;
    for(char c:input.key.code)if(!c)return S::invalid_argument;
    if(count_==64)return S::full;
    for(std::size_t i=0;i<count_;++i)if(entries_[i].value.key.instance==input.key.instance)return S::duplicate_actor;
    const auto bytes=input.draw_count*sizeof(GpuDraw);
    if(!separate(input.draws,bytes,this,sizeof(*this)) || !separate(input.draws,bytes,&input,sizeof(input)))return S::invalid_argument;
    for(std::size_t i=0;i<count_;++i)if(!separate(input.draws,bytes,entries_[i].value.draws,entries_[i].value.draw_count*sizeof(GpuDraw)))return S::invalid_argument;
    for(std::size_t i=0;i<input.draw_count;++i)if(input.draws[i].light.has_normal_rotation) {
        const auto* r=input.draws[i].light.normal_rotation;
        for(unsigned j=0;j<9;++j)if(r[j]!=(j%4==0?1.0f:0.0f))return S::invalid_argument;
    }
    Entry e;e.value=input;const auto result=commit(e);
    if(result==S::ok)entries_[count_++]=e;
    return result;
}
S ActorRenderPoses::unregister_actor(const GameActorKey& key) noexcept {
    const auto i=find(key);if(i==count_)return S::missing_actor;
    // Restore the latest actual base pose before releasing borrowed draw ownership.
    auto e=entries_[i];e.overridden=false;const auto result=commit(e);if(result!=S::ok)return result;
    entries_[i]=entries_[--count_];entries_[count_]={};return S::ok;
}
S ActorRenderPoses::update_base_pose(const GameActorKey& key,const float (&position)[3],double yaw) noexcept {
    const auto i=find(key);if(i==count_)return S::missing_actor;
    auto e=entries_[i];for(unsigned j=0;j<3;++j)e.value.position[j]=position[j];e.value.base_yaw=yaw;
    const auto result=commit(e);if(result==S::ok)entries_[i]=e;return result;
}
S ActorRenderPoses::update_camera(const GameActorKey& key,const float (&camera)[16]) noexcept {
    const auto i=find(key);if(i==count_)return S::missing_actor;
    auto e=entries_[i];for(unsigned j=0;j<16;++j)e.value.camera[j]=camera[j];
    const auto result=commit(e);if(result==S::ok)entries_[i]=e;return result;
}
GameServiceResult ActorRenderPoses::set_yaw(void* p,const GameActorKey& key,double yaw) noexcept {
    auto& self=*static_cast<ActorRenderPoses*>(p);const auto i=self.find(key);if(i==self.count_)return GameServiceResult::unavailable;
    auto e=self.entries_[i];e.override_yaw=yaw;e.overridden=true;
    const auto result=self.commit(e);
    if(result!=S::ok)return result==S::unavailable?GameServiceResult::unavailable:GameServiceResult::failed;
    self.entries_[i]=e;return GameServiceResult::applied;
}
GameServiceResult ActorRenderPoses::clear_yaw(void* p,const GameActorKey& key) noexcept {
    auto& self=*static_cast<ActorRenderPoses*>(p);const auto i=self.find(key);if(i==self.count_)return GameServiceResult::unavailable;
    auto e=self.entries_[i];e.overridden=false;const auto result=self.commit(e);
    if(result!=S::ok)return result==S::unavailable?GameServiceResult::unavailable:GameServiceResult::failed;
    self.entries_[i]=e;return GameServiceResult::applied;
}
ActorPoseServices ActorRenderPoses::pose_services() noexcept {return {this,set_yaw,clear_yaw};}
}
