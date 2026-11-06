// SPDX-License-Identifier: GPL-3.0-only
// Modified bounded native adaptation of authored new-yokosuka SRF parsing.
// Exact source/notice receipt: docs/SRF-SCOPE.md; no dialogue data included.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cedar {
enum class SrfError : std::uint8_t {
    none, invalid_buffer, overlapping_storage, size_limit, truncated, malformed_length, invalid_padding,
    invalid_speaker, invalid_timing, unsupported_shape, unsupported_channel,
    invalid_duration, unsupported_reserved, record_limit, cue_limit,
    not_parsed, invalid_record_index, invalid_cue_index, end_of_records
};
struct SrfStatus {
    SrfError error{SrfError::none};
    std::size_t offset{}; // byte offset in the SRF member
    std::uint32_t record_index{UINT32_MAX};
    explicit operator bool() const noexcept { return error == SrfError::none; }
};
struct SrfBytes { const std::uint8_t* data{}; std::size_t size{}; };
struct SrfCue {
    std::uint16_t shape{}, channel{}, duration_ticks{}, reserved{};
    // Numeric source fields only: no model-shape mapping or inferred seconds.
};
struct SrfRecordView {
    std::uint32_t index{};
    std::size_t offset{}, byte_length{}, timing_offset{};
    SrfBytes speaker_raw{}, speaker_id{}; // ID removes trailing NUL/space only.
    SrfBytes text_raw{}, text_content{}; // Content removes trailing NUL only.
    SrfBytes timing_raw{}; // Empty or cue tuples followed by four FF bytes.
    std::uint32_t cue_count{};
    SrfStatus cue(std::uint32_t index, SrfCue& result) const noexcept;
private:
    friend class SrfCursor;
    const std::uint8_t* member_data_{};
    std::size_t member_length_{};
};

class SrfView;
class SrfCursor {
public:
    SrfStatus next(SrfRecordView& result) noexcept;
private:
    friend class SrfView;
    const std::uint8_t* data_{};
    std::size_t length_{}, offset_{};
    std::uint32_t index_{}, cues_seen_{};
    bool ended_{};
};

// Explicitly selected SRF member; no magic-based archive/file discovery.
// Allocation-free borrowed views. Input stays immutable/alive through every view,
// cursor and span use; do not resume a cursor after a non-end error. No text
// conversion, I/O, voice matching/playback, script VM or dialogue-progress state.
class SrfView {
public:
    static constexpr std::size_t max_bytes = 64u * 1024u * 1024u;
    static constexpr std::uint32_t max_records = 65536;
    static constexpr std::uint32_t max_cues = 1024u * 1024u;
    static constexpr std::size_t sector_bytes = 0x800;
    // Validates complete stream before publishing any view. Failure discards
    // prior state. An invalid input/view alias is rejected without touching
    // either object. Nonnull zero-length/all-zero input is a valid empty stream.
    SrfStatus parse(const void* bytes, std::size_t length) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    std::uint32_t record_count() const noexcept { return count_; }
    SrfCursor cursor() const noexcept;
    // Indexed lookup scans preceding records; cursor is linear for full iteration.
    SrfStatus record(std::uint32_t index, SrfRecordView& result) const noexcept;
private:
    const std::uint8_t* data_{};
    std::size_t length_{};
    std::uint32_t count_{};
};
} // namespace cedar
