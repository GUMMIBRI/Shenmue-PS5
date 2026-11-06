// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "cedar/startup_scene.hpp"
#include "cedar/scene_actors.hpp"

namespace cedar {
struct SceneSessionBinding {std::size_t placement_index{};GameActorKey actor{};};
enum class SceneSessionError : std::uint8_t {
    none,invalid_state,invalid_argument,invalid_buffer,duplicate_binding,
    scene_failure,missing_placement,actor_failure,reentrant
};
struct SceneSessionStatus {
    SceneSessionError error{SceneSessionError::none};
    std::size_t binding_index{static_cast<std::size_t>(-1)};
    StartupSceneStatus scene{},cleanup_scene{};
    SceneActorResult actor{},cleanup_actor{};
    bool cleanup_pending{};
    explicit operator bool() const noexcept {return error==SceneSessionError::none;}
};
// Explicit close before destruction. Caller owns root/index/buffer/draw/GPU
// lifetimes; teardown errors require preserving them and retrying close.
class SceneSession final {
public:
    explicit SceneSession(ActorWorldAxes axes) noexcept:actors_(axes){}
    SceneSession(const SceneSession&)=delete;
    SceneSession& operator=(const SceneSession&)=delete;
    SceneSessionStatus open(StartupResources&,std::size_t requirement_index,
        StartupSceneEncoding,const StartupSceneBuffers&,
        const SceneSessionBinding*,std::size_t count,const float (&camera)[16],
        const SceneActorServices&) noexcept;
    SceneSessionStatus close() noexcept;
    bool ready() const noexcept {return ready_ && !busy_ && scene_.ready();}
    bool cleanup_pending() const noexcept {return active_ && !ready_;}
    std::uint64_t generation() const noexcept {return scene_.generation();}
    GameFacingServices facing_services() noexcept;
    ActorWorldRead read(const GameActorKey&) const noexcept;
    SceneActorTagResult resolve_tag(const char (&tag)[4]) const noexcept;
private:
    struct Span {std::uintptr_t begin{},end{};};
    static bool convert(void*,const ScenePlacementRecord&,SceneActorPose&) noexcept;
    static bool resolve(void*,const ScenePlacementRecord&,SceneActorModel&) noexcept;
    static GameServiceResult apply(void*,const GameActorKey&,const GameFacingRequest&) noexcept;
    static GameServiceResult clear(void*,const GameActorKey&) noexcept;
    bool draw_storage_valid(const SceneActorModel&) const noexcept;
    SceneSessionStatus cleanup() noexcept;
    SceneSessionStatus rollback(SceneSessionStatus) noexcept;
    StartupScene scene_{};
    SceneActors actors_;
    StartupResources* resources_{};
    SceneActorServices source_services_{};
    Span protected_[10]{};
    bool active_{},ready_{},busy_{},building_{},callback_busy_{};
};
} // namespace cedar
