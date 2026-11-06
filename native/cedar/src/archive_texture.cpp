#include "cedar/archive_texture.hpp"
#include <limits>

namespace cedar {
namespace {
struct Range { std::uintptr_t start{}, end{}; };
bool range(const void* p, std::size_t size, Range& result) noexcept {
    if (!p && size) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(p);
    if (size > std::numeric_limits<std::uintptr_t>::max() - start) return false;
    result = {start, start + size};
    return true;
}
bool overlap(Range a, Range b) noexcept {
    return a.start < a.end && b.start < b.end && a.start < b.end && b.start < a.end;
}
}
ArchiveTextureStatus load_archive_texture(const ArchiveSet& archives,
    const ArchiveTextureRequest& request, GzipWorkspace& workspace) noexcept {
    ArchiveTextureStatus result;
    if (!request.stored || !request.rgba ||
        (request.mode != ArchiveTextureMode::dds_resource && request.mode != ArchiveTextureMode::pkf_texture)) {
        result.error = ArchiveTextureError::invalid_request; return result;
    }
    Range mutable_ranges[4], args, set;
    if (!range(request.stored, request.stored_capacity, mutable_ranges[0]) ||
        !range(request.rgba, request.rgba_capacity, mutable_ranges[1]) ||
        !range(request.inflate_buffer, request.inflate_capacity, mutable_ranges[2]) ||
        !range(&workspace, sizeof(workspace), mutable_ranges[3]) ||
        !range(&request, sizeof(request), args) || !range(&archives, sizeof(archives), set)) {
        result.error = ArchiveTextureError::invalid_request; return result;
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (overlap(mutable_ranges[i], args) || overlap(mutable_ranges[i], set)) {
            result.error = ArchiveTextureError::overlapping_buffers; return result;
        }
        for (unsigned j = 0; j < i; ++j)
            if (overlap(mutable_ranges[i], mutable_ranges[j])) {
                result.error = ArchiveTextureError::overlapping_buffers; return result;
            }
    }
    TadEntry entry;
    result.archive_status = archives.stat_hash(request.hash1, request.hash2, entry);
    if (!result.archive_status) { result.error = ArchiveTextureError::archive_failure; return result; }
    if (entry.size > PosixReadOnlyAssets::max_read_bytes || entry.size > request.stored_capacity) {
        result.error = ArchiveTextureError::stored_size_limit; return result;
    }
    const auto length = static_cast<std::size_t>(entry.size);
    result.archive_status = archives.read_hash(request.hash1, request.hash2, 0, request.stored, length);
    result.stored_bytes_read = result.archive_status.bytes_transferred;
    if (!result.archive_status) { result.error = ArchiveTextureError::archive_failure; return result; }
    std::uint32_t width{}, height{};
    std::size_t span{};
    if (request.mode == ArchiveTextureMode::dds_resource) {
        TextureResourceRequest texture;
        texture.source = request.stored;
        texture.source_bytes = length;
        texture.mip_index = request.mip_index;
        texture.rgba = request.rgba;
        texture.rgba_capacity = request.rgba_capacity;
        texture.rgba_row_pitch = request.rgba_row_pitch;
        texture.inflate_buffer = request.inflate_buffer;
        texture.inflate_capacity = request.inflate_capacity;
        result.dds_status = decode_texture_resource(texture, workspace);
        if (!result.dds_status) { result.error = ArchiveTextureError::dds_resource_failure; return result; }
        width = result.dds_status.mip.width;
        height = result.dds_status.mip.height;
        span = result.dds_status.rgba_span;
    } else {
        PackageTextureRequest texture;
        texture.source = request.stored;
        texture.source_bytes = length;
        texture.texture_index = request.texture_index;
        texture.mip_index = request.mip_index;
        texture.rgba = request.rgba;
        texture.rgba_capacity = request.rgba_capacity;
        texture.rgba_row_pitch = request.rgba_row_pitch;
        texture.inflate_buffer = request.inflate_buffer;
        texture.inflate_capacity = request.inflate_capacity;
        result.package_status = decode_package_texture(texture, workspace);
        if (!result.package_status) { result.error = ArchiveTextureError::package_texture_failure; return result; }
        if (result.package_status.kind == PackageTextureKind::dds) {
            width = result.package_status.dds_mip.width;
            height = result.package_status.dds_mip.height;
        } else {
            width = result.package_status.pvr_metadata.width;
            height = result.package_status.pvr_metadata.height;
        }
        span = result.package_status.rgba_span;
    }
    result.surface = {static_cast<const std::uint8_t*>(request.rgba), span,
                      request.rgba_row_pitch, width, height, PixelFormat::rgba8};
    if (validate_surface(result.surface) != Status::ok) {
        result.surface = {};
        result.error = ArchiveTextureError::invalid_request;
        return result;
    }
    result.output_valid = true;
    return result;
}
} // namespace cedar
