#include "cedar/payload.hpp"

#include <cstring>

namespace cedar {
namespace {
std::uint16_t word16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}
std::uint32_t word32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}
PayloadStatus require_bytes(std::size_t cursor, std::size_t count,
                             std::size_t length) noexcept {
    if (cursor > gzip_header_limit || count > gzip_header_limit - cursor)
        return {PayloadError::header_limit};
    if (cursor > length || count > length - cursor) return {PayloadError::truncated};
    return {};
}
PayloadStatus skip_string(const std::uint8_t* bytes, std::size_t length,
                           std::size_t& cursor) noexcept {
    for (;;) {
        const auto available = require_bytes(cursor, 1, length);
        if (!available) return available;
        if (!bytes[cursor++]) return {};
    }
}
}

PayloadStatus parse_gzip_header(const void* prefix, std::size_t length,
                                GzipHeader& result) noexcept {
    result = {};
    if (!prefix) return {PayloadError::invalid_buffer};
    if (length < 2) return {PayloadError::truncated};
    const auto* bytes = static_cast<const std::uint8_t*>(prefix);
    if (bytes[0] != 0x1fu || bytes[1] != 0x8bu) return {PayloadError::unknown_signature};
    if (length < 10) return {PayloadError::truncated};
    if (bytes[2] != 8) return {PayloadError::unsupported_method};
    if (bytes[3] & 0xe0u) return {PayloadError::malformed_header};

    GzipHeader candidate;
    candidate.flags = bytes[3];
    candidate.modification_time = word32(bytes + 4);
    candidate.extra_flags = bytes[8];
    candidate.operating_system = bytes[9];
    std::size_t cursor = 10;
    if (candidate.flags & 0x04u) {
        auto available = require_bytes(cursor, 2, length);
        if (!available) return available;
        candidate.extra_bytes = word16(bytes + cursor);
        cursor += 2;
        available = require_bytes(cursor, candidate.extra_bytes, length);
        if (!available) return available;
        cursor += candidate.extra_bytes;
    }
    if (candidate.flags & 0x08u) {
        const auto available = skip_string(bytes, length, cursor);
        if (!available) return available;
    }
    if (candidate.flags & 0x10u) {
        const auto available = skip_string(bytes, length, cursor);
        if (!available) return available;
    }
    if (candidate.flags & 0x02u) {
        const auto available = require_bytes(cursor, 2, length);
        if (!available) return available;
        candidate.opaque_header_crc16 = word16(bytes + cursor);
        cursor += 2;
    }
    candidate.header_bytes = cursor;
    result = candidate;
    return {};
}

PayloadStatus probe_payload(const void* prefix, std::size_t length,
                             PayloadInfo& result) noexcept {
    result = {};
    if (!prefix) return {PayloadError::invalid_buffer};
    if (length < 2) return {PayloadError::truncated};
    const auto* bytes = static_cast<const std::uint8_t*>(prefix);
    if (bytes[0] == 0x1fu && bytes[1] == 0x8bu) {
        GzipHeader header;
        const auto status = parse_gzip_header(prefix, length, header);
        if (!status) return status;
        result = {PayloadKind::gzip, true, header};
        return {};
    }
    if (length < 4) return {PayloadError::truncated};
    PayloadKind kind;
    switch (word32(bytes)) {
    case 0x464b4150u: kind = PayloadKind::pkf; break;  // PAKF
    case 0x43415049u: kind = PayloadKind::ipac; break; // IPAC
    case 0x54525650u: kind = PayloadKind::pvrt; break; // PVRT
    case 0x58494247u: kind = PayloadKind::gbix; break; // GBIX
    default: return {PayloadError::unknown_signature};
    }
    result.kind = kind;
    return {};
}

IpacStatus IpacIndex::parse(const void* bytes, std::size_t length) noexcept {
    data_ = nullptr;
    header_ = {};
    if (!bytes) return {IpacError::invalid_buffer};
    if (length < header_bytes) return {IpacError::truncated};
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    if (word32(data) != 0x43415049u) return {IpacError::unknown_signature};
    const IpacHeader candidate{word32(data + 4), word32(data + 8), word32(data + 12)};
    if (candidate.dictionary_offset < header_bytes) return {IpacError::malformed_index};
    // This relation is established by the compatible source's writer. Other
    // layouts are not silently interpreted as having the same payload bound.
    if (candidate.content_size != candidate.dictionary_offset)
        return {IpacError::unsupported_layout};
    if (candidate.dictionary_offset > length) return {IpacError::truncated};
    if (candidate.file_count > max_entries) return {IpacError::too_many_entries};
    const auto dictionary_bytes = length - candidate.dictionary_offset;
    if (candidate.file_count > dictionary_bytes / dictionary_entry_bytes)
        return {IpacError::truncated};

    for (std::uint32_t index = 0; index < candidate.file_count; ++index) {
        const auto* encoded = data + candidate.dictionary_offset +
                              static_cast<std::size_t>(index) * dictionary_entry_bytes;
        const auto offset = word32(encoded + 12);
        const auto size = word32(encoded + 16);
        if (offset < header_bytes || offset > candidate.dictionary_offset ||
            size > candidate.dictionary_offset - offset)
            return {IpacError::entry_out_of_range, index};
    }
    header_ = candidate;
    data_ = data;
    return {};
}

IpacStatus IpacIndex::entry(std::uint32_t index, IpacEntry& result) const noexcept {
    result = {};
    if (!parsed()) return {IpacError::not_parsed};
    if (index >= header_.file_count) return {IpacError::invalid_entry_index, index};
    const auto* encoded = data_ + header_.dictionary_offset +
                          static_cast<std::size_t>(index) * dictionary_entry_bytes;
    std::memcpy(result.name, encoded, sizeof(result.name));
    std::memcpy(result.extension, encoded + 8, sizeof(result.extension));
    result.offset = word32(encoded + 12);
    result.size = word32(encoded + 16);
    return {};
}

IpacStatus IpacIndex::member(std::uint32_t index, const void*& bytes,
                            std::size_t& length) const noexcept {
    bytes = nullptr;
    length = 0;
    IpacEntry metadata;
    const auto status = entry(index, metadata);
    if (!status) return status;
    bytes = data_ + metadata.offset;
    length = metadata.size;
    return {};
}

} // namespace cedar
