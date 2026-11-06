#include "cedar/graphics_settings.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace cedar {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Graphics wire format requires IEEE754 binary32");
constexpr std::uint32_t control_mask = 7;
constexpr std::size_t feature_count = static_cast<std::size_t>(GraphicsFeature::count);
GraphicsSettingsStatus fail(GraphicsSettingsError error, GraphicsFeature feature = GraphicsFeature::count) noexcept {
    return {error, feature, 0};
}
bool overlaps(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return an && bn && (x <= y ? y - x < an : x - y < bn);
}
std::uint16_t read16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8u));
}
std::uint32_t read32(const std::uint8_t* p) noexcept {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8u) |
           (std::uint32_t{p[2]} << 16u) | (std::uint32_t{p[3]} << 24u);
}
void write16(std::uint8_t* p, std::uint16_t value) noexcept {
    p[0] = static_cast<std::uint8_t>(value); p[1] = static_cast<std::uint8_t>(value >> 8u);
}
void write32(std::uint8_t* p, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(value >> (8u * i));
}
float read_float(const std::uint8_t* p) noexcept {
    const auto bits = read32(p);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void write_float(std::uint8_t* p, float value) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    write32(p, bits);
}
std::uint32_t checksum(const std::uint8_t* bytes) noexcept {
    // CRC32/IEEE over the entire v1 record, treating its CRC field as zero.
    // Corruption detection only; no authentication or original-save semantics.
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < graphics_settings_wire_bytes; ++i) {
        crc ^= i >= 12 && i < 16 ? 0u : bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
template<class Enum> bool valid_enum(Enum value, unsigned maximum) noexcept {
    return static_cast<unsigned>(value) <= maximum;
}
GraphicsSettingsStatus scalar(float value, float low, float high, GraphicsFeature feature) noexcept {
    if (!std::isfinite(value)) return fail(GraphicsSettingsError::nonfinite, feature);
    if (value < low || value > high) return fail(GraphicsSettingsError::out_of_range, feature);
    return {};
}
GraphicsSettingsStatus decode_failure(GraphicsSettingsStatus status, GraphicsSettings& output) noexcept {
    output = {};
    return status;
}
unsigned requested_mode(const GraphicsSettings& settings, GraphicsFeature feature) noexcept {
    switch (feature) {
    case GraphicsFeature::render_scale: return static_cast<unsigned>(settings.render_scale);
    case GraphicsFeature::upscale: return static_cast<unsigned>(settings.upscale);
    case GraphicsFeature::rcas: return static_cast<unsigned>(settings.rcas);
    case GraphicsFeature::filter: return static_cast<unsigned>(settings.filter);
    case GraphicsFeature::gamma: return static_cast<unsigned>(settings.gamma);
    case GraphicsFeature::saturation: return static_cast<unsigned>(settings.saturation);
    case GraphicsFeature::sampling: return static_cast<unsigned>(settings.sampling);
    case GraphicsFeature::aspect: return static_cast<unsigned>(settings.aspect);
    case GraphicsFeature::movie_borders: return static_cast<unsigned>(settings.movie_borders);
    case GraphicsFeature::movie_cleanup: return static_cast<unsigned>(settings.movie_cleanup);
    case GraphicsFeature::count: return 0;
    }
    return 0;
}
std::uint32_t allowed_modes(GraphicsFeature feature) noexcept {
    switch (feature) {
    case GraphicsFeature::render_scale:
    case GraphicsFeature::gamma:
    case GraphicsFeature::saturation: return 0x02u;
    case GraphicsFeature::rcas:
    case GraphicsFeature::movie_borders: return 0x06u;
    default: return 0x0eu;
    }
}
}

