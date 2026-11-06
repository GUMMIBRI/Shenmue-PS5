// SPDX-License-Identifier: GPL-3.0-only
// Source-informed dependencies retain new-yokosuka GPL in third_party/licenses.
#include "cedar/scene_actors.hpp"
#include <cmath>
#include <limits>
namespace cedar {namespace {
using E=SceneActorError;
bool separate(const void*p,std::size_t n,const void*q,std::size_t m) {
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(q),max=std::numeric_limits<std::uintptr_t>::max();
    return (!n||p)&&(!m||q)&&n<=max-a&&m<=max-b&&(!n||!m||a+n<=b||b+m<=a);
}
bool string_valid(SceneResourceString s) {return s.size && s.size<=SceneResourceView::max_string && s.bytes && separate(s.bytes,s.size,nullptr,0);}
GameServiceResult service(ActorWorldStatus s) {return s==ActorWorldStatus::ok?GameServiceResult::applied:
    (s==ActorWorldStatus::unavailable||s==ActorWorldStatus::missing_actor)?GameServiceResult::unavailable:GameServiceResult::failed;}
}
SceneActorResult SceneActors::add(const SceneResourceView& view,std::size_t index,const GameActorKey& key,
    const float (&camera)[16],const SceneActorServices& services) noexcept {
    SceneActorResult r;r.remaining=count_;
    auto fail=[&](E error){r.error=error;return r;};
    if(busy_)return fail(E::reentrant);
    if(count_==64)return fail(E::full);
    const auto* source=view.record(index);if(!source)return fail(E::missing_record);
    if(!key.instance || !services.convert || !services.resolve)return fail(E::invalid_argument);
    for(char c:key.code)if(!c)return fail(E::invalid_argument);
    for(std::size_t i=0;i<count_;++i)if(entries_[i].key.instance==key.instance)return fail(E::duplicate_actor);
    if(!separate(&view,sizeof(view),this,sizeof(*this)) || !separate(source,sizeof(*source),this,sizeof(*this)) ||
       !separate(&key,sizeof(key),this,sizeof(*this)) || !separate(&camera,sizeof(camera),this,sizeof(*this)) ||
       !separate(&services,sizeof(services),this,sizeof(*this)) || !string_valid(source->model) ||
       !separate(source->model.bytes,source->model.size,this,sizeof(*this)) ||
       (source->has_image && (!string_valid(source->image)||!separate(source->image.bytes,source->image.size,this,sizeof(*this)))))return fail(E::invalid_buffer);
    if((services.converter_context && !separate(services.converter_context,1,this,sizeof(*this))) ||
       (services.resolver_context && !separate(services.resolver_context,1,this,sizeof(*this))))return fail(E::invalid_buffer);
    const auto placement=*source;const auto actor=key;float actual_camera[16];bool nonzero=false;
    for(unsigned i=0;i<16;++i){if(!std::isfinite(camera[i]))return fail(E::invalid_argument);actual_camera[i]=camera[i];nonzero|=camera[i]!=0;}
    if(!nonzero)return fail(E::invalid_argument);
    busy_=true;SceneActorPose pose;
    if(!services.convert(services.converter_context,placement,pose)){busy_=false;return fail(E::conversion_unavailable);}
    if(!std::isfinite(pose.yaw)||!std::isfinite(pose.reference_yaw)){busy_=false;return fail(E::invalid_argument);}
    for(unsigned i=0;i<3;++i)if(!std::isfinite(pose.position[i])||!std::isfinite(pose.reference_position[i])){busy_=false;return fail(E::invalid_argument);}
    SceneActorModel model;
    if(!services.resolve(services.resolver_context,placement,model)){busy_=false;return fail(E::model_unavailable);}
    if(!string_valid(model.resolved_model) || model.resolved_model.size!=placement.model.size){busy_=false;return fail(E::model_mismatch);}
    for(std::size_t i=0;i<placement.model.size;++i)if(model.resolved_model.bytes[i]!=placement.model.bytes[i]){busy_=false;return fail(E::model_mismatch);}
    if(!model.draws || !model.draw_count || model.draw_count>4096 ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),this,sizeof(*this)) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),&view,sizeof(view)) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),&key,sizeof(key)) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),&camera,sizeof(camera)) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),&services,sizeof(services)) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),source,sizeof(*source)) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),placement.model.bytes,placement.model.size) ||
       !separate(model.draws,model.draw_count*sizeof(GpuDraw),model.resolved_model.bytes,model.resolved_model.size) ||
       (placement.has_image && !separate(model.draws,model.draw_count*sizeof(GpuDraw),placement.image.bytes,placement.image.size))) {
        busy_=false;return fail(E::invalid_buffer);
    }
    for(std::size_t i=0;i<view.record_count();++i) {
        const auto* record=view.record(i);const auto bytes=model.draw_count*sizeof(GpuDraw);
        if(!record || !separate(model.draws,bytes,record,sizeof(*record)) ||
           !separate(model.draws,bytes,record->model.bytes,record->model.size) ||
           (record->has_image && !separate(model.draws,bytes,record->image.bytes,record->image.size))) {
            busy_=false;return fail(E::invalid_buffer);
        }
    }
    ActorRenderRegistration registration;registration.key=actor;registration.draws=model.draws;registration.draw_count=model.draw_count;
    for(unsigned i=0;i<16;++i)registration.camera[i]=actual_camera[i];
    for(unsigned i=0;i<3;++i){registration.reference_position[i]=pose.reference_position[i];registration.position[i]=pose.position[i];}
    registration.reference_yaw=pose.reference_yaw;registration.base_yaw=pose.yaw;
    r.world=world_.register_actor(actor,pose.position,pose.yaw);
    if(r.world!=ActorWorldStatus::ok){busy_=false;return fail(E::world_failure);}
    r.world_registered=true;
    r.render=render_.register_actor(registration);
    if(r.render!=ActorRenderStatus::ok) {
        r.world=world_.unregister_actor(actor);r.world_registered=r.world!=ActorWorldStatus::ok;
        if(r.world_registered){entries_[count_++]={actor,placement,true,false};r.remaining=count_;busy_=false;return fail(E::rollback_pending);}
        busy_=false;return fail(E::render_failure);
    }
    r.render_registered=true;entries_[count_++]={actor,placement,true,true};r.remaining=count_;busy_=false;return r;
}
SceneActorResult SceneActors::close() noexcept {
    SceneActorResult r;r.remaining=count_;if(busy_){r.error=E::reentrant;return r;}busy_=true;
    while(count_) {
        auto& e=entries_[count_-1];
        if(e.world){r.world=world_.unregister_actor(e.key);if(r.world!=ActorWorldStatus::ok){r.error=E::world_failure;break;}e.world=false;}
        if(e.render){r.render=render_.unregister_actor(e.key);if(r.render!=ActorRenderStatus::ok){r.error=E::render_failure;break;}e.render=false;}
        entries_[--count_]={};
    }
    r.remaining=count_;if(count_){r.world_registered=entries_[count_-1].world;r.render_registered=entries_[count_-1].render;}
    busy_=false;return r;
}
GameServiceResult SceneActors::apply(void*p,const GameActorKey& key,const GameFacingRequest& request) noexcept {
    if(!p)return GameServiceResult::unavailable;
    auto& self=*static_cast<SceneActors*>(p);if(self.busy_)return GameServiceResult::unavailable;
    self.busy_=true;const auto result=self.world_.apply(key,request);self.busy_=false;return service(result);
}
GameServiceResult SceneActors::clear(void*p,const GameActorKey& key) noexcept {
    if(!p)return GameServiceResult::unavailable;
    auto& self=*static_cast<SceneActors*>(p);if(self.busy_)return GameServiceResult::unavailable;
    self.busy_=true;const auto result=self.world_.clear(key);self.busy_=false;return service(result);
}
GameFacingServices SceneActors::facing_services() noexcept {return {this,apply,clear};}
SceneActorTagResult SceneActors::resolve_tag(const char (&tag)[4]) const noexcept {
    if(busy_)return {SceneActorTagStatus::reentrant,{}};
    std::size_t matches=0;const Entry* match=nullptr;
    for(std::size_t i=0;i<count_;++i) {
        bool equal=true;for(unsigned j=0;j<4;++j)if(entries_[i].placement.object_tag[j]!=tag[j])equal=false;
        if(equal){++matches;match=&entries_[i];}
    }
    if(!matches)return {};
    if(matches>1)return {SceneActorTagStatus::ambiguous,{}};
    if(!match->world || !match->render)return {SceneActorTagStatus::unavailable,{}};
    return {SceneActorTagStatus::found,match->key};
}
}
