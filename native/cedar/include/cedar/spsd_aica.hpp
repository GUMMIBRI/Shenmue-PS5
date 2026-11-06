// SPDX-License-Identifier: ISC
// Actual SPSD AICA resource consumption using retained vgmstream compatibility.
#pragma once
#include "cedar/spsd.hpp"
#include "cedar/aica_adpcm.hpp"

namespace cedar {
enum class SpsdAicaError : std::uint8_t {
    none, not_started, unsupported_source, invalid_buffer, overlapping_storage,
    size_limit, insufficient_output, insufficient_scratch, insufficient_frames,
    source_failure, source_contract_failure, codec_failure, codec_contract_failure
};
struct SpsdAicaResult {
    SpsdAicaError error{SpsdAicaError::none};
    SpsdStatus source_status{};
    VgmstreamAicaResult codec_status{};
    std::uint32_t error_channel{UINT32_MAX};
    std::uint64_t error_frame{}, frames_remaining{};
    std::size_t requested_frames{}, frames_written{}, output_samples_written{}, completed_prefix_frames{};
    bool source_attempted{}, codec_called{}, output_valid{}, output_modified{}, scratch_modified{};
    explicit operator bool() const noexcept { return error == SpsdAicaError::none; }
};

// Decode-once source, not playback/resampling/voice/game integration. Owns only
// two predictor states and a view COPY; retains immutable caller-owned bytes.
// Serialize all operations. stop/destruction does not release caller storage.
class SpsdAicaStream final {
public:
    static constexpr std::size_t max_frames_per_call = vgmstream_aica_max_samples;
    SpsdAicaStream() noexcept = default;
    SpsdAicaStream(const SpsdAicaStream&) = delete;
    SpsdAicaStream& operator=(const SpsdAicaStream&) = delete;
    // Any failed start discards the previous stream. Only validated codec3 and
    // source-backed filtered/low-first per-channel SPSD compatibility accepted.
    SpsdAicaResult start(const SpsdView& source) noexcept;
    void stop() noexcept;
    SpsdAicaResult rewind() noexcept; // Explicit real 0/127 reset, never auto-loop.
    // EXACT frames, output capacity in interleaved int16 sample elements;
    // scratch capacity in mono int16 elements. Scratch >=1 for nonzero requests.
    // Full preflight before any writes. States/cursor commit only full success.
    // Unexpected second-pass error can alter scattered output elements; discard
    // all output unless output_valid. output_samples_written is NOT a byte prefix.
    SpsdAicaResult decode_frames(std::size_t requested_frames,
        std::int16_t* interleaved, std::size_t output_capacity_samples,
        std::int16_t* mono_scratch, std::size_t scratch_capacity_samples) noexcept;
    bool started() const noexcept { return active_; }
    std::uint32_t sample_rate() const noexcept { return active_ ? view_.metadata().sample_rate : 0; }
    std::uint32_t channels() const noexcept { return active_ ? view_.metadata().channels : 0; }
    std::uint64_t total_frames() const noexcept { return total_frames_; }
    std::uint64_t frames_remaining() const noexcept { return total_frames_ - frame_; }
private:
    SpsdView view_{};
    VgmstreamAicaState states_[2]{};
    std::uint64_t frame_{}, total_frames_{};
    bool active_{};
};
} // namespace cedar
