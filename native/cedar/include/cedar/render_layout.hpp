// Original CPU-only aspect/extent planning; not a producer/camera mutation.
#pragma once
#include "cedar/graphics_settings.hpp"
#include "cedar/post_types.hpp"

namespace cedar {
struct RenderExtent { std::uint32_t width{}, height{}; };
enum class LayoutOrientation : std::uint8_t { identity, clockwise_90, half_turn, clockwise_270 };
enum class LayoutFitBehavior : std::uint8_t { preserve_source_aspect, stretch_game_to_output };
enum class RenderLayoutError : std::uint8_t {
    none, invalid_settings, invalid_source, invalid_output, invalid_aspect,
    invalid_orientation, invalid_fit_behavior, invalid_engine_extent,
    invalid_engine_viewport, invalid_budget, extent_limit, pixel_budget_exceeded
};
struct RenderLayoutRequest {
    PostSource source{};
    GraphicsSettings settings{};
    RenderExtent output{};
    // Actual source pixel width/height ratio. Zero deliberately requires input.
    double source_pixel_aspect{};
    LayoutOrientation orientation{LayoutOrientation::identity};
    // Explicit stretch applies only to gameplay settings.aspect==fit.
    LayoutFitBehavior fit_behavior{LayoutFitBehavior::preserve_source_aspect};
    // Actual engine viewport for preserve_engine; not guessed by this planner.
    PixelRect engine_viewport{};
    // Explicit unscaled engine render baseline, avoiding scale compounding from
    // an already upscaled/cropped producer. Required only for game content.
    RenderExtent engine_base_extent{};
    // Per-image pixel cap for output and planned render extent, not total bytes.
    std::uint64_t pixel_budget{};
};
struct RenderLayoutResult {
    RenderLayoutError error{RenderLayoutError::none};
    GraphicsSettingsStatus settings_status{};
    PixelRect viewport{};
    RenderExtent engine_render_extent{};
    LayoutOrientation orientation{LayoutOrientation::identity};
    bool engine_extent_requested{}, bypass_post{}, movie_aspect_preserved{};
    bool gameplay_stretched{}, fsr_eligible{}, orientation_transform_required{};
    explicit operator bool() const noexcept { return error == RenderLayoutError::none; }
};

// Extents <=8192 on each axis, pixel_budget 1..8192^2. Errors never downsize or
// report a partial plan. No GPU/window/event/asset or source-pixel operations.
RenderLayoutResult plan_render_layout(const RenderLayoutRequest& request) noexcept;
} // namespace cedar
