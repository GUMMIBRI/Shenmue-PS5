// SPDX-License-Identifier: ISC
// Container/dispatch/seed provenance: docs/SPSD-SCOPE.md and SPSD-AICA.md.
// Complete vgmstream COPYING retained in third_party/licenses/vgmstream-yamaha-ISC.txt.
#include "cedar/spsd_aica.hpp"
#include <limits>

namespace cedar {
namespace {
struct Span { std::uintptr_t begin{}, end{}; };
bool span(const void* pointer, std::size_t size, Span& result) noexcept {
    if (!pointer && size) return false;
    result.begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (size > std::numeric_limits<std::uintptr_t>::max() - result.begin) return false;
    result.end = result.begin + size;
    return true;
}
bool overlap(const Span& a, const Span& b) noexcept {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
struct Chunk {
    SpsdSlice channel[2]{};
    std::size_t frames{}, encoded_bytes{};
    std::uint8_t phase{};
};
bool plan_chunk(const SpsdView& view, std::uint64_t position, std::size_t remaining,
                std::size_t scratch_capacity, Chunk& chunk, SpsdAicaResult& diagnostic) noexcept {
    chunk = {};
    chunk.frames = remaining < scratch_capacity ? remaining : scratch_capacity;
    if (chunk.frames > vgmstream_aica_max_samples) chunk.frames = vgmstream_aica_max_samples;
    chunk.phase = static_cast<std::uint8_t>(position & 1u);
    const auto byte_offset = static_cast<std::size_t>(position / 2);
    for (std::uint32_t ch = 0; ch < view.metadata().channels; ++ch) {
        SpsdSlice probe{};
        diagnostic.source_attempted = true;
        diagnostic.source_status = view.channel_slice(ch, byte_offset, 0, probe);
        if (!diagnostic.source_status) {
            diagnostic.error = SpsdAicaError::source_failure;
            diagnostic.error_channel = ch;
            diagnostic.error_frame = position;
            return false;
        }
        if (!diagnostic.source_status.contiguous_bytes) {
            diagnostic.error = SpsdAicaError::source_contract_failure;
            diagnostic.error_channel = ch;
            diagnostic.error_frame = position;
            return false;
        }
        const auto samples = std::uint64_t{diagnostic.source_status.contiguous_bytes} * 2 - chunk.phase;
        if (samples < chunk.frames) chunk.frames = static_cast<std::size_t>(samples);
    }
    if (!chunk.frames) { diagnostic.error = SpsdAicaError::source_contract_failure; return false; }
    chunk.encoded_bytes = (chunk.frames + chunk.phase + 1) / 2;
    for (std::uint32_t ch = 0; ch < view.metadata().channels; ++ch) {
        diagnostic.source_status = view.channel_slice(ch, byte_offset, chunk.encoded_bytes, chunk.channel[ch]);
        if (!diagnostic.source_status) {
            diagnostic.error = SpsdAicaError::source_failure;
            diagnostic.error_channel = ch;
            diagnostic.error_frame = position;
            return false;
        }
    }
    return true;
}
} // namespace

void SpsdAicaStream::stop() noexcept {
    view_ = {};
    states_[0] = states_[1] = {};
    frame_ = total_frames_ = 0;
    active_ = false;
}
SpsdAicaResult SpsdAicaStream::start(const SpsdView& source) noexcept {
    const auto candidate = source;
    stop();
    SpsdAicaResult result{};
    const auto& metadata = candidate.metadata();
    if (!candidate.parsed() || metadata.codec != SpsdCodec::aica_int || metadata.raw_codec != 3 ||
        metadata.aica_profile != SpsdAicaProfile::vgmstream_filtered_low_first_per_channel ||
        !metadata.sample_rate || metadata.channels < 1 || metadata.channels > 2) {
        result.error = SpsdAicaError::unsupported_source;
        return result;
    }
    Span input{}, owner{};
    if (!span(candidate.source_data(), candidate.source_size(), input) || !span(this, sizeof(*this), owner)) {
        result.error = SpsdAicaError::invalid_buffer;
        return result;
    }
    if (overlap(input, owner)) { result.error = SpsdAicaError::overlapping_storage; return result; }
    VgmstreamAicaState initialized[2]{};
    for (std::uint32_t ch = 0; ch < metadata.channels; ++ch) {
        result.codec_status.error = reset_vgmstream_aica(initialized[ch], VgmstreamAicaProfile::filtered_254_256);
        if (result.codec_status.error != VgmstreamAicaError::none) {
            result.error = SpsdAicaError::codec_failure;
            result.error_channel = ch;
            return result;
        }
    }
    view_ = candidate;
    states_[0] = initialized[0]; states_[1] = initialized[1];
    total_frames_ = std::uint64_t{metadata.bytes_per_channel} * 2;
    active_ = true;
    result.frames_remaining = total_frames_;
    return result;
}
SpsdAicaResult SpsdAicaStream::rewind() noexcept {
    SpsdAicaResult result{};
    result.frames_remaining = frames_remaining();
    if (!active_) { result.error = SpsdAicaError::not_started; return result; }
    VgmstreamAicaState initialized[2]{};
    for (std::uint32_t ch = 0; ch < channels(); ++ch) {
        result.codec_status.error = reset_vgmstream_aica(initialized[ch], VgmstreamAicaProfile::filtered_254_256);
        if (result.codec_status.error != VgmstreamAicaError::none) {
            result.error = SpsdAicaError::codec_failure;
            result.error_channel = ch;
            return result;
        }
    }
    states_[0] = initialized[0]; states_[1] = initialized[1];
    frame_ = 0;
    result.frames_remaining = total_frames_;
    return result;
}

SpsdAicaResult SpsdAicaStream::decode_frames(std::size_t frames, std::int16_t* output,
    std::size_t output_capacity, std::int16_t* scratch, std::size_t scratch_capacity) noexcept {
    SpsdAicaResult result{};
    result.requested_frames = frames;
    result.frames_remaining = frames_remaining();
    if (!active_) { result.error = SpsdAicaError::not_started; return result; }
    if (frames > max_frames_per_call) { result.error = SpsdAicaError::size_limit; return result; }
    if (frames > frames_remaining()) { result.error = SpsdAicaError::insufficient_frames; return result; }
    const auto channel_count = channels();
    if (frames > std::numeric_limits<std::size_t>::max() / channel_count ||
        output_capacity > std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t) ||
        scratch_capacity > std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t)) {
        result.error = SpsdAicaError::size_limit;
        return result;
    }
    if (output_capacity < frames * channel_count) { result.error = SpsdAicaError::insufficient_output; return result; }
    if (frames && !scratch_capacity) { result.error = SpsdAicaError::insufficient_scratch; return result; }
    Span destination{}, workspace{}, input{}, owner{};
    if (!span(output, output_capacity * sizeof(std::int16_t), destination) ||
        !span(scratch, scratch_capacity * sizeof(std::int16_t), workspace) ||
        !span(view_.source_data(), view_.source_size(), input) || !span(this, sizeof(*this), owner) ||
        (output_capacity && reinterpret_cast<std::uintptr_t>(output) % alignof(std::int16_t)) ||
        (scratch_capacity && reinterpret_cast<std::uintptr_t>(scratch) % alignof(std::int16_t))) {
        result.error = SpsdAicaError::invalid_buffer;
        return result;
    }
    if (overlap(destination, workspace) || overlap(destination, input) || overlap(workspace, input) ||
        overlap(destination, owner) || overlap(workspace, owner)) {
        result.error = SpsdAicaError::overlapping_storage;
        return result;
    }
    // Complete layout walk before ANY output or scratch write. Immutable view
    // means second-pass ranges are the same; still handle unexpected failures.
    std::size_t checked = 0;
    while (checked < frames) {
        Chunk chunk{};
        if (!plan_chunk(view_, frame_ + checked, frames - checked, scratch_capacity, chunk, result)) return result;
        checked += chunk.frames;
    }
    VgmstreamAicaState candidate[2]{states_[0], states_[1]};
    std::size_t complete = 0;
    while (complete < frames) {
        Chunk chunk{};
        if (!plan_chunk(view_, frame_ + complete, frames - complete, scratch_capacity, chunk, result)) return result;
        for (std::uint32_t ch = 0; ch < channel_count; ++ch) {
            result.codec_called = true;
            result.codec_status = decode_vgmstream_aica(candidate[ch], chunk.channel[ch].data,
                chunk.encoded_bytes, chunk.phase, chunk.frames, VgmstreamAicaNibbleOrder::low_then_high,
                scratch, scratch_capacity);
            result.scratch_modified |= result.codec_status.samples_written != 0;
            if (!result.codec_status || result.codec_status.samples_written != chunk.frames ||
                result.codec_status.bytes_read != chunk.encoded_bytes ||
                result.codec_status.bytes_advanced != (chunk.frames + chunk.phase) / 2 ||
                result.codec_status.next_nibble_phase != ((chunk.frames + chunk.phase) & 1u)) {
                result.error = result.codec_status ? SpsdAicaError::codec_contract_failure : SpsdAicaError::codec_failure;
                result.error_channel = ch;
                result.error_frame = frame_ + complete;
                return result; // No caller-visible state/cursor commit.
            }
            for (std::size_t i = 0; i < chunk.frames; ++i) output[(complete + i) * channel_count + ch] = scratch[i];
            result.output_samples_written += chunk.frames;
            result.output_modified = true;
        }
        complete += chunk.frames;
        result.completed_prefix_frames = complete;
    }
    states_[0] = candidate[0]; states_[1] = candidate[1];
    frame_ += frames;
    result.frames_written = frames;
    result.frames_remaining = frames_remaining();
    result.output_valid = true;
    return result;
}
} // namespace cedar
