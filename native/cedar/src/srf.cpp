// SPDX-License-Identifier: GPL-3.0-only
// Modified native adaptation: brynnb/new-yokosuka, extract_dialogue_inventory.py,
// parse_srf/parse_srf_timing at b0df7f2720a7eecabce2e610fdedec35bf1243e3.
// Original source GPLv3 notice: third_party/licenses/new-yokosuka-GPL3.txt.
#include "cedar/srf.hpp"
#include <limits>

namespace cedar {
namespace {
std::uint16_t word16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8u));
}
std::uint32_t word32(const std::uint8_t* p) noexcept {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8u) |
           (std::uint32_t{p[2]} << 16u) | (std::uint32_t{p[3]} << 24u);
}
SrfStatus fail(SrfError error, std::size_t offset, std::uint32_t index) noexcept {
    return {error, offset, index};
}
bool overlap(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!an || !bn) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < an : x - y < bn;
}
bool terminator(SrfBytes bytes) noexcept {
    return bytes.size >= 4 && word32(bytes.data + bytes.size - 4) == 0xffffffffu;
}
SrfStatus decode_cue(const std::uint8_t* bytes, std::size_t offset,
                      std::uint32_t record, SrfCue& result) noexcept {
    result = {};
    const auto shape = word16(bytes), channel = word16(bytes + 2);
    const auto duration = word16(bytes + 4), reserved = word16(bytes + 6);
    if (shape > 5) return fail(SrfError::unsupported_shape, offset, record);
    if (channel != 2) return fail(SrfError::unsupported_channel, offset + 2, record);
    // Source tuple is signed16. Reject negative/zero durations without wrapping.
    if (!duration || duration > 0x7fffu) return fail(SrfError::invalid_duration, offset + 4, record);
    if (reserved) return fail(SrfError::unsupported_reserved, offset + 6, record);
    result = {shape, channel, duration, reserved};
    return {};
}
SrfStatus read_block(const std::uint8_t* data, std::size_t length, std::size_t& offset,
                      std::uint32_t record, SrfBytes& result) noexcept {
    result = {};
    if (offset > length || length - offset < 4) return fail(SrfError::truncated, offset, record);
    const auto size = word32(data + offset);
    if (size < 4 || size % 4) return fail(SrfError::malformed_length, offset, record);
    if (size > length - offset) return fail(SrfError::truncated, offset, record);
    result = {data + offset + 4, size - 4u};
    offset += size;
    return {};
}
SrfStatus next_record_start(const std::uint8_t* data, std::size_t length,
                             std::size_t& offset, std::uint32_t index) noexcept {
    if (offset == length) return fail(SrfError::end_of_records, offset, index);
    // Scan only a zero-prefix, not the entire remaining member for every record.
    // Final all-zero tail is permitted by the primary parser, even short tails.
    if (length - offset >= 4 && word32(data + offset) != 0) return {};
    auto nonzero = offset;
    while (nonzero < length && !data[nonzero]) ++nonzero;
    if (nonzero == length) { offset = length; return fail(SrfError::end_of_records, offset, index); }
    if (length - offset < 4) return fail(SrfError::truncated, offset, index);
    const auto next_sector = (offset / SrfView::sector_bytes + 1) * SrfView::sector_bytes;
    if (next_sector > length || nonzero < next_sector)
        return fail(SrfError::invalid_padding, offset, index);
    // Whole intermediate sectors are known-zero. At the final boundary a
    // nonzero byte must lie in the first length word; otherwise it is dirty pad.
    const auto candidate = nonzero / SrfView::sector_bytes * SrfView::sector_bytes;
    if (nonzero - candidate >= 4) return fail(SrfError::invalid_padding, candidate, index);
    offset = candidate;
    return {};
}
}

SrfStatus SrfRecordView::cue(std::uint32_t cue_index, SrfCue& result) const noexcept {
    if (overlap(&result, sizeof(result), member_data_, member_length_) || overlap(&result, sizeof(result), this, sizeof(*this)))
        return fail(SrfError::overlapping_storage, timing_offset, index);
    result = {};
    if (!member_data_ || !timing_raw.data) return fail(SrfError::not_parsed, timing_offset, index);
    if (cue_index >= cue_count) return fail(SrfError::invalid_cue_index, timing_offset, index);
    const auto member_begin = reinterpret_cast<std::uintptr_t>(member_data_);
    const auto timing_begin = reinterpret_cast<std::uintptr_t>(timing_raw.data);
    if (timing_begin < member_begin || timing_begin - member_begin > member_length_ ||
        timing_raw.size > member_length_ - (timing_begin - member_begin))
        return fail(SrfError::invalid_timing, timing_offset, index);
    if (!terminator(timing_raw) || (timing_raw.size - 4) % 8 ||
        cue_count != (timing_raw.size - 4) / 8)
        return fail(SrfError::invalid_timing, timing_offset, index);
    const auto relative = static_cast<std::size_t>(cue_index) * 8;
    return decode_cue(timing_raw.data + relative, timing_offset + relative, index, result);
}

