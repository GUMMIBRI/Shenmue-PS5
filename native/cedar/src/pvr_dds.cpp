// Independent transport view informed by MIT ShenmueDK at
// 1dae443814b3755281d207a13f25571c0e51a5d1, Copyright (c) 2019 Phil.
// See docs/PVR-DDS-SCOPE.md and third_party/licenses/shenmuedk-MIT.txt.
#include "cedar/pvr_dds.hpp"
#include <limits>

namespace cedar {
namespace {
PvrDdsStatus fail(PvrDdsError error, std::size_t offset = 0) noexcept { return {error, offset}; }
std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
} // namespace

PvrDdsStatus PvrDdsView::parse(const void* resource, std::size_t bytes) noexcept {
    *this = PvrDdsView{};
    if (!resource) return fail(PvrDdsError::invalid_buffer);
    if (bytes > max_resource_bytes) return fail(PvrDdsError::resource_limit);
    const auto address = reinterpret_cast<std::uintptr_t>(resource);
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - address)
        return fail(PvrDdsError::invalid_buffer);
    if (bytes < 4) return fail(PvrDdsError::truncated, bytes);
    const auto* p = static_cast<const std::uint8_t*>(resource);
    PvrDdsMetadata info;
    std::size_t offset = 0;
    if (le32(p) == 0x58494247u) { // Fixed12 GBIX profile established by reader.
        if (bytes < 12) return fail(PvrDdsError::truncated, bytes);
        info.has_gbix = true;
        info.gbix_declared_size = le32(p + 4);
        info.global_index = le32(p + 8);
        offset = 12;
    }
    if (offset > bytes || bytes - offset < 16) return fail(PvrDdsError::truncated, bytes);
    if (le32(p + offset) != 0x54525650u) return fail(PvrDdsError::invalid_signature, offset);
    info.pvrt_offset = offset;
    info.declared_size = le32(p + offset + 4);
    info.pixel_format = p[offset + 8];
    info.data_format = p[offset + 9];
    info.reserved = le16(p + offset + 10);
    info.width = le16(p + offset + 12);
    info.height = le16(p + offset + 14);
    const bool dds_pixel = info.pixel_format == 0x80 || info.pixel_format == 0x81;
    const bool dds_data = info.data_format == 0x80 || info.data_format == 0x87;
    if (!dds_pixel && !dds_data) return fail(PvrDdsError::not_dds_profile, offset + 8);
    // One DDS hint must not be discarded as a normal-PVR fallback. Requiring
    // both fields narrows support to the compatible DDS reader/writer profile.
    if (!dds_pixel) return fail(PvrDdsError::unsupported_pixel_format, offset + 8);
    if (!dds_data) return fail(PvrDdsError::unsupported_layout, offset + 9);
    info.dds_offset = offset + 16;
    info.dds_span_bytes = bytes - info.dds_offset;
    if (info.dds_span_bytes < 4) return fail(PvrDdsError::truncated, bytes);
    if (le32(p + info.dds_offset) != 0x20534444u)
        return fail(PvrDdsError::invalid_embedded_signature, info.dds_offset);
    info.declared_size_matches_dds_span = info.declared_size == info.dds_span_bytes;
    metadata_ = info;
    dds_ = {p + info.dds_offset, info.dds_span_bytes};
    return {};
}
} // namespace cedar
