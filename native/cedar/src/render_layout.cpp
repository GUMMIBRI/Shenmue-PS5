#include "cedar/render_layout.hpp"
#include <cmath>

namespace cedar {
namespace {
constexpr std::uint32_t axis_limit = 8192;
constexpr std::uint64_t maximum_pixel_budget = std::uint64_t{axis_limit} * axis_limit;
std::uint64_t pixels(const RenderExtent& extent) noexcept {
    return std::uint64_t{extent.width} * extent.height;
}
bool extent(const RenderExtent& value) noexcept {
    return value.width && value.height && value.width <= axis_limit && value.height <= axis_limit;
}
bool rectangle(const PixelRect& value, const RenderExtent& bounds) noexcept {
    return value.width && value.height && value.x <= bounds.width && value.y <= bounds.height &&
           value.width <= bounds.width - value.x && value.height <= bounds.height - value.y;
}
bool orientation_valid(LayoutOrientation value) noexcept {
    return value == LayoutOrientation::identity || value == LayoutOrientation::clockwise_90 ||
           value == LayoutOrientation::half_turn || value == LayoutOrientation::clockwise_270;
}
RenderLayoutResult fail(RenderLayoutError error) noexcept {
    RenderLayoutResult result{};
    result.error = error;
    return result;
}
bool fit(double aspect, const RenderExtent& output, PixelRect& result) noexcept {
    // Nearest integer, ties upward. The unscaled dimension fills its output axis;
    // this does not quantize to multiples of 4/3 or 16/9 and discard extra pixels.
    const double output_aspect = static_cast<double>(output.width) / output.height;
    double width = output.width, height = output.height;
    if (output_aspect > aspect) width = std::floor(height * aspect + 0.5);
    else height = std::floor(width / aspect + 0.5);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
        width > output.width || height > output.height)
        return false; // No clamp to a fake one-pixel or down-quality viewport.
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    result = {(output.width - w) / 2, (output.height - h) / 2, w, h};
    return true;
}
bool fsr_storage(SDL_GPUTextureFormat format) noexcept {
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
}
} // namespace

RenderLayoutResult plan_render_layout(const RenderLayoutRequest& request) noexcept {
    const auto settings = validate_graphics_settings(request.settings);
    if (!settings) {
        auto result = fail(RenderLayoutError::invalid_settings);
        result.settings_status = settings;
        return result;
    }
    if (!request.output.width || !request.output.height) return fail(RenderLayoutError::invalid_output);
    if (!extent(request.output)) return fail(RenderLayoutError::extent_limit);
    if (!request.pixel_budget || request.pixel_budget > maximum_pixel_budget)
        return fail(RenderLayoutError::invalid_budget);
    if (pixels(request.output) > request.pixel_budget) return fail(RenderLayoutError::pixel_budget_exceeded);
    if (!orientation_valid(request.orientation)) return fail(RenderLayoutError::invalid_orientation);
    if (request.fit_behavior != LayoutFitBehavior::preserve_source_aspect &&
        request.fit_behavior != LayoutFitBehavior::stretch_game_to_output)
        return fail(RenderLayoutError::invalid_fit_behavior);
    if (!(request.source_pixel_aspect > 0) || !std::isfinite(request.source_pixel_aspect))
        return fail(RenderLayoutError::invalid_aspect);
    if (request.source.content != FrameContent::game && request.source.content != FrameContent::movie &&
        request.source.content != FrameContent::native_ui)
        return fail(RenderLayoutError::invalid_source);

    RenderLayoutResult result{};
    result.orientation = request.orientation;
    if (request.source.content == FrameContent::native_ui) {
        // Native UI is already produced for the actual native output. Do not
        // stretch/scale/postprocess it using gameplay/movie policy or source crop.
        result.viewport = {0, 0, request.output.width, request.output.height};
        result.engine_render_extent = request.output;
        result.orientation = LayoutOrientation::identity;
        result.bypass_post = true;
        return result;
    }
    const RenderExtent source_extent{request.source.width, request.source.height};
    if (!extent(source_extent) || !rectangle(request.source.crop, source_extent))
        return fail(RenderLayoutError::invalid_source);
    const auto& crop = request.source.crop;
    double source_aspect = (static_cast<double>(crop.width) / crop.height) * request.source_pixel_aspect;
    if (!(source_aspect > 0) || !std::isfinite(source_aspect)) return fail(RenderLayoutError::invalid_aspect);
    if (request.orientation == LayoutOrientation::clockwise_90 ||
        request.orientation == LayoutOrientation::clockwise_270)
        source_aspect = 1.0 / source_aspect;
    if (!(source_aspect > 0) || !std::isfinite(source_aspect)) return fail(RenderLayoutError::invalid_aspect);
    result.orientation_transform_required = request.orientation != LayoutOrientation::identity;

    if (request.source.content == FrameContent::movie) {
        if (!fit(source_aspect, request.output, result.viewport)) return fail(RenderLayoutError::invalid_aspect);
        result.movie_aspect_preserved = true;
        // A decoded movie is not the game's internally rendered scene. Its
        // actual crop extent is reported, never a fictional render-scale change.
        result.engine_render_extent = {crop.width, crop.height};
    } else {
        switch (request.settings.aspect) {
        case GraphicsAspect::preserve_engine:
            if (!rectangle(request.engine_viewport, request.output))
                return fail(RenderLayoutError::invalid_engine_viewport);
            result.viewport = request.engine_viewport;
            break;
        case GraphicsAspect::four_three:
            if (!fit(4.0 / 3.0, request.output, result.viewport)) return fail(RenderLayoutError::invalid_aspect);
            break;
        case GraphicsAspect::sixteen_nine:
            if (!fit(16.0 / 9.0, request.output, result.viewport)) return fail(RenderLayoutError::invalid_aspect);
            break;
        case GraphicsAspect::fit:
            if (request.fit_behavior == LayoutFitBehavior::stretch_game_to_output) {
                result.viewport = {0, 0, request.output.width, request.output.height};
                result.gameplay_stretched = true;
            } else if (!fit(source_aspect, request.output, result.viewport)) {
                return fail(RenderLayoutError::invalid_aspect);
            }
            break;
        default: return fail(RenderLayoutError::invalid_settings); // Validator normally rejects this first.
        }
        if (!extent(request.engine_base_extent)) return fail(RenderLayoutError::invalid_engine_extent);
        result.engine_render_extent = request.engine_base_extent;
        if (request.settings.render_scale == GraphicsOverride::native) {
            const auto percent = request.settings.render_scale_percent;
            const std::uint64_t width = (std::uint64_t{request.engine_base_extent.width} * percent + 50) / 100;
            const std::uint64_t height = (std::uint64_t{request.engine_base_extent.height} * percent + 50) / 100;
            if (!width || !height || width > axis_limit || height > axis_limit)
                return fail(RenderLayoutError::extent_limit);
            result.engine_render_extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
            result.engine_extent_requested = true;
        }
    }
    if (pixels(result.engine_render_extent) > request.pixel_budget)
        return fail(RenderLayoutError::pixel_budget_exceeded);
    // Eligibility is a data/geometric condition, not selected implementation,
    // engine ownership, shader availability, speed or an already changed source.
    result.fsr_eligible = !result.orientation_transform_required && request.source.opaque &&
        request.source.encoding == PostColorEncoding::perceptual_sdr_unorm &&
        fsr_storage(request.source.format) && crop.width <= result.viewport.width &&
        crop.height <= result.viewport.height;
    return result;
}
} // namespace cedar
