// Independent bounded implementation informed by MIT ShenmueDK at
// 1dae443814b3755281d207a13f25571c0e51a5d1 (Copyright (c) 2019 Phil).
// Original bounds/view code; compatible format/scaling/twiddle derivation is
// attributed in docs/PVR-SCOPE.md. Full MIT notice is preserved alongside source.
#include "cedar/pvr.hpp"
#include <limits>

namespace cedar {
namespace {
PvrStatus failure(PvrError error, std::size_t offset = 0) noexcept { return {error, offset}; }
std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
bool multiply(std::size_t a, std::size_t b, std::size_t& result) noexcept {
    if (b && a > std::numeric_limits<std::size_t>::max() / b) return false;
    result = a * b;
    return true;
}
// The compatible createTwiddleMap spreads bits into positions 0,2,4,... . Its
// SquareTwiddled codec puts spread(x) in odd and spread(y) in even positions.
std::size_t spread(std::uint32_t coordinate) noexcept {
    std::size_t value = 0;
    for (unsigned bit = 0; coordinate; ++bit, coordinate >>= 1)
        value |= static_cast<std::size_t>(coordinate & 1u) << (2 * bit);
    return value;
}
void rgba16(unsigned word, PvrPixelFormat format, std::uint8_t* output) noexcept {
    if (format == PvrPixelFormat::argb4444) {
        output[0] = static_cast<std::uint8_t>(((word >> 8) & 15u) * 17);
        output[1] = static_cast<std::uint8_t>(((word >> 4) & 15u) * 17);
        output[2] = static_cast<std::uint8_t>((word & 15u) * 17);
        output[3] = static_cast<std::uint8_t>(((word >> 12) & 15u) * 17);
    } else {
        const bool rgb565 = format == PvrPixelFormat::rgb565;
        output[0] = static_cast<std::uint8_t>(((word >> (rgb565 ? 11 : 10)) & 31u) * 255 / 31);
        output[1] = static_cast<std::uint8_t>(((word >> 5) & (rgb565 ? 63u : 31u)) * 255 / (rgb565 ? 63 : 31));
        output[2] = static_cast<std::uint8_t>((word & 31u) * 255 / 31);
        output[3] = static_cast<std::uint8_t>(rgb565 || (word & 0x8000u) ? 255 : 0);
    }
}
bool range(const void* pointer, std::size_t bytes, std::uintptr_t& begin,
            std::uintptr_t& end) noexcept {
    begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    end = begin + bytes;
    return true;
}
} // namespace

PvrStatus PvrView::parse(const void* resource, std::size_t bytes) noexcept {
    *this = PvrView{};
    if (!resource) return failure(PvrError::invalid_buffer);
    if (bytes > max_file_bytes) return failure(PvrError::resource_limit);
    if (bytes < 4) return failure(PvrError::truncated, bytes);
    std::uintptr_t begin, end;
    if (!range(resource, bytes, begin, end)) return failure(PvrError::invalid_buffer);
    const auto* p = static_cast<const std::uint8_t*>(resource);
    PvrMetadata info;
    std::size_t offset = 0;
    if (le32(p) == 0x58494247u) { // GBIX: only the compatible fixed12-byte profile.
        if (bytes < 12) return failure(PvrError::truncated, bytes);
        info.has_gbix = true;
        info.gbix_declared_size = le32(p + 4);
        info.global_index = le32(p + 8);
        offset = 12;
    }
    if (offset > bytes || bytes - offset < 16) return failure(PvrError::truncated, bytes);
    if (le32(p + offset) != 0x54525650u) return failure(PvrError::invalid_signature, offset);
    info.pvrt_offset = offset;
    info.declared_size = le32(p + offset + 4);
    const auto pixel = p[offset + 8], layout = p[offset + 9];
    if (pixel > 2) return failure(PvrError::unsupported_pixel_format, offset + 8);
    if (layout != 1 && layout != 3 && layout != 9 && layout != 13)
        return failure(PvrError::unsupported_layout, offset + 9);
    info.pixel_format = static_cast<PvrPixelFormat>(pixel);
    info.layout = static_cast<PvrLayout>(layout);
    info.reserved = le16(p + offset + 10);
    info.width = le16(p + offset + 12);
    info.height = le16(p + offset + 14);
    if (!info.width || !info.height) return failure(PvrError::invalid_dimensions, offset + 12);
    if (info.width > max_dimension || info.height > max_dimension)
        return failure(PvrError::resource_limit, offset + 12);
    if ((layout == 1 || layout == 3) &&
        (info.width != info.height || (info.width & (info.width - 1u))))
        return failure(PvrError::unsupported_layout, offset + 12);
    if (layout == 13) {
        const auto side = info.width < info.height ? info.width : info.height;
        if ((side & (side - 1u)) || info.width % side || info.height % side)
            return failure(PvrError::unsupported_layout, offset + 12);
    }
    std::size_t pixels;
    if (!multiply(info.width, info.height, pixels) || !multiply(pixels, 2, info.pixel_bytes) ||
        !multiply(pixels, 4, info.rgba_bytes) || info.rgba_bytes > max_decoded_bytes)
        return failure(PvrError::resource_limit, offset + 12);
    info.pixel_offset = offset + 16;
    if (layout == 3) {
        // Compatible VQ uses 256 vectors of four16-bit texels. The square index
        // plane holds one8-bit vector selection per2x2 texels (one for1x1).
        info.codebook_offset = info.pixel_offset;
        info.codebook_bytes = 256u * 4u * 2u;
        info.index_offset = info.codebook_offset + info.codebook_bytes;
        info.index_bytes = pixels < 4 ? 1 : pixels / 4;
        if (info.index_bytes > std::numeric_limits<std::size_t>::max() - info.codebook_bytes)
            return failure(PvrError::resource_limit, info.pixel_offset);
        info.pixel_bytes = info.codebook_bytes + info.index_bytes;
    }
    if (info.pixel_bytes > bytes - info.pixel_offset) return failure(PvrError::truncated, bytes);
    info.consumed_bytes = info.pixel_offset + info.pixel_bytes;
    info.trailing_bytes = bytes - info.consumed_bytes;
    data_ = p;
    size_ = bytes;
    metadata_ = info;
    return {};
}
PvrStatus PvrView::decode_rgba(void* output, std::size_t capacity,
                              std::size_t row_pitch) const noexcept {
    if (!valid()) return failure(PvrError::not_parsed);
    if (!output) return failure(PvrError::invalid_buffer);
    const auto& info = metadata_;
    const std::size_t row_bytes = static_cast<std::size_t>(info.width) * 4;
    if (row_pitch < row_bytes) return failure(PvrError::output_too_small);
    std::size_t preceding_rows;
    if (!multiply(row_pitch, info.height - 1u, preceding_rows) ||
        preceding_rows > std::numeric_limits<std::size_t>::max() - row_bytes)
        return failure(PvrError::resource_limit);
    const auto required = preceding_rows + row_bytes;
    if (required > capacity) return failure(PvrError::output_too_small);
    std::uintptr_t source_begin, source_end, output_begin, output_end, view_begin, view_end;
    if (!range(data_, size_, source_begin, source_end) || !range(output, required, output_begin, output_end) ||
        !range(this, sizeof(*this), view_begin, view_end)) return failure(PvrError::invalid_buffer);
    if ((output_begin < source_end && source_begin < output_end) ||
        (output_begin < view_end && view_begin < output_end)) return failure(PvrError::overlapping_buffers);
    const auto* source = data_ + info.pixel_offset;
    auto* destination = static_cast<std::uint8_t*>(output);
    const bool twiddled = info.layout == PvrLayout::square_twiddled;
    const bool rectangle_twiddled = info.layout == PvrLayout::rectangle_twiddled;
    const bool vq = info.layout == PvrLayout::vector_quantized;
    const std::size_t tile_side = info.width < info.height ? info.width : info.height;
    const std::size_t tile_pixels = tile_side * tile_side;
    const std::size_t tile_columns = info.width / tile_side;
    for (std::uint32_t y = 0; y < info.height; ++y) {
        auto* row = destination + static_cast<std::size_t>(y) * row_pitch;
        const auto y_position = twiddled ? spread(y) : static_cast<std::size_t>(y) * info.width;
        for (std::uint32_t x = 0; x < info.width; ++x) {
            const std::uint8_t* encoded;
            if (vq) {
                const auto vector_address = (spread(x >> 1) << 1) | spread(y >> 1);
                const auto vector = data_[info.index_offset + vector_address];
                // Source VQ decoder advances y inside x: TL,BL,TR,BR.
                const auto component = ((x & 1u) << 1) | (y & 1u);
                const auto texel = static_cast<std::size_t>(vector) * 4 + component;
                encoded = data_ + info.codebook_offset + texel * 2;
            } else {
                std::size_t index;
                if (rectangle_twiddled) {
                    const auto tile = (y / tile_side) * tile_columns + x / tile_side;
                    index = tile * tile_pixels + ((spread(x % tile_side) << 1) | spread(y % tile_side));
                } else index = twiddled ? ((spread(x) << 1) | y_position) : y_position + x;
                encoded = source + index * 2;
            }
            rgba16(le16(encoded), info.pixel_format, row + static_cast<std::size_t>(x) * 4);
        }
    }
    return {};
}
} // namespace cedar
