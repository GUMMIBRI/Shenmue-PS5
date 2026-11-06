// Original bounded DDS reader/decoder, authored from published Microsoft formats.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class DdsError : std::uint8_t {
    none, invalid_buffer, truncated, invalid_header, resource_limit,
    unsupported_format, unsupported_dimension, unsupported_layout,
    invalid_mip, output_too_small, overlapping_buffers
};
struct DdsStatus {
    DdsError error{DdsError::none};
    std::size_t offset{}; // Header field or first missing/invalid payload byte.
    explicit operator bool() const noexcept { return error == DdsError::none; }
};
enum class DdsFormat : std::uint8_t { rgba8, bgra8, bc1, bc2, bc3 };
enum class DdsAlphaMode : std::uint8_t { unknown, straight, premultiplied, opaque, custom };
enum class DdsColorSpace : std::uint8_t { unspecified, linear, srgb };
struct DdsMetadata {
    std::uint32_t width{}, height{}, mip_count{};
    DdsFormat format{DdsFormat::rgba8};
    DdsAlphaMode alpha{DdsAlphaMode::unknown};
    DdsColorSpace color_space{DdsColorSpace::unspecified};
    bool has_dx10_header{};
    bool stored_alpha{};
    std::size_t consumed_bytes{}; // Bytes through the final declared mip.
    std::size_t trailing_bytes{}; // Extra bytes are not interpreted as surfaces.
};
struct DdsMip {
    std::uint32_t width{}, height{};
    std::size_t offset{}, size{}, row_pitch{}, rgba_bytes{};
};

// Non-owning, allocation-free parse of one 2D surface and its declared mips.
// Input memory must stay valid and immutable for this view's lifetime. Parsing
// never reads beyond size. Failed parse clears the previous view. No file access.
// Output retains encoded sRGB values and premultiplied/custom alpha semantics;
// it performs no color-space conversion, unpremultiplication or alpha override.
class DdsView final {
public:
    static constexpr std::uint32_t max_dimension = 16384;
    static constexpr std::uint32_t max_mips = 15;
    static constexpr std::size_t max_file_bytes = 256u * 1024u * 1024u;
    static constexpr std::size_t max_decoded_bytes = 64u * 1024u * 1024u;
    DdsStatus parse(const void* bytes, std::size_t size) noexcept;
    bool valid() const noexcept { return data_ != nullptr; }
    const DdsMetadata& metadata() const noexcept { return metadata_; }
    DdsStatus mip(std::uint32_t index, DdsMip& result) const noexcept;
    // RGBA byte output in row-major order. Padding bytes are untouched. Whole
    // input/output ranges must not overlap. Error checks precede output writes.
    // BC decoding crops 4x4 edge blocks to actual mip dimensions. RGB arithmetic
    // uses normalized RGB565 endpoints and nearest integer output; see scope.
    DdsStatus decode_rgba(std::uint32_t mip_index, void* output,
                          std::size_t capacity, std::size_t row_pitch) const noexcept;
private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    DdsMetadata metadata_{};
    DdsMip mips_[max_mips]{};
};
} // namespace cedar
