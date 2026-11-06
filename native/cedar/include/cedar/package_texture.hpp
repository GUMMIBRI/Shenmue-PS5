// Original bounded composition of compatible PKF, gzip, DDS and PVR facilities.
#pragma once
#include "cedar/dds.hpp"
#include "cedar/gzip_decode.hpp"
#include "cedar/pkf.hpp"
#include "cedar/pvr.hpp"
#include "cedar/pvr_dds.hpp"

namespace cedar {
enum class PackageTextureError : std::uint8_t {
    none, invalid_buffer, address_overflow, size_limit, overlapping_buffers,
    truncated_signature, unsupported_signature, gzip_failure, pkf_failure,
    unsupported_texture, unsupported_mip, dds_failure, pvr_failure, pvr_dds_failure
};
enum class PackageTextureKind : std::uint8_t { unknown, dds, pvr };
struct PackageTextureRequest {
    const void* source{};
    std::size_t source_bytes{};
    std::uint32_t texture_index{};
    std::uint32_t mip_index{}; // PVR's supported nonmipped profile requires zero.
    void* rgba{};
    std::size_t rgba_capacity{}, rgba_row_pitch{};
    void* inflate_buffer{}; // Required only for gzip-wrapped PKF.
    std::size_t inflate_capacity{};
};
struct PackageTextureStatus {
    PackageTextureError error{PackageTextureError::none};
    PackageTextureKind kind{PackageTextureKind::unknown};
    bool gzip_attempted{}, pkf_attempted{}, pkf_parsed{}, texture_selected{};
    bool dds_attempted{}, dds_parsed{}, pvr_attempted{}, pvr_parsed{}, output_valid{};
    bool pvr_dds_attempted{}, pvr_dds_parsed{};
    GzipStatus gzip_status{};
    PkfStatus pkf_status{};
    DdsStatus dds_status{};
    PvrStatus pvr_status{};
    PvrDdsStatus pvr_dds_status{};
    PvrDdsMetadata pvr_dds_metadata{};
    PkfHeader package_header{}; // Valid when pkf_parsed.
    std::uint8_t texture_id[8]{}; // Exact opaque TEXN ID; valid when texture_selected.
    std::size_t source_bytes{}, source_consumed{}, inflated_bytes{}, package_bytes{};
    std::size_t texture_bytes{}, texture_consumed{}, rgba_bytes{}, rgba_span{};
    DdsMetadata dds_metadata{}; // Valid when dds_parsed; transfer/alpha preserved.
    DdsMip dds_mip{}; // Valid only for successful DDS output.
    PvrMetadata pvr_metadata{}; // Valid when pvr_parsed; origin/transfer remain unspecified.
    explicit operator bool() const noexcept { return error == PackageTextureError::none && output_valid; }
};
constexpr std::size_t package_texture_input_limit = 256u * 1024u * 1024u;

// Exact plain PAKF or verified RFC1952 gzip -> PAKF, then explicit TEXN index.
// At that exact bounded body start only DDS, PVRT, or fixed-profile GBIX is
// accepted. No signature scan, offset/format fallback or nested gzip guess.
// Caller owns immutable source, mutable RGBA and (gzip only) inflate storage.
// Full relevant mutable ranges/workspace/request/source must be disjoint and
// cannot wrap addresses. No heap, file operations, SDL, extraction or rendering.
// Metadata is tagged by kind and stage flags, never silently converted. Any
// failure leaves output_valid=false; prior RGBA contents are not a new result.
PackageTextureStatus decode_package_texture(const PackageTextureRequest& request,
                                             GzipWorkspace& workspace) noexcept;
} // namespace cedar
