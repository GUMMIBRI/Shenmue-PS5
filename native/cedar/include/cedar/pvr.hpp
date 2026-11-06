// Independent bounded implementation of a narrow ShenmueDK-compatible profile.
// Format provenance: MIT ShenmueDK 1dae443814b3755281d207a13f25571c0e51a5d1.
// Copyright (c) 2019 Phil; preserved notice third_party/licenses/shenmuedk-MIT.txt.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class PvrError : std::uint8_t {
    none, invalid_buffer, truncated, invalid_signature, invalid_dimensions,
    resource_limit, unsupported_pixel_format, unsupported_layout,
    output_too_small, overlapping_buffers, not_parsed
};
struct PvrStatus {
    PvrError error{PvrError::none};
    std::size_t offset{};
    explicit operator bool() const noexcept { return error == PvrError::none; }
};
enum class PvrPixelFormat : std::uint8_t { argb1555 = 0, rgb565 = 1, argb4444 = 2 };
enum class PvrLayout : std::uint8_t {
    square_twiddled = 1, vector_quantized = 3, rectangle = 9, rectangle_twiddled = 13
};
struct PvrMetadata {
    std::uint16_t width{}, height{}, reserved{};
    PvrPixelFormat pixel_format{PvrPixelFormat::argb1555};
    PvrLayout layout{PvrLayout::rectangle};
    bool has_gbix{};
    std::uint32_t global_index{};
    // Opaque, UNVERIFIED size fields. This profile does not use them as bounds.
    // Caller must pass a separately bounded resource, e.g. an exact TEXN body.
    std::uint32_t declared_size{}, gbix_declared_size{};
    // pixel_offset/bytes cover the complete encoded payload, including the
    // codebook and index plane for VQ. Other layouts have no codebook/index span.
    std::size_t pvrt_offset{}, pixel_offset{}, pixel_bytes{}, rgba_bytes{};
    std::size_t codebook_offset{}, codebook_bytes{}, index_offset{}, index_bytes{};
    std::size_t consumed_bytes{}, trailing_bytes{};
};

// Non-owning narrow profile, not a general Dreamcast PVR/PVR3 chunk reader.
// Accepts bare PVRT or exactly the compatible reader's fixed12-byte GBIX prefix.
// Dimensions/pixel count and caller extent bound reads; size fields stay opaque.
// Supports only nonmipped 16-bit layouts above; VQ has a full256-vector codebook.
// No indexed palette, smallVQ, YUV, stride,
// RLE/size prefix, DDS-in-PVRT, guessed alignment or generic GBIX extension walk.
// Source memory must remain valid and immutable. Failed parse clears prior view.
class PvrView final {
public:
    static constexpr std::uint32_t max_dimension = 4096;
    static constexpr std::size_t max_file_bytes = 64u * 1024u * 1024u;
    static constexpr std::size_t max_decoded_bytes = 64u * 1024u * 1024u;
    PvrStatus parse(const void* resource, std::size_t bytes) noexcept;
    bool valid() const noexcept { return data_ != nullptr; }
    const PvrMetadata& metadata() const noexcept { return metadata_; }
    // RGBA byte output in encoded row coordinate order; no vertical flip,
    // transfer conversion or inferred alpha premultiplication. Origin/transfer
    // semantics are unspecified. RGB scaling matches compatible codec floor
    // expansion; 1-bit alpha is 0/255 and 4-bit alpha is nibble*17.
    // Full source/output spans must be disjoint. Row padding stays untouched.
    PvrStatus decode_rgba(void* output, std::size_t capacity,
                          std::size_t row_pitch) const noexcept;
private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    PvrMetadata metadata_{};
};
} // namespace cedar
