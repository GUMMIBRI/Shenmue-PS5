// SPDX-License-Identifier: GPL-3.0-only
// Native composition of source-informed scene/facing APIs; SCENE-ACTORS.md.
#pragma once
#include "cedar/scene_resource.hpp"
#include "cedar/actor_render.hpp"
namespace cedar {
struct SceneActorPose {float position[3]{},reference_position[3]{};double yaw{},reference_yaw{};};
struct SceneActorModel {SceneResourceString resolved_model{};GpuDraw* draws{};std::size_t draw_count{};};
using SceneActorConverter=bool (*)(void*,const ScenePlacementRecord&,SceneActorPose&) noexcept;
using SceneActorResolver=bool (*)(void*,const ScenePlacementRecord&,SceneActorModel&) noexcept;
struct SceneActorServices {SceneActorConverter convert{};void* converter_context{};SceneActorResolver resolve{};void* resolver_context{};};
enum class SceneActorError : std::uint8_t { none,invalid_argument,invalid_buffer,missing_record,
    duplicate_actor,full,conversion_unavailable,model_unavailable,model_mismatch,world_failure,render_failure,rollback_pending,reentrant };
struct SceneActorResult {
    SceneActorError error{SceneActorError::none};ActorWorldStatus world{ActorWorldStatus::ok};
    ActorRenderStatus render{ActorRenderStatus::ok};
    std::size_t remaining{};bool world_registered{},render_registered{};
    explicit operator bool() const noexcept{return error==SceneActorError::none;}
};
enum class SceneActorTagStatus : std::uint8_t { found,missing,ambiguous,unavailable,reentrant };
struct SceneActorTagResult {SceneActorTagStatus status{SceneActorTagStatus::missing};GameActorKey key{};};
class SceneActors final {
public:
    explicit SceneActors(ActorWorldAxes axes) noexcept:render_(axes),world_(axes,render_.pose_services()){}
    SceneActors(const SceneActors&)=delete;SceneActors& operator=(const SceneActors&)=delete;
    SceneActorResult add(const SceneResourceView&,std::size_t,const GameActorKey&,
        const float (&camera)[16],const SceneActorServices&) noexcept;
    SceneActorResult close() noexcept;
    GameFacingServices facing_services() noexcept;
    ActorWorldRead read(const GameActorKey& key) const noexcept {
        return busy_?ActorWorldRead{ActorWorldStatus::reentrant,{}}:world_.read(key);
    }
    SceneActorTagResult resolve_tag(const char (&tag)[4]) const noexcept;
    std::size_t count() const noexcept{return count_;}
private:
    struct Entry {GameActorKey key{};ScenePlacementRecord placement{};bool world{},render{};};
    static GameServiceResult apply(void*,const GameActorKey&,const GameFacingRequest&) noexcept;
    static GameServiceResult clear(void*,const GameActorKey&) noexcept;
    ActorRenderPoses render_;ActorWorld world_;Entry entries_[64]{};
    std::size_t count_{};bool busy_{};
};
}
