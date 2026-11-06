#pragma once

// Layout derived from MIT ShenmueDK; see docs/PKF-SCOPE.md and preserved notice.
#include "cedar/payload.hpp"

#include <cstddef>
#include <cstdint>

namespace cedar {

enum class PkfError {
    none, invalid_buffer, truncated, unknown_signature, malformed_node,
    unsupported_node, too_many_textures, invalid_texture_index, not_parsed,
    end_of_textures, no_ipac, unsupported_trailing_data, ipac_failure
};
struct PkfStatus {
    PkfError error = PkfError::none;
    std::uint32_t texture_index = UINT32_MAX;
    IpacStatus ipac_status{};
    explicit operator bool() const noexcept { return error == PkfError::none; }
};
struct PkfBytes { const std::uint8_t* data = nullptr; std::size_t size = 0; };
struct PkfHeader {
    std::uint32_t content_size = 0;
    std::uint32_t unknown = 0; // uninterpreted
    std::uint32_t texture_count = 0;
};
struct PkfTextureView {
    std::uint32_t index = 0;
    std::uint64_t node_offset = 0;
    std::uint32_t node_size = 0; // inclusive of the 8-byte node header
    std::uint8_t texture_id[8]{}; // opaque fixed-width ID, not a string/hash rule
    PkfBytes texture_body{}; // raw PVR-side bytes; no validation or decoding
};

// Complete immutable plain PAKF bytes only. A caller must decode/verify gzip
// first if applicable. This view never mutates or owns the caller's storage.
// Optional DUMY, declared TEXN nodes and optional trailing IPAC are checked.
// Content bytes after the declared TEXN nodes are exposed as opaque remainder,
// never guessed to be padding or additional valid texture nodes.
class PkfView {
public:
    static constexpr std::size_t header_bytes = 16;
    static constexpr std::uint32_t max_textures = 1024u * 1024u;

    class Cursor {
    public:
        Cursor() noexcept = default;
        PkfStatus next(PkfTextureView& texture) noexcept;
    private:
        friend class PkfView;
        const std::uint8_t* data_ = nullptr;
        std::size_t offset_ = 0;
        std::size_t content_end_ = 0;
        std::uint32_t count_ = 0;
        std::uint32_t index_ = 0;
    };

    // Failure discards a previous view. Input storage must outlive this view,
    // every cursor, and all returned spans, and remain immutable throughout.
    PkfStatus parse(const void* bytes, std::size_t length) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    const PkfHeader& header() const noexcept { return header_; }
    Cursor textures() const noexcept;
    // Indexed access scans preceding nodes; use a cursor for linear traversal.
    PkfStatus texture(std::uint32_t index, PkfTextureView& result) const noexcept;
    PkfStatus optional_ipac(IpacIndex& result) const noexcept;
    PkfBytes dummy_body() const noexcept;
    PkfBytes content_remainder() const noexcept;
private:
    const std::uint8_t* data_ = nullptr;
    std::size_t length_ = 0;
    PkfHeader header_{};
    std::size_t first_texture_ = 0;
    std::size_t textures_end_ = 0;
    std::size_t dummy_size_ = 0;
};

} // namespace cedar
