#pragma once
// Modified standalone adaptation of permissively licensed vgmstream AICA math.
// Only filtered254/256 profile. Exact pin/notices: docs/AICA-ADPCM.md.
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class VgmstreamAicaProfile : std::uint8_t { unspecified, filtered_254_256 };
enum class VgmstreamAicaNibbleOrder : std::uint8_t { low_then_high, high_then_low };
struct VgmstreamAicaState {
    VgmstreamAicaProfile profile{VgmstreamAicaProfile::unspecified};
    std::int32_t history{}, step{}; // One independent state per encoded channel.
};
enum class VgmstreamAicaError : std::uint8_t {
    none, unsupported_profile, invalid_state, invalid_order, invalid_phase,
    invalid_buffer, insufficient_input, insufficient_output, overlapping_storage,
    size_limit
};
struct VgmstreamAicaResult {
    VgmstreamAicaError error{VgmstreamAicaError::none};
    std::size_t samples_written{}, bytes_read{}, bytes_advanced{};
    // Logical first/second nibble under the explicitly selected order. If1,
    // retain the byte at bytes_advanced for the next call; do NOT discard it.
    std::uint8_t next_nibble_phase{};
    explicit operator bool() const noexcept { return error == VgmstreamAicaError::none; }
};
constexpr std::size_t vgmstream_aica_max_samples = 1024u * 1024u;
constexpr std::size_t vgmstream_aica_max_input = 64u * 1024u * 1024u;

// Profile must be explicitly selected. Reset chooses0/127 by explicit caller
// request, not by parsing an unknown voice/container. Restore accepts only valid
// PCM16 predictor / step127..24576. Any failure leaves state unchanged.
VgmstreamAicaError reset_vgmstream_aica(VgmstreamAicaState&, VgmstreamAicaProfile) noexcept;
VgmstreamAicaError restore_vgmstream_aica(VgmstreamAicaState&, VgmstreamAicaProfile,
                                         std::int32_t history, std::int32_t step) noexcept;

// Contiguous mono-channel PCM16. Exactly sample_count encoded nibbles; phase0/1
// selects first/second nibble of encoded[0] under order. No hidden pending byte.
// Whole-call validation precedes any output or state writes; failure writes
// nothing. Input/state/output must not overlap. Input stays immutable through
// return; caller advances pointer by bytes_advanced and carries next phase.
// Replacing a chunk after odd consumption must retain its unconsumed nibble.
// No STR layout, interleave, rate, resampling, loop or playback inference.
VgmstreamAicaResult decode_vgmstream_aica(VgmstreamAicaState& state,
    const std::uint8_t* encoded, std::size_t encoded_bytes, std::uint8_t first_nibble_phase,
    std::size_t sample_count, VgmstreamAicaNibbleOrder order,
    std::int16_t* output, std::size_t output_capacity_samples) noexcept;
} // namespace cedar