GraphicsSettingsStatus validate_graphics_settings(const GraphicsSettings& settings) noexcept {
    if (!valid_enum(settings.render_scale, 1)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::render_scale);
    if (!valid_enum(settings.upscale, 3)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::upscale);
    if (!valid_enum(settings.rcas, 2)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::rcas);
    if (!valid_enum(settings.filter, 3)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::filter);
    if (!valid_enum(settings.gamma, 1)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::gamma);
    if (!valid_enum(settings.saturation, 1)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::saturation);
    if (!valid_enum(settings.sampling, 3)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::sampling);
    if (!valid_enum(settings.aspect, 3)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::aspect);
    if (!valid_enum(settings.movie_borders, 2)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::movie_borders);
    if (!valid_enum(settings.movie_cleanup, 3)) return fail(GraphicsSettingsError::invalid_enum, GraphicsFeature::movie_cleanup);
    if (settings.render_scale_percent < 50 || settings.render_scale_percent > 200)
        return fail(GraphicsSettingsError::out_of_range, GraphicsFeature::render_scale);
    if (settings.anisotropy < 1 || settings.anisotropy > 16)
        return fail(GraphicsSettingsError::out_of_range, GraphicsFeature::sampling);
    auto status = scalar(settings.rcas_sharpness, 0, 1, GraphicsFeature::rcas);
    if (!status) return status;
    status = scalar(settings.gamma_value, 0.25f, 4, GraphicsFeature::gamma);
    if (!status) return status;
    status = scalar(settings.saturation_value, 0, 2, GraphicsFeature::saturation);
    if (!status) return status;
    status = scalar(settings.scanline_strength, 0, 1, GraphicsFeature::filter);
    if (!status) return status;
    status = scalar(settings.mask_strength, 0, 1, GraphicsFeature::filter);
    if (!status) return status;
    return scalar(settings.movie_blur_strength, 0, 1, GraphicsFeature::movie_borders);
}

GraphicsSettingsStatus encode_graphics_settings(const GraphicsSettings& settings, void* output,
                                                 std::size_t capacity) noexcept {
    const auto valid = validate_graphics_settings(settings);
    if (!valid) return valid;
    if (!output) return fail(GraphicsSettingsError::invalid_buffer);
    if (capacity < graphics_settings_wire_bytes) return fail(GraphicsSettingsError::truncated);
    if (overlaps(output, graphics_settings_wire_bytes, &settings, sizeof(settings)))
        return fail(GraphicsSettingsError::invalid_buffer);
    auto* bytes = static_cast<std::uint8_t*>(output);
    std::memset(bytes, 0, graphics_settings_wire_bytes);
    bytes[0] = 'C'; bytes[1] = 'G'; bytes[2] = 'F'; bytes[3] = 'X';
    write16(bytes + 4, graphics_settings_version);
    write16(bytes + 6, static_cast<std::uint16_t>(graphics_settings_wire_bytes));
    const auto flags = (settings.render_scale == GraphicsOverride::native ? 1u : 0u) |
                       (settings.gamma == GraphicsOverride::native ? 2u : 0u) |
                       (settings.saturation == GraphicsOverride::native ? 4u : 0u);
    write32(bytes + 8, flags);
    write16(bytes + 16, settings.render_scale_percent);
    bytes[18] = static_cast<std::uint8_t>(settings.upscale);
    bytes[19] = static_cast<std::uint8_t>(settings.rcas);
    bytes[20] = static_cast<std::uint8_t>(settings.filter);
    bytes[21] = static_cast<std::uint8_t>(settings.sampling);
    bytes[22] = settings.anisotropy;
    bytes[23] = static_cast<std::uint8_t>(settings.aspect);
    bytes[24] = static_cast<std::uint8_t>(settings.movie_borders);
    bytes[25] = static_cast<std::uint8_t>(settings.movie_cleanup);
    write_float(bytes + 28, settings.rcas_sharpness);
    write_float(bytes + 32, settings.gamma_value);
    write_float(bytes + 36, settings.saturation_value);
    write_float(bytes + 40, settings.scanline_strength);
    write_float(bytes + 44, settings.mask_strength);
    write_float(bytes + 48, settings.movie_blur_strength);
    write32(bytes + 12, checksum(bytes));
    return {GraphicsSettingsError::none, GraphicsFeature::count, graphics_settings_wire_bytes};
}

