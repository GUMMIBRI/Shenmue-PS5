// Original caller-owned rigid pose to real draw-state bridge.
#pragma once
#include "cedar/actor_world.hpp"
#include "cedar/gpu_renderer.hpp"
namespace cedar {
struct ActorRenderRegistration {
    GameActorKey key{};
    GpuDraw* draws{};std::size_t draw_count{};
    float camera[16]{},reference_position[3]{},position[3]{};
    double reference_yaw{},base_yaw{};
};
enum class ActorRenderStatus : std::uint8_t { ok,invalid_argument,duplicate_actor,missing_actor,full,overflow,unavailable };
class ActorRenderPoses final {
public:
    explicit ActorRenderPoses(ActorWorldAxes axes) noexcept:axes_(axes){}
    ActorRenderPoses(const ActorRenderPoses&)=delete;
    ActorRenderPoses& operator=(const ActorRenderPoses&)=delete;
    ActorRenderStatus register_actor(const ActorRenderRegistration&) noexcept;
    ActorRenderStatus unregister_actor(const GameActorKey&) noexcept;
    ActorRenderStatus update_base_pose(const GameActorKey&,const float (&position)[3],double yaw) noexcept;
    ActorRenderStatus update_camera(const GameActorKey&,const float (&camera)[16]) noexcept;
    ActorPoseServices pose_services() noexcept;
private:
    struct Entry {ActorRenderRegistration value{};double override_yaw{};bool overridden{};};
    ActorRenderStatus commit(const Entry&) noexcept;
    std::size_t find(const GameActorKey&) const noexcept;
    static GameServiceResult set_yaw(void*,const GameActorKey&,double) noexcept;
    static GameServiceResult clear_yaw(void*,const GameActorKey&) noexcept;
    ActorWorldAxes axes_;Entry entries_[64]{};std::size_t count_{};
};
}
