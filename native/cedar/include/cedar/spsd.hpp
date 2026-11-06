// SPDX-License-Identifier: ISC
// Source-informed vgmstream SPSD metadata/layout subset; full notices retained
// in third_party/licenses/vgmstream-yamaha-ISC.txt. See docs/SPSD-SCOPE.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class SpsdCodec : std::uint8_t { pcm16_le, pcm8, aica_int };
enum class SpsdLayout : std::uint8_t { mono, block_interleaved_0x2000, planar };
enum class SpsdAicaProfile : std::uint8_t { none, vgmstream_filtered_low_first_per_channel };
enum class SpsdError : std::uint8_t {
    none, invalid_buffer, truncated, invalid_magic, unsupported_version,
    unsupported_codec, unsupported_layout, invalid_data_size, invalid_rate,
    not_parsed, invalid_channel, out_of_range, noncontiguous_range
};
struct SpsdStatus {
    SpsdError error{SpsdError::none};
    std::size_t offset{}; // Header offset or rejected logical-channel byte offset.
    std::size_t contiguous_bytes{}; // Available physical span at a valid channel offset.
    explicit operator bool() const noexcept { return error == SpsdError::none; }
};
struct SpsdMetadata {
    std::uint32_t version{}, data_bytes{}, sample_rate{}, raw_loop_word{};
    std::uint16_t raw_index{};
    std::uint8_t raw_codec{}, raw_flags{}, raw_loop_flag{}, channels{};
    SpsdCodec codec{SpsdCodec::pcm16_le};
    SpsdLayout layout{SpsdLayout::mono};
    SpsdAicaProfile aica_profile{SpsdAicaProfile::none};
    std::uint32_t interleave_bytes{}, bytes_per_channel{};
    std::size_t trailing_bytes{}; // Outside declared payload; not decoded or authenticated padding.
};
struct SpsdSlice {
    const std::uint8_t* data{};
    std::size_t bytes{}, file_offset{}; // Actual bounded borrowed physical span.
};

// Borrows one complete immutable header+payload buffer. No file/codec/playback,
// allocation, loop application, rate conversion or original-game association.
class SpsdView final {
public:
    static constexpr std::size_t header_bytes = 0x40;
    static constexpr std::uint32_t interleave_block_bytes = 0x2000;
    // Failed parse invalidates previous view. Trailing bytes are exposed but not
    // guessed as audio; each channel and each PCM16 sample must divide exactly.
    SpsdStatus parse(const void* bytes, std::size_t length) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    const SpsdMetadata& metadata() const noexcept { return metadata_; }
    const std::uint8_t* source_data() const noexcept { return data_; }
    std::size_t source_size() const noexcept { return length_; }
    // Logical offsets count encoded BYTES in one channel, not PCM sample frames.
    // For multichannel0x0d, a range must stay in one physical per-channel block
    // (including its short tail). noncontiguous_range reports the available span.
    // No whole-channel contiguity is falsely promised for block stereo.
    SpsdStatus channel_slice(std::uint32_t channel, std::size_t channel_byte_offset,
                             std::size_t length, SpsdSlice& output) const noexcept;
private:
    const std::uint8_t* data_{};
    std::size_t length_{};
    SpsdMetadata metadata_{};
};
} // namespace cedar