SrfStatus SrfCursor::next(SrfRecordView& result) noexcept {
    if (overlap(&result, sizeof(result), data_, length_) || overlap(&result, sizeof(result), this, sizeof(*this)))
        return fail(SrfError::overlapping_storage, offset_, index_);
    result = {};
    if (!data_) return fail(SrfError::not_parsed, offset_, index_);
    if (ended_) return fail(SrfError::end_of_records, offset_, index_);
    const auto start = next_record_start(data_, length_, offset_, index_);
    if (!start) {
        ended_ = start.error == SrfError::end_of_records;
        return start;
    }
    if (index_ == SrfView::max_records) return fail(SrfError::record_limit, offset_, index_);
    SrfRecordView candidate;
    candidate.member_data_ = data_;
    candidate.member_length_ = length_;
    candidate.index = index_;
    candidate.offset = offset_;
    auto end = offset_;
    auto status = read_block(data_, length_, end, index_, candidate.speaker_raw);
    if (!status) return status;
    status = read_block(data_, length_, end, index_, candidate.text_raw);
    if (!status) return status;
    const auto timing_offset = end + 4; // read_block checks its length before use.
    status = read_block(data_, length_, end, index_, candidate.timing_raw);
    if (!status) return status;
    candidate.timing_offset = timing_offset;
    candidate.byte_length = end - candidate.offset;
    candidate.speaker_id = candidate.speaker_raw;
    while (candidate.speaker_id.size) {
        const auto last = candidate.speaker_id.data[candidate.speaker_id.size - 1];
        if (last && last != 0x20u) break;
        --candidate.speaker_id.size;
    }
    for (std::size_t i = 0; i < candidate.speaker_id.size; ++i)
        if (candidate.speaker_id.data[i] < 0x20u || candidate.speaker_id.data[i] > 0x7eu)
            return fail(SrfError::invalid_speaker, candidate.offset + 4 + i, index_);
    candidate.text_content = candidate.text_raw;
    while (candidate.text_content.size && !candidate.text_content.data[candidate.text_content.size - 1])
        --candidate.text_content.size;
    if (candidate.timing_raw.size) {
        if (!terminator(candidate.timing_raw) || (candidate.timing_raw.size - 4) % 8)
            return fail(SrfError::invalid_timing, timing_offset, index_);
        const auto cues = (candidate.timing_raw.size - 4) / 8;
        if (cues > SrfView::max_cues - cues_seen_) return fail(SrfError::cue_limit, timing_offset, index_);
        candidate.cue_count = static_cast<std::uint32_t>(cues);
        for (std::uint32_t i = 0; i < candidate.cue_count; ++i) {
            SrfCue cue;
            const auto relative = static_cast<std::size_t>(i) * 8;
            status = decode_cue(candidate.timing_raw.data + relative, timing_offset + relative, index_, cue);
            if (!status) return status;
        }
    }
    cues_seen_ += candidate.cue_count;
    ++index_;
    offset_ = end;
    result = candidate;
    return {};
}

SrfStatus SrfView::parse(const void* bytes, std::size_t length) noexcept {
    if (overlap(bytes, length, this, sizeof(*this))) return fail(SrfError::overlapping_storage, 0, UINT32_MAX);
    data_ = nullptr; length_ = count_ = 0;
    if (!bytes) return fail(SrfError::invalid_buffer, 0, UINT32_MAX);
    if (length > max_bytes) return fail(SrfError::size_limit, 0, UINT32_MAX);
    if (length > std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(bytes))
        return fail(SrfError::invalid_buffer, 0, UINT32_MAX);
    SrfCursor candidate;
    candidate.data_ = static_cast<const std::uint8_t*>(bytes);
    candidate.length_ = length;
    for (;;) {
        SrfRecordView record;
        const auto status = candidate.next(record);
        if (status.error == SrfError::end_of_records) break;
        if (!status) return status;
    }
    data_ = candidate.data_; length_ = length; count_ = candidate.index_;
    return {};
}
SrfCursor SrfView::cursor() const noexcept {
    SrfCursor result;
    result.data_ = data_; result.length_ = length_;
    return result;
}
SrfStatus SrfView::record(std::uint32_t record_index, SrfRecordView& result) const noexcept {
    if (overlap(&result, sizeof(result), data_, length_) || overlap(&result, sizeof(result), this, sizeof(*this)))
        return fail(SrfError::overlapping_storage, 0, record_index);
    result = {};
    if (!parsed()) return fail(SrfError::not_parsed, 0, record_index);
    if (record_index >= count_) return fail(SrfError::invalid_record_index, 0, record_index);
    auto current = cursor();
    for (std::uint32_t i = 0; i <= record_index; ++i) {
        const auto status = current.next(result);
        if (!status) return status;
    }
    return {};
}
} // namespace cedar
