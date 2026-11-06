// SPDX-License-Identifier: GPL-3.0-only
#include "cedar/scene_session.hpp"
#include <cmath>
#include <limits>
#include <initializer_list>

namespace cedar { namespace {
bool separate(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) noexcept {
    return a==b || c==d || b<=c || d<=a;
}
SceneSessionStatus fail(SceneSessionError e,std::size_t i=static_cast<std::size_t>(-1)) noexcept {
    SceneSessionStatus s;s.error=e;s.binding_index=i;return s;
}
} // namespace
SceneSessionStatus SceneSession::cleanup() noexcept {
    ready_=false;
    SceneSessionStatus result;
    result.actor=actors_.close();
    if(!result.actor) {result.error=SceneSessionError::actor_failure;result.cleanup_pending=active_=true;return result;}
    result.scene=scene_.close();
    if(!result.scene) {result.error=SceneSessionError::scene_failure;result.cleanup_pending=active_=true;return result;}
    active_=false;resources_=nullptr;source_services_={};
    for(auto& p:protected_) p={};
    return result;
}
SceneSessionStatus SceneSession::rollback(SceneSessionStatus result) noexcept {
    building_=false;
    const auto closed=cleanup();result.cleanup_actor=closed.actor;result.cleanup_scene=closed.scene;
    result.cleanup_pending=closed.cleanup_pending;return result;
}
SceneSessionStatus SceneSession::close() noexcept {
    if(busy_) return fail(SceneSessionError::reentrant);
    busy_=true;const auto result=cleanup();busy_=false;return result;
}
SceneSessionStatus SceneSession::open(StartupResources& resources,std::size_t requirement,
    StartupSceneEncoding encoding,const StartupSceneBuffers& buffers,
    const SceneSessionBinding* bindings,std::size_t count,const float (&camera)[16],
    const SceneActorServices& services) noexcept {
    if(busy_) return fail(SceneSessionError::reentrant);
    if(active_ || ready_) return fail(SceneSessionError::invalid_state);
    if(!resources.ready() || !count || count>64 || !services.convert || !services.resolve)
        return fail(SceneSessionError::invalid_argument);
    Span checked[10]{};
    const auto span=[&](const void* p,std::size_t n,std::size_t size,std::size_t align,Span& s) noexcept {
        const auto max=std::numeric_limits<std::uintptr_t>::max();s.begin=reinterpret_cast<std::uintptr_t>(p);
        if((n && (!p || s.begin%align)) || n>max/size || n*size>max-s.begin) return false;
        s.end=s.begin+n*size;return true;
    };
    if(!span(this,1,sizeof(*this),alignof(SceneSession),checked[0]) ||
       !span(&resources,1,sizeof(resources),alignof(StartupResources),checked[1]) ||
       !span(&buffers,1,sizeof(buffers),alignof(StartupSceneBuffers),checked[2]) ||
       !span(bindings,count,sizeof(*bindings),alignof(SceneSessionBinding),checked[3]) ||
       !span(&camera,1,sizeof(camera),alignof(float),checked[4]) ||
       !span(&services,1,sizeof(services),alignof(SceneActorServices),checked[5]) ||
       !span(buffers.stored,buffers.stored_capacity,1,1,checked[6]) ||
       !span(buffers.decoded,buffers.decoded_capacity,1,1,checked[7]) ||
       !span(buffers.gzip,buffers.gzip?1:0,sizeof(GzipWorkspace),alignof(GzipWorkspace),checked[8]) ||
       !span(buffers.records,buffers.record_capacity,sizeof(ScenePlacementRecord),alignof(ScenePlacementRecord),checked[9]))
        return fail(SceneSessionError::invalid_buffer);
    for(unsigned i=0;i<10;++i) {
        if(i!=1 && !resources.storage_disjoint(reinterpret_cast<const void*>(checked[i].begin),checked[i].end-checked[i].begin))
            return fail(SceneSessionError::invalid_buffer);
        for(unsigned j=i+1;j<10;++j) if(!separate(checked[i].begin,checked[i].end,checked[j].begin,checked[j].end))
            return fail(SceneSessionError::invalid_buffer);
    }
    const void* contexts[]={services.converter_context,services.resolver_context};
    for(const void* p:contexts) if(p) {
        Span s;if(!span(p,1,1,1,s) || !resources.storage_disjoint(p,1)) return fail(SceneSessionError::invalid_buffer);
        for(auto r:checked) if(!separate(s.begin,s.end,r.begin,r.end)) return fail(SceneSessionError::invalid_buffer);
    }
    bool camera_nonzero=false;
    for(float f:camera) {if(!std::isfinite(f)) return fail(SceneSessionError::invalid_argument);camera_nonzero|=f!=0;}
    if(!camera_nonzero) return fail(SceneSessionError::invalid_argument);
    for(std::size_t i=0;i<count;++i) {
        if(!bindings[i].actor.instance || bindings[i].placement_index>=SceneResourceView::max_records)
            return fail(SceneSessionError::invalid_argument,i);
        for(char c:bindings[i].actor.code) if(!c) return fail(SceneSessionError::invalid_argument,i);
        for(std::size_t j=0;j<i;++j) if(bindings[i].placement_index==bindings[j].placement_index || bindings[i].actor.instance==bindings[j].actor.instance)
            return fail(SceneSessionError::duplicate_binding,i);
    }
    busy_=true;active_=true;resources_=&resources;source_services_=services;
    for(unsigned i=0;i<10;++i) protected_[i]=checked[i];
    SceneSessionStatus result;
    result.scene=scene_.open(resources,requirement,encoding,buffers);
    if(!result.scene) {result.error=SceneSessionError::scene_failure;result=rollback(result);busy_=false;return result;}
    const auto* view=scene_.view();
    if(!view) {result.error=SceneSessionError::scene_failure;result=rollback(result);busy_=false;return result;}
    for(std::size_t i=0;i<count;++i) if(bindings[i].placement_index>=view->record_count()) {
        result.error=SceneSessionError::missing_placement;result.binding_index=i;result=rollback(result);busy_=false;return result;
    }
    building_=true;
    const SceneActorServices guarded{convert,this,resolve,this};
    for(std::size_t i=0;i<count;++i) {
        result.actor=actors_.add(*view,bindings[i].placement_index,bindings[i].actor,camera,guarded);
        if(!result.actor) {result.error=SceneSessionError::actor_failure;result.binding_index=i;
            result=rollback(result);busy_=false;return result;}
    }
    building_=false;ready_=true;busy_=false;return result;
}
bool SceneSession::convert(void* p,const ScenePlacementRecord& placement,SceneActorPose& out) noexcept {
    auto& self=*static_cast<SceneSession*>(p);
    if(!self.building_ || !self.busy_ || self.callback_busy_ || !self.source_services_.convert) return false;
    self.callback_busy_=true;const auto r=self.source_services_.convert(self.source_services_.converter_context,placement,out);
    self.callback_busy_=false;return r;
}
bool SceneSession::draw_storage_valid(const SceneActorModel& model) const noexcept {
    if(!model.draws || !model.draw_count || model.draw_count>4096 || reinterpret_cast<std::uintptr_t>(model.draws)%alignof(GpuDraw)) return false;
    const auto a=reinterpret_cast<std::uintptr_t>(model.draws),bytes=model.draw_count*sizeof(GpuDraw);
    if(bytes>std::numeric_limits<std::uintptr_t>::max()-a || !resources_ || !resources_->storage_disjoint(model.draws,bytes)) return false;
    for(auto p:protected_) if(!separate(a,a+bytes,p.begin,p.end)) return false;
    for(const void* context:{source_services_.converter_context,source_services_.resolver_context}) if(context) {
        const auto address=reinterpret_cast<std::uintptr_t>(context);
        if(address>=a && address<a+bytes) return false;
    }
    return true;
}
bool SceneSession::resolve(void* p,const ScenePlacementRecord& placement,SceneActorModel& out) noexcept {
    auto& self=*static_cast<SceneSession*>(p);
    if(!self.building_ || !self.busy_ || self.callback_busy_ || !self.source_services_.resolve) return false;
    self.callback_busy_=true;const auto r=self.source_services_.resolve(self.source_services_.resolver_context,placement,out);
    self.callback_busy_=false;return r && self.draw_storage_valid(out);
}
GameServiceResult SceneSession::apply(void* p,const GameActorKey& key,const GameFacingRequest& request) noexcept {
    if(!p) return GameServiceResult::unavailable;
    auto& self=*static_cast<SceneSession*>(p);if(!self.ready()) return GameServiceResult::unavailable;
    self.busy_=true;const auto services=self.actors_.facing_services();
    const auto result=services.apply?services.apply(services.context,key,request):GameServiceResult::unavailable;
    self.busy_=false;return result;
}
GameServiceResult SceneSession::clear(void* p,const GameActorKey& key) noexcept {
    if(!p) return GameServiceResult::unavailable;
    auto& self=*static_cast<SceneSession*>(p);if(!self.ready()) return GameServiceResult::unavailable;
    self.busy_=true;const auto services=self.actors_.facing_services();
    const auto result=services.clear?services.clear(services.context,key):GameServiceResult::unavailable;
    self.busy_=false;return result;
}
GameFacingServices SceneSession::facing_services() noexcept {return {this,apply,clear};}
ActorWorldRead SceneSession::read(const GameActorKey& key) const noexcept {
    return ready()?actors_.read(key):ActorWorldRead{busy_?ActorWorldStatus::reentrant:ActorWorldStatus::unavailable,{}};
}
SceneActorTagResult SceneSession::resolve_tag(const char (&tag)[4]) const noexcept {
    return ready()?actors_.resolve_tag(tag):SceneActorTagResult{busy_?SceneActorTagStatus::reentrant:SceneActorTagStatus::unavailable,{}};
}
} // namespace cedar
