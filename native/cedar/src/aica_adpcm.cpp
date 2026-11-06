// Modified standalone adaptation from vgmstream/vgmstream yamaha_decoder.c,
// pin6802153425adde134e49394fc653455166d7d2f2. Complete permissive notices:
// third_party/licenses/vgmstream-yamaha-ISC.txt. No Flycast code branch copied.
#include "cedar/aica_adpcm.hpp"
#include <limits>

namespace cedar {
namespace {
bool profile(VgmstreamAicaProfile value) noexcept {
    return value == VgmstreamAicaProfile::filtered_254_256;
}
bool valid(const VgmstreamAicaState& state) noexcept {
    return state.history >= -32768 && state.history <= 32767 && state.step >= 127 && state.step <= 24576;
}
bool overlap(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!an || !bn) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < an : x - y < bn;
}
bool bounded(const void* data, std::size_t bytes) noexcept {
    return bytes <= std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(data);
}
std::int32_t clamp(std::int32_t value, std::int32_t low, std::int32_t high) noexcept {
    return value < low ? low : value > high ? high : value;
}
std::int16_t expand(std::uint8_t code, VgmstreamAicaState& state) noexcept {
    static constexpr std::int32_t factors[8] = {230,230,230,230,307,409,512,614};
    // State bounds prove all products fit int32. Division matches the source:
    // signed feedback attenuation truncates toward zero, never negative shift.
    const auto attenuated = state.history * 254 / 256;
    auto delta = ((static_cast<std::int32_t>(code & 7u) * 2 + 1) * state.step) / 8;
    if (delta > 32767) delta = 32767;
    if (code & 8u) delta = -delta;
    const auto sample = clamp(attenuated + delta, -32768, 32767);
    state.step = clamp(state.step * factors[code & 7u] / 256, 127, 24576);
    state.history = sample;
    return static_cast<std::int16_t>(sample);
}
}

VgmstreamAicaError restore_vgmstream_aica(VgmstreamAicaState& state, VgmstreamAicaProfile selected,
                                          std::int32_t history, std::int32_t step) noexcept {
    if (!profile(selected)) return VgmstreamAicaError::unsupported_profile;
    const VgmstreamAicaState candidate{selected, history, step};
    if (!valid(candidate)) return VgmstreamAicaError::invalid_state;
    state = candidate;
    return VgmstreamAicaError::none;
}
VgmstreamAicaError reset_vgmstream_aica(VgmstreamAicaState& state, VgmstreamAicaProfile selected) noexcept {
    return restore_vgmstream_aica(state, selected, 0, 127);
}
VgmstreamAicaResult decode_vgmstream_aica(VgmstreamAicaState& state,
    const std::uint8_t* encoded, std::size_t encoded_bytes, std::uint8_t phase,
    std::size_t samples, VgmstreamAicaNibbleOrder order,
    std::int16_t* output, std::size_t capacity) noexcept {
    if (!profile(state.profile)) return {VgmstreamAicaError::unsupported_profile};
    if (!valid(state)) return {VgmstreamAicaError::invalid_state};
    if (order != VgmstreamAicaNibbleOrder::low_then_high && order != VgmstreamAicaNibbleOrder::high_then_low)
        return {VgmstreamAicaError::invalid_order};
    if (phase > 1) return {VgmstreamAicaError::invalid_phase};
    if (samples > vgmstream_aica_max_samples || encoded_bytes > vgmstream_aica_max_input)
        return {VgmstreamAicaError::size_limit};
    const auto required = samples ? (samples + phase + 1) / 2 : 0;
    if (required > encoded_bytes) return {VgmstreamAicaError::insufficient_input};
    if (samples > capacity) return {VgmstreamAicaError::insufficient_output};
    const auto output_bytes = samples * sizeof(std::int16_t); // Explicit sample bound prevents overflow.
    if ((!encoded && encoded_bytes) || (!output && samples) ||
        !bounded(encoded, encoded_bytes) || !bounded(output, output_bytes) ||
        (samples && reinterpret_cast<std::uintptr_t>(output) % alignof(std::int16_t)))
        return {VgmstreamAicaError::invalid_buffer};
    if (overlap(encoded, encoded_bytes, output, output_bytes) ||
        overlap(encoded, encoded_bytes, &state, sizeof(state)) ||
        overlap(output, output_bytes, &state, sizeof(state)))
        return {VgmstreamAicaError::overlapping_storage};
    auto candidate = state;
    for (std::size_t i = 0; i < samples; ++i) {
        const auto position = i + phase;
        const bool second = (position & 1u) != 0;
        const auto shift = order == VgmstreamAicaNibbleOrder::low_then_high ? (second ? 4u : 0u) : (second ? 0u : 4u);
        const auto code = static_cast<std::uint8_t>((encoded[position / 2] >> shift) & 0x0fu);
        output[i] = expand(code, candidate);
    }
    state = candidate;
    return {VgmstreamAicaError::none, samples, required, (samples + phase) / 2,
             static_cast<std::uint8_t>((samples + phase) & 1u)};
}
} // namespace cedar