GraphicsSettingsStatus decode_graphics_settings(const void* input, std::size_t length,
                                                 GraphicsSettings& settings) noexcept {
    if (!input) return decode_failure(fail(GraphicsSettingsError::invalid_buffer), settings);
    if (length < 16) return decode_failure(fail(GraphicsSettingsError::truncated), settings);
    const auto* bytes = static_cast<const std::uint8_t*>(input);
    if (bytes[0] != 'C' || bytes[1] != 'G' || bytes[2] != 'F' || bytes[3] != 'X')
        return decode_failure(fail(GraphicsSettingsError::invalid_magic), settings);
    if (read16(bytes + 4) != graphics_settings_version)
        return decode_failure(fail(GraphicsSettingsError::unsupported_version), settings);
    if (read16(bytes + 6) != graphics_settings_wire_bytes)
        return decode_failure(fail(GraphicsSettingsError::invalid_length), settings);
    if (length < graphics_settings_wire_bytes)
        return decode_failure(fail(GraphicsSettingsError::truncated), settings);
    if (length != graphics_settings_wire_bytes)
        return decode_failure(fail(GraphicsSettingsError::invalid_length), settings);
    const auto flags = read32(bytes + 8);
    if ((flags & ~control_mask) || read16(bytes + 26))
        return decode_failure(fail(GraphicsSettingsError::unknown_bits), settings);
    if (read32(bytes + 12) != checksum(bytes))
        return decode_failure(fail(GraphicsSettingsError::checksum_mismatch), settings);
    GraphicsSettings candidate;
    candidate.render_scale = flags & 1u ? GraphicsOverride::native : GraphicsOverride::preserve_engine;
    candidate.gamma = flags & 2u ? GraphicsOverride::native : GraphicsOverride::preserve_engine;
    candidate.saturation = flags & 4u ? GraphicsOverride::native : GraphicsOverride::preserve_engine;
    candidate.render_scale_percent = read16(bytes + 16);
    candidate.upscale = static_cast<GraphicsUpscale>(bytes[18]);
    candidate.rcas = static_cast<GraphicsRcas>(bytes[19]);
    candidate.filter = static_cast<GraphicsFilter>(bytes[20]);
    candidate.sampling = static_cast<GraphicsSampling>(bytes[21]);
    candidate.anisotropy = bytes[22];
    candidate.aspect = static_cast<GraphicsAspect>(bytes[23]);
    candidate.movie_borders = static_cast<GraphicsMovieBorders>(bytes[24]);
    candidate.movie_cleanup = static_cast<GraphicsMovieCleanup>(bytes[25]);
    candidate.rcas_sharpness = read_float(bytes + 28);
    candidate.gamma_value = read_float(bytes + 32);
    candidate.saturation_value = read_float(bytes + 36);
    candidate.scanline_strength = read_float(bytes + 40);
    candidate.mask_strength = read_float(bytes + 44);
    candidate.movie_blur_strength = read_float(bytes + 48);
    const auto valid = validate_graphics_settings(candidate);
    if (!valid) return decode_failure(valid, settings);
    settings = candidate;
    return {GraphicsSettingsError::none, GraphicsFeature::count, graphics_settings_wire_bytes};
}

GraphicsSettingsStatus resolve_graphics_settings(const GraphicsSettings& settings,
    const GraphicsCapabilities& capabilities, GraphicsResolution& result) noexcept {
    result = {};
    const auto valid = validate_graphics_settings(settings);
    if (!valid) return valid;
    if (capabilities.maximum_anisotropy < 1 || capabilities.maximum_anisotropy > 16)
        return fail(GraphicsSettingsError::out_of_range, GraphicsFeature::sampling);
    for (std::size_t i = 0; i < feature_count; ++i) {
        const auto feature = static_cast<GraphicsFeature>(i);
        const auto& cap = capabilities.features[i];
        if (!valid_enum(cap.engine, 2)) return fail(GraphicsSettingsError::invalid_enum, feature);
        if (cap.supported_modes & ~allowed_modes(feature)) return fail(GraphicsSettingsError::unknown_bits, feature);
    }
    std::uint32_t native_candidates = 0;
    GraphicsFeature first_blocked = GraphicsFeature::count;
    for (std::size_t i = 0; i < feature_count; ++i) {
        const auto feature = static_cast<GraphicsFeature>(i);
        const auto mode = requested_mode(settings, feature);
        if (!mode) continue;
        const auto bit = std::uint32_t{1} << i;
        result.requested_mask |= bit;
        const auto& cap = capabilities.features[i];
        auto decision = GraphicsDecision::native;
        if (!(cap.supported_modes & graphics_mode_bit(mode)) ||
            (feature == GraphicsFeature::sampling && settings.sampling == GraphicsSampling::anisotropic &&
             settings.anisotropy > capabilities.maximum_anisotropy))
            decision = GraphicsDecision::unavailable;
        else if (!cap.exclusive_native_ownership && cap.engine == GraphicsEngineFeature::unknown)
            decision = GraphicsDecision::engine_capability_unknown;
        else if (!cap.exclusive_native_ownership && cap.engine == GraphicsEngineFeature::existing_equal_or_better)
            decision = GraphicsDecision::engine_ownership_required;
        result.decisions[i] = decision;
        if (decision == GraphicsDecision::native) native_candidates |= bit;
        else {
            result.blocked_mask |= bit;
            if (first_blocked == GraphicsFeature::count) first_blocked = feature;
        }
    }
    if (result.blocked_mask) return fail(GraphicsSettingsError::unavailable, first_blocked);
    result.native_mask = native_candidates;
    return {};
}

} // namespace cedar
