// SPDX-License-Identifier: ISC
// Adapted bounded metadata/layout rules from official vgmstream/vgmstream
// 6802153425adde134e49394fc653455166d7d2f2 src/meta/spsd.c. Root COPYING and
// authorship notices retained in third_party/licenses/vgmstream-yamaha-ISC.txt.
#include "cedar/spsd.hpp"
#include <limits>

namespace cedar {
namespace {
std::uint16_t le16(const std::uint8_t* p) noexcept {
    return std::uint16_t(p[0]) | std::uint16_t(std::uint16_t(p[1]) << 8);
}
std::uint32_t le32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint32_t be32(const std::uint8_t* p) noexcept {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
        (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
bool valid_span(const void* data, std::size_t size) noexcept {
    return (!size || data) && size <= std::numeric_limits<std::uintptr_t>::max() -
        reinterpret_cast<std::uintptr_t>(data);
}
bool separate(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!valid_span(a, an) || !valid_span(b, bn)) return false;
    if (!an || !bn) return true;
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x + an <= y || y + bn <= x;
}
} // namespace

SpsdStatus SpsdView::parse(const void* bytes, std::size_t length) noexcept {
    data_ = nullptr;
    length_ = 0;
    metadata_ = {};
    if (!bytes || !valid_span(bytes, length) || !separate(bytes, length, this, sizeof(*this)))
        return {SpsdError::invalid_buffer};
    if (length < header_bytes) return {SpsdError::truncated, length};
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    if (p[0] != 'S' || p[1] != 'P' || p[2] != 'S' || p[3] != 'D')
        return {SpsdError::invalid_magic};
    SpsdMetadata candidate{};
    candidate.version = be32(p + 4);
    if (candidate.version != 0x01010004u && candidate.version != 0x00010004u)
        return {SpsdError::unsupported_version, 4};
    candidate.raw_codec = p[8];
    candidate.raw_flags = p[9];
    candidate.channels = (candidate.raw_flags & 3u) ? 2u : 1u;
    candidate.raw_loop_flag = candidate.raw_flags & 0x80u;
    candidate.raw_index = le16(p + 0x0a);
    candidate.data_bytes = le32(p + 0x0c);
    candidate.sample_rate = le16(p + 0x2a);
    candidate.raw_loop_word = le32(p + 0x2c);
    if (!candidate.sample_rate) return {SpsdError::invalid_rate, 0x2a};
    if (candidate.data_bytes > length - header_bytes) return {SpsdError::truncated, 0x0c};
    candidate.trailing_bytes = length - header_bytes - candidate.data_bytes;
    if (candidate.data_bytes % candidate.channels) return {SpsdError::invalid_data_size, 0x0c};
    candidate.bytes_per_channel = candidate.data_bytes / candidate.channels;
    switch (candidate.raw_codec) {
    case 0:
        candidate.codec = SpsdCodec::pcm16_le;
        if (candidate.bytes_per_channel % 2) return {SpsdError::invalid_data_size, 0x0c};
        break;
    case 1: candidate.codec = SpsdCodec::pcm8; break;
    case 3:
        candidate.codec = SpsdCodec::aica_int;
        candidate.aica_profile = SpsdAicaProfile::vgmstream_filtered_low_first_per_channel;
        break;
    default: return {SpsdError::unsupported_codec, 8};
    }
    switch (candidate.raw_index) {
    case 0:
        if (candidate.channels != 1) return {SpsdError::unsupported_layout, 0x0a};
        candidate.layout = SpsdLayout::mono;
        candidate.interleave_bytes = candidate.data_bytes;
        break;
    case 0x0d:
        candidate.layout = SpsdLayout::block_interleaved_0x2000;
        candidate.interleave_bytes = interleave_block_bytes;
        break;
    case 0xff:
        candidate.layout = SpsdLayout::planar;
        candidate.interleave_bytes = candidate.bytes_per_channel;
        break;
    default: return {SpsdError::unsupported_layout, 0x0a};
    }
    data_ = p;
    length_ = length;
    metadata_ = candidate;
    return {};
}

SpsdStatus SpsdView::channel_slice(std::uint32_t channel, std::size_t offset,
                                  std::size_t length, SpsdSlice& output) const noexcept {
    if (!separate(&output, sizeof(output), this, sizeof(*this)) ||
        !separate(&output, sizeof(output), data_, length_))
        return {SpsdError::invalid_buffer};
    output = {};
    if (!parsed()) return {SpsdError::not_parsed};
    if (channel >= metadata_.channels) return {SpsdError::invalid_channel, channel};
    if (offset > metadata_.bytes_per_channel || length > metadata_.bytes_per_channel - offset)
        return {SpsdError::out_of_range, offset};
    std::uint64_t physical{}, available{};
    if (!length && offset == metadata_.bytes_per_channel) {
        physical = metadata_.data_bytes; // Explicit empty end marker, never audio bytes.
    } else if (metadata_.layout != SpsdLayout::block_interleaved_0x2000 || metadata_.channels == 1) {
        physical = std::uint64_t{channel} * metadata_.bytes_per_channel + offset;
        available = metadata_.bytes_per_channel - offset;
    } else {
        const std::uint64_t block = interleave_block_bytes;
        const std::uint64_t group = block * metadata_.channels;
        const std::uint64_t full_groups = metadata_.data_bytes / group;
        const std::uint64_t full_channel_bytes = full_groups * block;
        if (offset < full_channel_bytes) {
            const std::uint64_t group_index = offset / block;
            const std::uint64_t within = offset % block;
            physical = group_index * group + std::uint64_t{channel} * block + within;
            available = block - within;
        } else {
            const std::uint64_t tail_per_channel = (metadata_.data_bytes % group) / metadata_.channels;
            const std::uint64_t within = offset - full_channel_bytes;
            if (within > tail_per_channel) return {SpsdError::out_of_range, offset};
            physical = full_groups * group + std::uint64_t{channel} * tail_per_channel + within;
            available = tail_per_channel - within;
        }
    }
    if (length > available) return {SpsdError::noncontiguous_range, offset, static_cast<std::size_t>(available)};
    if (physical > metadata_.data_bytes || length > metadata_.data_bytes - physical)
        return {SpsdError::out_of_range, offset};
    const auto file_offset = header_bytes + static_cast<std::size_t>(physical);
    output = {data_ + file_offset, length, file_offset};
    return {SpsdError::none, 0, static_cast<std::size_t>(available)};
}
} // namespace cedar
