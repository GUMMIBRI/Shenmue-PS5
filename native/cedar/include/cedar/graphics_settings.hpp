#pragma once

#include <cstddef>
#include <cstdint>

namespace cedar {

enum class GraphicsOverride : std::uint8_t { preserve_engine, native };
enum class GraphicsUpscale : std::uint8_t { preserve_engine, native, linear, fsr1 };
enum class GraphicsRcas : std::uint8_t { preserve_engine, off, on };
enum class GraphicsFilter : std::uint8_t { preserve_engine, off, scanlines, crt };
enum class GraphicsSampling : std::uint8_t { preserve_engine, nearest, linear, anisotropic };
enum class GraphicsAspect : std::uint8_t { preserve_engine, four_three, sixteen_nine, fit };
enum class GraphicsMovieBorders : std::uint8_t { preserve_engine, black, blur };
enum class GraphicsMovieCleanup : std::uint8_t { preserve_engine, off, gentle, strong };

// Every control preserves the engine until explicitly selected. Parameter values
// are latent tuning only; they do not enable an effect or override an engine.
struct GraphicsSettings {
    GraphicsOverride render_scale{GraphicsOverride::preserve_engine};
    std::uint16_t render_scale_percent{100};
    GraphicsUpscale upscale{GraphicsUpscale::preserve_engine};
    GraphicsRcas rcas{GraphicsRcas::preserve_engine};
    float rcas_sharpness{0.5f}; // Latent: AMD stops1, matching PE1; effect remains preserved/off.
    GraphicsFilter filter{GraphicsFilter::preserve_engine};
    GraphicsOverride gamma{GraphicsOverride::preserve_engine};
    float gamma_value{1.0f};
    GraphicsOverride saturation{GraphicsOverride::preserve_engine};
    float saturation_value{1.0f};
    float scanline_strength{0.32f}, mask_strength{0.36666667f}; // Latent native strengths .08/.055.
    GraphicsSampling sampling{GraphicsSampling::preserve_engine};
    std::uint8_t anisotropy{1};
    GraphicsAspect aspect{GraphicsAspect::preserve_engine};
    GraphicsMovieBorders movie_borders{GraphicsMovieBorders::preserve_engine};
    GraphicsMovieCleanup movie_cleanup{GraphicsMovieCleanup::preserve_engine};
    float movie_blur_strength{0.11111111f}; // Latent2px radius; backdrop remains preserved/off.
};

enum class GraphicsFeature : std::uint8_t {
    render_scale, upscale, rcas, filter, gamma, saturation, sampling,
    aspect, movie_borders, movie_cleanup, count
};
enum class GraphicsSettingsError : std::uint8_t {
    none, invalid_buffer, truncated, invalid_magic, unsupported_version,
    invalid_length, unknown_bits, checksum_mismatch, invalid_enum, nonfinite,
    out_of_range, unavailable
};
struct GraphicsSettingsStatus {
    GraphicsSettingsError error{GraphicsSettingsError::none};
    GraphicsFeature feature{GraphicsFeature::count}; // count means record/general
    std::size_t wire_bytes{}; // successful encode/decode only
    explicit operator bool() const noexcept { return error == GraphicsSettingsError::none; }
};

constexpr std::uint16_t graphics_settings_version = 1;
constexpr std::size_t graphics_settings_wire_bytes = 52;

GraphicsSettingsStatus validate_graphics_settings(const GraphicsSettings& settings) noexcept;
// On failure no output bytes are written. Output must not overlap settings.
GraphicsSettingsStatus encode_graphics_settings(const GraphicsSettings& settings,
                                                 void* output, std::size_t capacity) noexcept;
// Requires one exact complete v1 record. Every failure replaces the destination
// with full preserve-engine defaults, never a partially decoded configuration.
GraphicsSettingsStatus decode_graphics_settings(const void* input, std::size_t length,
                                                 GraphicsSettings& settings) noexcept;

enum class GraphicsEngineFeature : std::uint8_t { unknown, not_present, existing_equal_or_better };
constexpr std::uint32_t graphics_mode_bit(unsigned mode) noexcept {
    return mode < 32 ? (std::uint32_t{1} << mode) : 0;
}
struct GraphicsFeatureCapability {
    // Bit N means the caller has an actual native implementation of enum value
    // N for this feature. Numeric override features use GraphicsOverride::native.
    // Bit0/preserve requires no implementation and must not be claimed here.
    std::uint32_t supported_modes{};
    GraphicsEngineFeature engine{GraphicsEngineFeature::unknown};
    // True only when the caller has explicitly secured exclusive native control
    // (engine effect bypassed/disabled), not merely because a mode was selected.
    bool exclusive_native_ownership{};
};
struct GraphicsCapabilities {
    GraphicsFeatureCapability features[static_cast<std::size_t>(GraphicsFeature::count)]{};
    std::uint8_t maximum_anisotropy{1};
};
enum class GraphicsDecision : std::uint8_t {
    preserve_engine, native, unavailable, engine_ownership_required, engine_capability_unknown
};
struct GraphicsResolution {
    GraphicsDecision decisions[static_cast<std::size_t>(GraphicsFeature::count)]{};
    std::uint32_t requested_mask{}, blocked_mask{};
    // All-or-nothing plan: zero on ANY unavailable/error result. No effects are
    // applied by this data facility, including when this mask is nonzero.
    std::uint32_t native_mask{};
};

// Pure capability/ownership resolution, not discovery or shader support. Unknown
// engine state and existing equal/better effects block native duplication unless
// exclusive ownership is confirmed. Unimplemented modes return unavailable.
// On unavailable, decisions/requested/blocked remain useful; native_mask is zero.
GraphicsSettingsStatus resolve_graphics_settings(const GraphicsSettings& settings,
                                                  const GraphicsCapabilities& capabilities,
                                                  GraphicsResolution& result) noexcept;

} // namespace cedar
