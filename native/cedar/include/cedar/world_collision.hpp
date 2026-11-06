// SPDX-License-Identifier: GPL-3.0-only
// Authored-reference planar movement; docs/WORLD-COLLISION.md.
#pragma once
#include <cstddef>

namespace cedar {
struct WorldCollisionSegment {
    double start_x{},start_z{},end_x{},end_z{};
    bool enabled{},blocking{},has_y_range{};
    double lower_y{},upper_y{};
};
struct WorldCollisionMove {
    double x{},y{},z{},displacement_x{},displacement_z{};
    // Explicit caller world-unit policy; no original/controller defaults.
    double radius{},height{},epsilon{},vertical_tolerance{},segment_epsilon_squared{};
};
enum class WorldCollisionError { none,invalid_buffer,address_overflow,overlapping_storage,
    resource_limit,invalid_policy,non_finite,coordinate_limit,invalid_segment };
enum class WorldCollisionBlock { none,swept_boundary,contact_not_improving,contact_center_crossing };
struct WorldCollisionStep {
    bool allowed{},escaping{};
    double x{},y{},z{}; // Actual accepted position; blocked keeps original.
    WorldCollisionBlock block{WorldCollisionBlock::none};
    std::size_t segment_index{static_cast<std::size_t>(-1)};
};
struct WorldCollisionResult {
    WorldCollisionError error{WorldCollisionError::none};
    std::size_t segment_index{static_cast<std::size_t>(-1)};
    explicit operator bool() const noexcept {return error==WorldCollisionError::none;}
};
// Actual all-or-nothing planar step. No sliding/grounding/velocity integration.
// Segments already have caller-established world axes/door transformations.
// Full input/output disjointness+finite preflight; errors leave output unchanged.
// Blocked is a successful query with allowed=false and original position.
WorldCollisionResult step_world_collision(const WorldCollisionSegment*,std::size_t count,
    const WorldCollisionMove&,WorldCollisionStep&) noexcept;
struct WorldTerrainTransition {double current_y{},height{},normal_y{},max_step_up{},max_drop{},max_slope_degrees{};};
struct WorldTerrainResult {WorldCollisionError error{WorldCollisionError::none};bool allowed{};};
// Uses an actual supplied face-normal Y and terrain height, no inferred up normal.
WorldTerrainResult world_terrain_transition(const WorldTerrainTransition&) noexcept;
} // namespace cedar
