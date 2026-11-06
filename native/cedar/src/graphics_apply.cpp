#include "cedar/graphics_apply.hpp"
#include <cmath>

namespace cedar {
namespace {
constexpr std::uint32_t bit(GraphicsFeature feature) noexcept {
    return std::uint32_t{1} << static_cast<unsigned>(feature);
}
bool unit(float value) noexcept { return value >= 0 && value <= 1; }
bool numeric_sdr(SDL_GPUTextureFormat format) noexcept {
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
        format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
        format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
}
bool original_available(const GraphicsApplyInputs& inputs, SDL_GPUFilter filter) noexcept {
    return inputs.original_presenter_available &&
        ((filter == SDL_GPU_FILTER_NEAREST && inputs.original_nearest_supported) ||
         (filter == SDL_GPU_FILTER_LINEAR && inputs.original_linear_supported));
}
bool address(GpuTextureAddress value) noexcept {
    return value == GpuTextureAddress::repeat || value == GpuTextureAddress::mirrored_repeat ||
        value == GpuTextureAddress::clamp_to_edge;
}
GraphicsApplyResult fail(GraphicsApplyResult result, GraphicsApplyError error, GraphicsFeature feature) noexcept {
    result.error = error;
    result.feature = feature;
    result.plan = {}; // Never expose a partly enabled native plan.
    result.resolution.native_mask = 0;
    if (feature != GraphicsFeature::count) {
        result.resolution.blocked_mask |= bit(feature);
        result.resolution.decisions[static_cast<unsigned>(feature)] = GraphicsDecision::unavailable;
    }
    return result;
}
} // namespace

GraphicsApplyResult plan_graphics_apply(const GraphicsSettings& settings,
    const GraphicsCapabilities& capabilities, const GraphicsApplyInputs& inputs) noexcept {
    GraphicsApplyResult result{};
    result.settings_status = resolve_graphics_settings(settings, capabilities, result.resolution);
    if (!result.settings_status) {
        result.error = GraphicsApplyError::resolution_failure;
        result.feature = result.settings_status.feature;
        return result;
    }
    if (settings.gamma == GraphicsOverride::native)
        return fail(result, GraphicsApplyError::unimplemented_feature, GraphicsFeature::gamma);
    if (settings.saturation == GraphicsOverride::native)
        return fail(result, GraphicsApplyError::unimplemented_feature, GraphicsFeature::saturation);
    if (!result.resolution.native_mask) return result; // Full preserve: no producer requirements or updates.

    GraphicsApplyPlan plan{};
    plan.layout_request = inputs.layout;
    plan.layout_request.settings = settings; // Authoritative validated/resolved request.
    const auto content = inputs.layout.source.content;
    if (content != FrameContent::game && content != FrameContent::movie && content != FrameContent::native_ui)
        return fail(result, GraphicsApplyError::producer_not_eligible, GraphicsFeature::count);
    if (content == FrameContent::native_ui) {
        plan.bypass_post = true;
        plan.inactive_content_mask = result.resolution.native_mask;
        result.plan = plan; // Native UI: no native sampler/effect/layout updates.
        return result;
    }
    if (content == FrameContent::game)
        plan.inactive_content_mask = result.resolution.native_mask &
            (bit(GraphicsFeature::movie_borders) | bit(GraphicsFeature::movie_cleanup));
    else if (content == FrameContent::movie)
        plan.inactive_content_mask = result.resolution.native_mask &
            (bit(GraphicsFeature::render_scale) | bit(GraphicsFeature::aspect));
    plan.native_mask = result.resolution.native_mask & ~plan.inactive_content_mask;
    if (!plan.native_mask) { result.plan = plan; return result; }
    const auto selected = [&](GraphicsFeature feature) noexcept { return (plan.native_mask & bit(feature)) != 0; };
    plan.layout_update = selected(GraphicsFeature::render_scale) || selected(GraphicsFeature::aspect);
    if (plan.native_mask != bit(GraphicsFeature::sampling)) {
        result.layout_attempted = true;
        plan.layout = plan_render_layout(plan.layout_request);
        result.layout_diagnostic = plan.layout;
        if (!plan.layout) return fail(result, GraphicsApplyError::layout_failure, GraphicsFeature::count);
    }

    const auto& source = inputs.layout.source;
    if (selected(GraphicsFeature::upscale) || selected(GraphicsFeature::rcas)) {
        if (inputs.fsr_baseline) plan.fsr = *inputs.fsr_baseline;
        if (!(plan.fsr.sharpness_stops >= 0 && plan.fsr.sharpness_stops <= 2))
            return fail(result, GraphicsApplyError::invalid_baseline, GraphicsFeature::rcas);
        const bool old_active_rcas = plan.fsr.enabled && plan.fsr.rcas_enabled;
        if (selected(GraphicsFeature::upscale)) {
            if (settings.upscale == GraphicsUpscale::fsr1) {
                // A latent true RCAS field on a disabled fresh module is not an
                // existing effect to preserve or permission to duplicate engine RCAS.
                if (!plan.fsr.enabled && settings.rcas == GraphicsRcas::preserve_engine)
                    plan.fsr.rcas_enabled = false;
                plan.fsr.enabled = true;
                plan.presentation = GraphicsPresentDirective::fsr1;
            } else {
                if (old_active_rcas && settings.rcas == GraphicsRcas::preserve_engine)
                    return fail(result, GraphicsApplyError::preserve_rcas_conflict, GraphicsFeature::rcas);
                plan.fsr.enabled = false;
                plan.original_present_update = true;
                plan.presentation = settings.upscale == GraphicsUpscale::linear ?
                    GraphicsPresentDirective::original_linear : GraphicsPresentDirective::original_native;
            }
            plan.original_filter = settings.upscale == GraphicsUpscale::linear ? SDL_GPU_FILTER_LINEAR : inputs.original_filter;
            if (!original_available(inputs, plan.original_filter))
                return fail(result, GraphicsApplyError::original_presenter_unavailable, GraphicsFeature::upscale);
        }
        if (selected(GraphicsFeature::rcas)) {
            plan.fsr.rcas_enabled = settings.rcas == GraphicsRcas::on;
            if (plan.fsr.rcas_enabled) {
                if (!plan.fsr.enabled)
                    return fail(result, GraphicsApplyError::rcas_requires_easu, GraphicsFeature::rcas);
                plan.fsr.sharpness_stops = 2.0f * (1.0f - settings.rcas_sharpness);
            }
        }
        if (plan.fsr.enabled && !plan.layout.fsr_eligible)
            return fail(result, GraphicsApplyError::producer_not_eligible, GraphicsFeature::upscale);
        if (plan.fsr.enabled && !original_available(inputs, inputs.original_filter))
            return fail(result, GraphicsApplyError::original_presenter_unavailable, GraphicsFeature::upscale);
        plan.fsr_update = true;
        plan.effective_easu_enabled = plan.fsr.enabled;
        plan.effective_rcas_enabled = plan.fsr.enabled && plan.fsr.rcas_enabled;
    }

    if (selected(GraphicsFeature::filter)) {
        if (inputs.display_baseline) plan.display = *inputs.display_baseline;
        else plan.display.filter = inputs.original_filter;
        plan.display.mode = settings.filter == GraphicsFilter::off ? DisplayEffectMode::off :
            settings.filter == GraphicsFilter::scanlines ? DisplayEffectMode::scanlines : DisplayEffectMode::crt;
        plan.display.scanline_strength = settings.scanline_strength * 0.25f;
        plan.display.mask_strength = settings.mask_strength * 0.15f;
        if (!unit(plan.display.mask_color[0]) || !unit(plan.display.mask_color[1]) ||
            !unit(plan.display.mask_color[2]) ||
            (plan.display.filter != SDL_GPU_FILTER_NEAREST && plan.display.filter != SDL_GPU_FILTER_LINEAR))
            return fail(result, GraphicsApplyError::invalid_baseline, GraphicsFeature::filter);
        if (plan.display.mode != DisplayEffectMode::off) {
            if (!inputs.logical_rows || inputs.logical_rows > 8192 ||
                source.encoding != PostColorEncoding::perceptual_sdr_unorm ||
                !numeric_sdr(source.format) || !numeric_sdr(inputs.post_target_format))
                return fail(result, GraphicsApplyError::producer_not_eligible, GraphicsFeature::filter);
            if (!original_available(inputs, inputs.original_filter))
                return fail(result, GraphicsApplyError::original_presenter_unavailable, GraphicsFeature::filter);
            plan.display.logical_rows = inputs.logical_rows;
        }
        plan.display_update = true;
    }

    if (selected(GraphicsFeature::movie_borders) || selected(GraphicsFeature::movie_cleanup)) {
        if (inputs.movie_baseline) plan.movie = *inputs.movie_baseline;
        const auto movie_feature = selected(GraphicsFeature::movie_borders) ?
            GraphicsFeature::movie_borders : GraphicsFeature::movie_cleanup;
        if (selected(GraphicsFeature::movie_cleanup)) {
            plan.movie.cleanup = settings.movie_cleanup == GraphicsMovieCleanup::off ? MovieCleanup::off :
                settings.movie_cleanup == GraphicsMovieCleanup::gentle ? MovieCleanup::gentle : MovieCleanup::strong;
        }
        if (selected(GraphicsFeature::movie_borders)) {
            plan.movie.blur_backdrop = settings.movie_borders == GraphicsMovieBorders::blur;
            plan.movie_clear_to_black = settings.movie_borders == GraphicsMovieBorders::black;
            if (plan.movie.blur_backdrop) {
                plan.movie.blur_radius_pixels = 0.25f + 15.75f * settings.movie_blur_strength;
                plan.movie.backdrop_brightness = 1.0f; // No hidden dimming/color change.
            }
        }
        const double pixel_aspect = inputs.layout.source_pixel_aspect;
        if (!(pixel_aspect >= 0.0625 && pixel_aspect <= 16.0) ||
            plan.layout.orientation_transform_required || !numeric_sdr(source.format) ||
            !numeric_sdr(inputs.post_target_format) ||
            std::uint64_t{source.crop.width} * source.crop.height > 16u * 1024u * 1024u)
            return fail(result, GraphicsApplyError::producer_not_eligible, movie_feature);
        plan.movie.pixel_aspect = static_cast<float>(pixel_aspect);
        if ((plan.movie.cleanup != MovieCleanup::off && plan.movie.cleanup != MovieCleanup::gentle &&
             plan.movie.cleanup != MovieCleanup::strong) ||
            (plan.movie.blur_backdrop &&
             (!(plan.movie.blur_radius_pixels >= 0.25f && plan.movie.blur_radius_pixels <= 16) ||
              !unit(plan.movie.backdrop_brightness))))
            return fail(result, GraphicsApplyError::invalid_baseline, movie_feature);
        if ((plan.movie.cleanup != MovieCleanup::off || plan.movie.blur_backdrop) &&
            source.encoding != PostColorEncoding::perceptual_sdr_unorm)
            return fail(result, GraphicsApplyError::producer_not_eligible, movie_feature);
        if (plan.movie.cleanup != MovieCleanup::off && !source.opaque)
            return fail(result, GraphicsApplyError::producer_not_eligible, GraphicsFeature::movie_cleanup);
        if (plan.movie.cleanup == MovieCleanup::off && !plan.movie.blur_backdrop &&
            ((source.encoding == PostColorEncoding::unknown && source.format != inputs.post_target_format) ||
             (source.encoding == PostColorEncoding::linear_hdr &&
              (source.format != SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT || source.format != inputs.post_target_format))))
            return fail(result, GraphicsApplyError::producer_not_eligible, movie_feature);
        if (!original_available(inputs, inputs.original_filter))
            return fail(result, GraphicsApplyError::original_presenter_unavailable, movie_feature);
        plan.movie_update = true;
    }

    if (selected(GraphicsFeature::sampling)) {
        if (!inputs.sampling_baseline)
            return fail(result, GraphicsApplyError::missing_sampling_baseline, GraphicsFeature::sampling);
        plan.sampling = *inputs.sampling_baseline; // Preserve actual U/V addressing.
        const bool nearest = settings.sampling == GraphicsSampling::nearest;
        plan.sampling.min_filter = plan.sampling.mag_filter = nearest ? GpuTextureFilter::nearest : GpuTextureFilter::linear;
        plan.sampling.mip_filter = nearest ? GpuTextureMipFilter::nearest : GpuTextureMipFilter::linear;
        plan.sampling.anisotropy = settings.sampling == GraphicsSampling::anisotropic ? settings.anisotropy : 1;
        plan.sampling.anisotropy_feature_confirmed = inputs.sampler_anisotropy_confirmed;
        if (!address(plan.sampling.u) || !address(plan.sampling.v))
            return fail(result, GraphicsApplyError::invalid_baseline, GraphicsFeature::sampling);
        if (plan.sampling.anisotropy > 1 && !inputs.sampler_anisotropy_confirmed)
            return fail(result, GraphicsApplyError::anisotropy_unconfirmed, GraphicsFeature::sampling);
        plan.sampling_update = true;
    }
    result.plan = plan;
    return result;
}
} // namespace cedar
