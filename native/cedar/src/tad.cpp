#include "cedar/tad.hpp"

// Format/layout attribution: Shenmue-Mods/ShenmueDK, MIT, Copyright (c) 2019 Phil.
// The bounded memory-view implementation below is original Cedar code.
namespace cedar {
namespace {
std::uint32_t little32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

TadEntry decode_entry(const std::uint8_t* bytes) noexcept {
    return {little32(bytes), little32(bytes + 4), little32(bytes + 8),
            little32(bytes + 16), little32(bytes + 24)};
}

bool unsupported_entry_words(const std::uint8_t* bytes) noexcept {
    // The reference calls these pad12/pad20/pad28. A nonzero word might describe
    // another layout or wider values; never silently discard it as padding.
    return little32(bytes + 12) || little32(bytes + 20) || little32(bytes + 28);
}
}

TadStatus TadIndex::parse(const void* bytes, std::size_t length) noexcept {
    data_ = nullptr;
    header_ = {};
    if (!bytes) return {TadError::invalid_buffer};
    if (length < header_bytes) return {TadError::truncated};
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    TadHeader candidate;
    candidate.file_type = little32(data);
    candidate.identifier1 = little32(data + 4);
    candidate.identifier2 = little32(data + 8);
    candidate.unix_timestamp = little32(data + 16);
    candidate.render_type = little32(data + 24);
    candidate.opaque_header_checksum = little32(data + 32);
    candidate.tac_size = little32(data + 40);
    candidate.file_count = little32(data + 48);

    // Narrow observed DX11 profile; these words are not a universal signature.
    if (candidate.file_type != 1 || candidate.identifier1 != 5 ||
        candidate.identifier2 != 2 || candidate.render_type != 0x31317864u)
        return {TadError::unsupported_profile};
    constexpr std::size_t reserved_offsets[] = {12u, 20u, 28u, 36u, 44u, 52u};
    for (const std::size_t offset : reserved_offsets) {
        if (little32(data + offset)) return {TadError::unsupported_extension};
    }
    if (little32(data + 56) != candidate.file_count)
        return {TadError::malformed_index};
    if (candidate.file_count > max_entries) return {TadError::too_many_entries};
    // Division precedes multiplication, preventing wrapped table sizes on 32-bit hosts.
    const auto available = length - header_bytes;
    if (candidate.file_count > available / entry_bytes) return {TadError::truncated};
    if (available / entry_bytes != candidate.file_count || available % entry_bytes)
        return {TadError::unsupported_extension};

    for (std::uint32_t index = 0; index < candidate.file_count; ++index) {
        const auto* encoded = data + header_bytes + static_cast<std::size_t>(index) * entry_bytes;
        if (unsupported_entry_words(encoded))
            return {TadError::unsupported_extension, index};
        const auto checked = validate_range(decode_entry(encoded), candidate.tac_size);
        if (!checked) return {checked.error, index};
    }
    header_ = candidate;
    data_ = data;
    return {};
}

TadStatus TadIndex::entry(std::uint32_t index, TadEntry& result) const noexcept {
    result = {};
    if (!parsed()) return {TadError::not_parsed};
    if (index >= header_.file_count) return {TadError::invalid_entry_index, index};
    const auto* encoded = data_ + header_bytes + static_cast<std::size_t>(index) * entry_bytes;
    result = decode_entry(encoded);
    return {};
}

TadStatus TadIndex::find_hash(std::uint32_t hash1, std::uint32_t hash2,
                             std::uint32_t& index, TadEntry& result) const noexcept {
    index = UINT32_MAX;
    result = {};
    if (!parsed()) return {TadError::not_parsed};
    for (std::uint32_t candidate = 0; candidate < header_.file_count; ++candidate) {
        const auto decoded = decode_entry(data_ + header_bytes +
                                         static_cast<std::size_t>(candidate) * entry_bytes);
        if (decoded.hash1 == hash1 && decoded.hash2 == hash2) {
            index = candidate;
            result = decoded;
            return {};
        }
    }
    return {TadError::not_found};
}

TadStatus TadIndex::validate_tac_size(std::uint64_t actual_bytes) const noexcept {
    if (!parsed()) return {TadError::not_parsed};
    return actual_bytes == header_.tac_size ? TadStatus{} : TadStatus{TadError::tac_size_mismatch};
}

TadStatus TadIndex::validate_range(const TadEntry& entry, std::uint64_t tac_bytes) noexcept {
    if (entry.offset > tac_bytes || entry.size > tac_bytes - entry.offset)
        return {TadError::entry_out_of_range};
    return {};
}

} // namespace cedar
