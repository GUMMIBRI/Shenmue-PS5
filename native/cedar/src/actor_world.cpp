// SPDX-License-Identifier: GPL-3.0-only
// Adapted from authored ScheduledActorRuntime.js b0df7f2720a7eecabce2e610fdedec35bf1243e3.
// Full license: third_party/licenses/new-yokosuka-GPL3.txt; docs/ACTOR-WORLD.md.
#include "cedar/actor_world.hpp"
#include <cmath>
#include <limits>

namespace cedar { namespace {
bool same_code(const char* a,const char* b) noexcept {
    for(unsigned i=0;i<4;++i) if(a[i]!=b[i]) return false;
    return true;
}
bool key_valid(const GameActorKey& key) noexcept {
    if(!key.instance) return false;
    for(char c:key.code) if(!c) return false;
    return true;
}
bool finite(const float* p) noexcept {return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);}
ActorWorldStatus status(GameServiceResult s) noexcept {
    switch(s) {
    case GameServiceResult::applied:return ActorWorldStatus::ok;
    case GameServiceResult::unavailable:return ActorWorldStatus::unavailable;
    default:return ActorWorldStatus::failed;
    }
}
GameServiceResult service(ActorWorldStatus s) noexcept {
    switch(s) {
    case ActorWorldStatus::ok:return GameServiceResult::applied;
    case ActorWorldStatus::missing_actor:case ActorWorldStatus::unavailable:return GameServiceResult::unavailable;
    default:return GameServiceResult::failed;
    }
}
bool revision_full(const ActorWorldState& s) noexcept {return s.revision==std::numeric_limits<std::uint64_t>::max();}
} // namespace
std::size_t ActorWorld::find(const GameActorKey& key) const noexcept {
    for(std::size_t i=0;i<count_;++i)
        if(actors_[i].key.instance==key.instance && same_code(actors_[i].key.code,key.code)) return i;
    return count_;
}
ActorWorldStatus ActorWorld::register_actor(const GameActorKey& input,const float (&position)[3],double yaw) noexcept {
    if(busy_) return ActorWorldStatus::reentrant;
    const auto key=input;
    if(axes_!=ActorWorldAxes::y_up_yaw_z_toward_x || !key_valid(key) || !finite(position) || !std::isfinite(yaw))
        return ActorWorldStatus::invalid_argument;
    // Instance handles cannot be reused with a second code while registered.
    for(std::size_t i=0;i<count_;++i) if(actors_[i].key.instance==key.instance) return ActorWorldStatus::duplicate_actor;
    if(count_==maximum_actors) return ActorWorldStatus::full;
    ActorWorldState next{}; next.key=key; next.base_yaw=yaw; next.yaw=yaw; next.revision=1;
    for(unsigned i=0;i<3;++i) next.position[i]=position[i];
    actors_[count_++]=next;
    return ActorWorldStatus::ok;
}
ActorWorldStatus ActorWorld::apply_pose(ActorWorldState& next) noexcept {
    if(!next.has_facing || !next.facing_active) return ActorWorldStatus::ok;
    double dx=next.facing.world_vector[0], dz=next.facing.world_vector[2];
    if(next.facing.kind==GameFacingKind::target) {dx-=next.position[0]; dz-=next.position[2];}
    // Offset target = current position + offset; subtract algebraically in double.
    // Source leaves previous orientation unchanged for coincident horizontal target.
    if(dx==0 && dz==0) return ActorWorldStatus::ok;
    // Do not install an override the owner has no way to remove later.
    if(!poses_.set_yaw || !poses_.clear_yaw) return ActorWorldStatus::unavailable;
    const double yaw=std::atan2(dx,dz);
    busy_=true; const auto applied=poses_.set_yaw(poses_.context,next.key,yaw); busy_=false;
    const auto r=status(applied);
    if(r==ActorWorldStatus::ok) {next.yaw=yaw; next.pose_override_applied=true;}
    return r;
}
ActorWorldStatus ActorWorld::apply(const GameActorKey& key,const GameFacingRequest& input) noexcept {
    if(busy_) return ActorWorldStatus::reentrant;
    const auto i=find(key); if(i==count_) return ActorWorldStatus::missing_actor;
    const auto request=input;
    if((request.kind!=GameFacingKind::target && request.kind!=GameFacingKind::participant_offset) ||
       !finite(request.world_vector) || (request.has_actor_code && !same_code(request.actor_code,key.code)) ||
       (request.kind==GameFacingKind::participant_offset && !same_code(request.participant_code,key.code)))
        return ActorWorldStatus::invalid_argument;
    if(revision_full(actors_[i])) return ActorWorldStatus::overflow;
    auto next=actors_[i]; next.facing=request; next.has_facing=true;
    next.facing_active=request.activate || next.facing_active || request.kind==GameFacingKind::participant_offset;
    const auto r=apply_pose(next); if(r!=ActorWorldStatus::ok) return r;
    ++next.revision; actors_[i]=next; return ActorWorldStatus::ok;
}
ActorWorldStatus ActorWorld::update_pose(const GameActorKey& key,const float (&position)[3],double yaw) noexcept {
    if(busy_) return ActorWorldStatus::reentrant;
    const auto i=find(key); if(i==count_) return ActorWorldStatus::missing_actor;
    if(!finite(position) || !std::isfinite(yaw)) return ActorWorldStatus::invalid_argument;
    if(revision_full(actors_[i])) return ActorWorldStatus::overflow;
    auto next=actors_[i];
    for(unsigned n=0;n<3;++n) next.position[n]=position[n];
    next.base_yaw=yaw;
    // An already-applied override persists until a new noncoincident facing or clear.
    if(!next.pose_override_applied) next.yaw=yaw;
    const auto r=apply_pose(next); if(r!=ActorWorldStatus::ok) return r;
    ++next.revision; actors_[i]=next; return ActorWorldStatus::ok;
}
ActorWorldStatus ActorWorld::clear(const GameActorKey& key) noexcept {
    if(busy_) return ActorWorldStatus::reentrant;
    const auto i=find(key); if(i==count_) return ActorWorldStatus::missing_actor;
    if(!actors_[i].has_facing) return ActorWorldStatus::ok; // Already absent, no gameplay effect claimed.
    if(revision_full(actors_[i])) return ActorWorldStatus::overflow;
    if(actors_[i].pose_override_applied) {
        if(!poses_.clear_yaw) return ActorWorldStatus::unavailable;
        busy_=true; const auto cleared=poses_.clear_yaw(poses_.context,actors_[i].key); busy_=false;
        const auto r=status(cleared); if(r!=ActorWorldStatus::ok) return r;
    }
    auto& s=actors_[i]; s.facing={}; s.has_facing=false; s.facing_active=false; s.pose_override_applied=false;
    s.yaw=s.base_yaw;
    ++s.revision; return ActorWorldStatus::ok;
}
ActorWorldStatus ActorWorld::unregister_actor(const GameActorKey& key) noexcept {
    if(busy_) return ActorWorldStatus::reentrant;
    const auto i=find(key); if(i==count_) return ActorWorldStatus::missing_actor;
    const auto r=clear(key); if(r!=ActorWorldStatus::ok) return r;
    for(auto n=i+1;n<count_;++n) actors_[n-1]=actors_[n];
    actors_[--count_]={}; return ActorWorldStatus::ok;
}
ActorWorldRead ActorWorld::read(const GameActorKey& key) const noexcept {
    if(busy_) return {ActorWorldStatus::reentrant,{}};
    const auto i=find(key);
    return i==count_ ? ActorWorldRead{} : ActorWorldRead{ActorWorldStatus::ok,actors_[i]};
}
GameServiceResult ActorWorld::service_apply(void* p,const GameActorKey& k,const GameFacingRequest& r) noexcept {
    return p ? service(static_cast<ActorWorld*>(p)->apply(k,r)) : GameServiceResult::unavailable;
}
GameServiceResult ActorWorld::service_clear(void* p,const GameActorKey& k) noexcept {
    return p ? service(static_cast<ActorWorld*>(p)->clear(k)) : GameServiceResult::unavailable;
}
GameFacingServices ActorWorld::facing_services() noexcept {return {this,service_apply,service_clear};}
} // namespace cedar
