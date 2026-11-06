#pragma once

// TAD layout derived from MIT-licensed ShenmueDK (Copyright (c) 2019 Phil).
// See docs/TAD-PROVENANCE.md and third_party/licenses/shenmuedk-MIT.txt.
#include <cstddef>
#include <cstdint>

namespace cedar {

enum class TadError {
    none, invalid_buffer, truncated, unsupported_profile, unsupported_extension,
    malformed_index, too_many_entries, invalid_entry_index, entry_out_of_range,
    tac_size_mismatch, not_parsed, not_found
};

struct TadStatus {
    TadError error = TadError::none;
    // Index of offending entry, or UINT32_MAX for header/general failures.
    std::uint32_t entry_index = UINT32_MAX;
    explicit operator bool() const noexcept { return error == TadError::none; }
};

struct TadHeader {
    std::uint32_t file_type = 0;
    std::uint32_t identifier1 = 0;
    std::uint32_t identifier2 = 0;
    std::uint32_t unix_timestamp = 0;
    std::uint32_t render_type = 0;
    std::uint32_t opaque_header_checksum = 0; // exposed, not authenticated
    std::uint32_t tac_size = 0;
    std::uint32_t file_count = 0;
};

struct TadEntry {
    std::uint32_t hash1 = 0;
    std::uint32_t hash2 = 0;
    std::uint32_t unknown = 0; // uninterpreted; no compression inference
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// Non-owning, allocation-free view of the supported 2018 PC DX11 index profile.
// Caller retains an immutable byte buffer throughout this object's use. Parsing
// reads little-endian words explicitly; it needs no host packing or alignment.
// This supplies raw TAC ranges only, not names, decompression or game loading.
class TadIndex {
public:
    static constexpr std::size_t header_bytes = 60;
    static constexpr std::size_t entry_bytes = 32;
    static constexpr std::uint32_t max_entries = 1024u * 1024u;

    // A failed parse discards a previous view. Validates every entry against
    // declared TAC size, rejects trailing bytes and unimplemented layout forms.
    TadStatus parse(const void* bytes, std::size_t length) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    const TadHeader& header() const noexcept { return header_; }
    TadStatus entry(std::uint32_t index, TadEntry& result) const noexcept;
    TadStatus find_hash(std::uint32_t hash1, std::uint32_t hash2,
                         std::uint32_t& index, TadEntry& result) const noexcept;
    // Bind to the actual paired TAC length before using any returned range.
    TadStatus validate_tac_size(std::uint64_t actual_bytes) const noexcept;
    static TadStatus validate_range(const TadEntry& entry, std::uint64_t tac_bytes) noexcept;
private:
    const std::uint8_t* data_ = nullptr;
    TadHeader header_{};
};

} // namespace cedar
