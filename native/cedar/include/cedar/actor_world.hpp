// SPDX-License-Identifier: GPL-3.0-only
// Adapted authored facing state/consumer; see docs/ACTOR-WORLD.md.
#pragma once
#include "cedar/game_events.hpp"
#include <cstddef>

namespace cedar {
enum class ActorWorldStatus : std::uint8_t {
    ok, invalid_argument, duplicate_actor, missing_actor, full,
    unavailable, failed, reentrant, overflow
};
// Caller explicitly converts native coordinates to this convention, or confirms
// its producer/pose consumer use it. No automatic browser/native conversion.
enum class ActorWorldAxes : std::uint8_t { unspecified, y_up_yaw_z_toward_x };
struct ActorPoseServices {
    void* context{};
    // Apply/remove a real dialogue yaw override. Neither function moves position.
    // Successful clear restores the owner's latest explicitly supplied base yaw.
    // applied means the actual pose owner accepted it, not merely queued a name.
    GameServiceResult (*set_yaw)(void*, const GameActorKey&, double radians) noexcept{};
    GameServiceResult (*clear_yaw)(void*, const GameActorKey&) noexcept{};
};
struct ActorWorldState {
    GameActorKey key{};
    float position[3]{};
    double base_yaw{}; // Latest explicit yaw supplied by the actual pose owner.
    double yaw{}; // Effective base yaw or successfully installed override.
    GameFacingRequest facing{};
    bool has_facing{}, facing_active{}, pose_override_applied{};
    std::uint64_t revision{};
};
struct ActorWorldRead {
    ActorWorldStatus status{ActorWorldStatus::missing_actor};
    ActorWorldState state{};
};
class ActorWorld final {
public:
    static constexpr std::size_t maximum_actors = 64;
    explicit ActorWorld(ActorWorldAxes axes, ActorPoseServices poses) noexcept : axes_(axes), poses_(poses) {}
    ActorWorld(const ActorWorld&)=delete;
    ActorWorld& operator=(const ActorWorld&)=delete;
    ActorWorldStatus register_actor(const GameActorKey&, const float (&position)[3], double base_yaw) noexcept;
    ActorWorldStatus unregister_actor(const GameActorKey&) noexcept;
    // Actual owner supplies new position/base yaw; active facing is recomputed.
    ActorWorldStatus update_pose(const GameActorKey&, const float (&position)[3], double base_yaw) noexcept;
    ActorWorldRead read(const GameActorKey&) const noexcept;
    ActorWorldStatus apply(const GameActorKey&, const GameFacingRequest&) noexcept;
    ActorWorldStatus clear(const GameActorKey&) noexcept;
    GameFacingServices facing_services() noexcept;
    std::size_t count() const noexcept { return count_; }
private:
    static GameServiceResult service_apply(void*,const GameActorKey&,const GameFacingRequest&) noexcept;
    static GameServiceResult service_clear(void*,const GameActorKey&) noexcept;
    std::size_t find(const GameActorKey&) const noexcept;
    ActorWorldStatus apply_pose(ActorWorldState&) noexcept;
    ActorWorldAxes axes_;
    ActorPoseServices poses_;
    ActorWorldState actors_[maximum_actors]{};
    std::size_t count_{};
    bool busy_{};
};
} // namespace cedar
