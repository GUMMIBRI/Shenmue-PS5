// Original sparse native configuration translation, not GPU effect execution.
#pragma once
#include "cedar/graphics_settings.hpp"
#include "cedar/render_layout.hpp"
#include "cedar/fsr1.hpp"
#include "cedar/display_effects.hpp"
#include "cedar/movie_effects.hpp"
#include "cedar/gpu_texture.hpp"

namespace cedar {
enum class GraphicsPresentDirective : std::uint8_t { preserve_engine, original_native, original_linear, fsr1 };
enum class GraphicsApplyError : std::uint8_t {
    none, resolution_failure, unimplemented_feature, layout_failure, invalid_baseline,
    producer_not_eligible, original_presenter_unavailable, rcas_requires_easu,
    preserve_rcas_conflict, missing_sampling_baseline, anisotropy_unconfirmed
};
struct GraphicsApplyInputs {
    RenderLayoutRequest layout{}; // Actual producer metadata/baseline, never inferred.
    std::uint32_t logical_rows{};
    SDL_GPUTextureFormat post_target_format{SDL_GPU_TEXTUREFORMAT_INVALID};
    // Null module baselines mean a fresh disabled CEDAR module, not an unknown
    // engine configuration. A live configured module requires its actual snapshot.
    const Fsr1Options* fsr_baseline{};
    const DisplayEffectSettings* display_baseline{};
    const MovieEffectsConfig* movie_baseline{};
    // Required for any sampling override: address modes cannot be guessed.
    const GpuTextureSampling* sampling_baseline{};
    bool sampler_anisotropy_confirmed{}; // Actual created-device feature, not selection.
    bool original_presenter_available{}, original_nearest_supported{}, original_linear_supported{};
    SDL_GPUFilter original_filter{SDL_GPU_FILTER_NEAREST}; // Actual original path's filter.
};
struct GraphicsApplyPlan {
    std::uint32_t native_mask{}, inactive_content_mask{};
    bool fsr_update{}, display_update{}, movie_update{}, sampling_update{}, layout_update{};
    bool original_present_update{}, movie_clear_to_black{}, bypass_post{};
    GraphicsPresentDirective presentation{GraphicsPresentDirective::preserve_engine};
    SDL_GPUFilter original_filter{SDL_GPU_FILTER_NEAREST};
    Fsr1Options fsr{false, false, 2.0f}; // New EASU/RCAS both disabled.
    DisplayEffectSettings display{}; // Off.
    MovieEffectsConfig movie{}; // Cleanup/backdrop Off; neutral brightness.
    GpuTextureSampling sampling{}; // Meaningful only when sampling_update.
    RenderLayoutRequest layout_request{};
    RenderLayoutResult layout{};
    // Describe the translated FSR configuration only when fsr_update=true;
    // never claim a global engine state or that recording/execution happened.
    bool effective_easu_enabled{}, effective_rcas_enabled{};
};
struct GraphicsApplyResult {
    GraphicsApplyError error{GraphicsApplyError::none};
    GraphicsFeature feature{GraphicsFeature::count};
    GraphicsSettingsStatus settings_status{};
    GraphicsResolution resolution{};
    bool layout_attempted{}; // No-op/sampling-only/content bypass need no image layout.
    RenderLayoutResult layout_diagnostic{};
    GraphicsApplyPlan plan{}; // Entirely default/zero-update on ANY failure.
    explicit operator bool() const noexcept { return error == GraphicsApplyError::none; }
};

// Always validates/resolves real capabilities and engine ownership first. Gamma
// and saturation native requests are unimplemented, even if a capability lies.
// Current explicit content scopes updates; inactive_content_mask explains valid
// native requests for a different producer type. Native UI produces no updates.
GraphicsApplyResult plan_graphics_apply(const GraphicsSettings& settings,
    const GraphicsCapabilities& capabilities, const GraphicsApplyInputs& inputs) noexcept;
} // namespace cedar
