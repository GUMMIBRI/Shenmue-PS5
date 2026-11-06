// Original in-memory DDS/gzip-DDS composition. No archive or game path inference.
#pragma once
#include "cedar/dds.hpp"
#include "cedar/gzip_decode.hpp"

namespace cedar {
enum class TextureResourceKind : std::uint8_t { unknown, dds, gzip_dds };
enum class TextureResourceError : std::uint8_t {
    none, invalid_buffer, address_overflow, size_limit, overlapping_buffers,
    truncated_signature, unsupported_signature, gzip_failure, dds_failure
};
struct TextureResourceRequest {
    const void* source{};
    std::size_t source_bytes{};
    std::uint32_t mip_index{};
    void* rgba{};
    std::size_t rgba_capacity{}, rgba_row_pitch{};
    // Required only for gzip. Holds the complete verified uncompressed DDS.
    void* inflate_buffer{};
    std::size_t inflate_capacity{};
};
struct TextureResourceStatus {
    TextureResourceError error{TextureResourceError::none};
    TextureResourceKind kind{TextureResourceKind::unknown};
    bool gzip_attempted{}, dds_attempted{}, dds_parsed{}, output_valid{};
    GzipStatus gzip_status{};
    DdsStatus dds_status{};
    std::size_t source_consumed{}, inflated_bytes{};
    std::size_t rgba_bytes{}, rgba_span{}; // Samples written versus span including row padding.
    DdsMetadata metadata{}; // Valid when dds_parsed, even if mip/output validation fails.
    DdsMip mip{}; // Valid only with output_valid.
    explicit operator bool() const noexcept { return error == TextureResourceError::none && output_valid; }
};

// Recognizes only exact DDS magic or RFC1952 gzip magic. Gzip must decode and
// verify completely before DDS parsing. No zlib/raw-deflate guessing or nesting.
// All storage is caller-owned; no heap/files/SDL/device operations. Source stays
// immutable. Full declared mutable ranges must be separate from source, request
// and each other (including inflate workspace). Range checks precede writes.
// Raw DDS ignores inflate_buffer/workspace. Gzip failure may alter scratch and
// workspace, never authenticates RGBA output. output_valid is false on ANY error;
// old RGBA contents are not the result of this call and are not cleared.
// Metadata retains sRGB/unspecified transfer and premultiplied/custom alpha;
// callers must use compatible presentation/blending. No implicit conversion.
TextureResourceStatus decode_texture_resource(const TextureResourceRequest& request,
                                               GzipWorkspace& workspace) noexcept;
} // namespace cedar
