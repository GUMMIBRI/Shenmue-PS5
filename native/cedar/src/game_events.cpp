// SPDX-License-Identifier: GPL-3.0-only
// Adapted from brynnb/new-yokosuka NativeDialogueSceneStaging.js at
// b0df7f2720a7eecabce2e610fdedec35bf1243e3; full GPL retained in
// third_party/licenses/new-yokosuka-GPL3.txt. No generated command/actor data.
#include "cedar/game_events.hpp"
#include <cmath>
namespace cedar {
namespace {
bool same(const char* a,const char* b) {for(unsigned i=0;i<4;++i) if(a[i]!=b[i]) return false;return true;}
bool code_valid(const char* a) {for(unsigned i=0;i<4;++i) if(!a[i]) return false;return true;}
GameEventResult result(GameServiceResult r) {
    switch(r) {
    case GameServiceResult::applied:return GameEventResult::applied;
    case GameServiceResult::unavailable:return GameEventResult::unavailable;
    case GameServiceResult::failed:return GameEventResult::failed;
    } return GameEventResult::failed;
}
}
GameEventResult GameEventFacing::begin(const GameActorKey& input) noexcept {
    if(busy_) return GameEventResult::reentrant;
    const auto candidate=input;
    if(!candidate.instance || !code_valid(candidate.code)) return GameEventResult::invalid_argument;
    if(active_) {const auto cleared=end();if(cleared!=GameEventResult::applied) return cleared;}
    actor_=candidate;active_=true;return GameEventResult::applied;
}
GameEventResult GameEventFacing::apply(const GameFacingRequest& input) noexcept {
    if(busy_) return GameEventResult::reentrant;
    if(!active_) return GameEventResult::no_active_actor;
    auto request=input;
    if(request.kind!=GameFacingKind::target && request.kind!=GameFacingKind::participant_offset)
        return GameEventResult::invalid_argument;
    for(float v:request.world_vector) if(!std::isfinite(v)) return GameEventResult::invalid_argument;
    if(request.has_actor_code && !same(request.actor_code,actor_.code)) return GameEventResult::actor_mismatch;
    if(request.kind==GameFacingKind::participant_offset) {
        if(!same(request.participant_code,actor_.code)) return GameEventResult::actor_mismatch;
        request.activate=true;
    }
    if(!services_.apply) return GameEventResult::unavailable;
    busy_=true;const auto applied=services_.apply(services_.context,actor_,request);busy_=false;
    return result(applied);
}
GameEventResult GameEventFacing::end() noexcept {
    if(busy_) return GameEventResult::reentrant;
    if(!active_) return GameEventResult::no_active_actor;
    if(!services_.clear) return GameEventResult::unavailable;
    busy_=true;const auto cleared=services_.clear(services_.context,actor_);busy_=false;
    const auto r=result(cleared);
    if(r==GameEventResult::applied) {actor_={};active_=false;}
    return r;
}
}
