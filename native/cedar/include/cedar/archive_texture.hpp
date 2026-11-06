#pragma once
#include "cedar/archive_set.hpp"
#include "cedar/texture_resource.hpp"
#include "cedar/package_texture.hpp"
#include "cedar/platform.hpp"

namespace cedar {
enum class ArchiveTextureMode : std::uint8_t { dds_resource, pkf_texture };
enum class ArchiveTextureError : std::uint8_t {
    none, invalid_request, overlapping_buffers, archive_failure, stored_size_limit,
    dds_resource_failure, package_texture_failure
};
struct ArchiveTextureRequest {
    std::uint32_t hash1{}, hash2{};
    ArchiveTextureMode mode{ArchiveTextureMode::dds_resource};
    std::uint32_t texture_index{}, mip_index{};
    void* stored{};
    std::size_t stored_capacity{};
    void* inflate_buffer{};
    std::size_t inflate_capacity{};
    void* rgba{};
    std::size_t rgba_capacity{}, rgba_row_pitch{};
};
struct ArchiveTextureStatus {
    ArchiveTextureError error{ArchiveTextureError::none};
    ArchiveSetStatus archive_status{};
    TextureResourceStatus dds_status{};
    PackageTextureStatus package_status{};
    std::size_t stored_bytes_read{};
    bool output_valid{};
    SurfaceView surface{}; // Only valid after complete successful decoding.
    explicit operator bool() const noexcept { return error == ArchiveTextureError::none && output_valid; }
};
// Explicit hash pair/priority/resource type. Does not infer filename hashes,
// patch order, package texture IDs, color conversion or original game placement.
// Only index-selected stored bytes are read into caller memory; no extraction.
// All buffers and underlying archive/index objects must remain valid/disjoint
// during this synchronous operation. Mutable buffers cannot overlap each other,
// workspace, request, ArchiveSet or any backing indices/readers. Caller retains
// the decoded buffer as long as surface is used. Metadata remains in nested
// results for the caller's color/alpha handling; SurfaceView implies no transfer.
ArchiveTextureStatus load_archive_texture(const ArchiveSet& archives,
    const ArchiveTextureRequest& request, GzipWorkspace& workspace) noexcept;
} // namespace cedar
