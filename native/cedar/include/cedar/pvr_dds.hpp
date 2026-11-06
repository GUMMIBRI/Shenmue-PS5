// Independent MIT ShenmueDK-compatible DDS-in-PVRT transport boundary.
// Copyright (c) 2019 Phil; notice: third_party/licenses/shenmuedk-MIT.txt.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class PvrDdsError : std::uint8_t {
    none, not_dds_profile, invalid_buffer, truncated, invalid_signature,
    resource_limit, unsupported_pixel_format, unsupported_layout,
    invalid_embedded_signature, not_parsed
};
struct PvrDdsStatus {
    PvrDdsError error{PvrDdsError::none};
    std::size_t offset{};
    explicit operator bool() const noexcept { return error == PvrDdsError::none; }
};
struct PvrDdsBytes { const std::uint8_t* data{}; std::size_t size{}; };
struct PvrDdsMetadata {
    bool has_gbix{};
    std::uint32_t global_index{}, gbix_declared_size{}, declared_size{};
    std::uint16_t width{}, height{}, reserved{}; // Outer metadata, not DDS dimensions.
    std::uint8_t pixel_format{}, data_format{};
    std::size_t pvrt_offset{}, dds_offset{}, dds_span_bytes{};
    // Equality observation only. The writer stores DDS payload bytes here, but
    // the reader uses its remaining stream extent. No generic size rule assumed.
    bool declared_size_matches_dds_span{};
};

// Exact reader profile from MIT ShenmueDK revision1dae443...: PVRT header16 then
// complete DDS bytes; optionally its fixed12 GBIX prefix. No signature scan.
// Pixel0x80/0x81 and data0x80/0x87 are required. Size fields are exposed without
// using them as bounds; caller MUST supply a separately bounded exact resource.
// parse validates only the transport and DDS magic, not the DDS header/payload.
// The returned span must pass DdsView separately. Inner DDS dimensions, transfer
// and alpha govern output; outer values are retained without invented equality.
// Caller-owned resource memory must remain valid/immutable while using the view.
// No heap, file access, mutation, extraction, decoding or presentation.
class PvrDdsView final {
public:
    static constexpr std::size_t max_resource_bytes = 256u * 1024u * 1024u;
    PvrDdsStatus parse(const void* resource, std::size_t bytes) noexcept;
    bool valid() const noexcept { return dds_.data != nullptr; }
    const PvrDdsMetadata& metadata() const noexcept { return metadata_; }
    PvrDdsBytes dds_bytes() const noexcept { return dds_; }
private:
    PvrDdsMetadata metadata_{};
    PvrDdsBytes dds_{};
};
} // namespace cedar
