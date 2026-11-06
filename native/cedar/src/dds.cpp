#include "cedar/dds.hpp"
#include <limits>

namespace cedar {
namespace {
DdsStatus failure(DdsError error, std::size_t offset = 0) noexcept { return {error, offset}; }
std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
constexpr std::uint32_t fourcc(char a, char b, char c, char d) noexcept {
    return static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) |
        (static_cast<std::uint32_t>(c) << 16) | (static_cast<std::uint32_t>(d) << 24);
}
bool multiply(std::size_t a, std::size_t b, std::size_t& result) noexcept {
    if (b && a > std::numeric_limits<std::size_t>::max() / b) return false;
    result = a * b;
    return true;
}
bool compressed(DdsFormat f) noexcept {
    return f == DdsFormat::bc1 || f == DdsFormat::bc2 || f == DdsFormat::bc3;
}
bool dxgi_format(std::uint32_t n, DdsMetadata& info) noexcept {
    switch (n) {
    case 28: case 29: info.format = DdsFormat::rgba8; break;
    case 87: case 91: info.format = DdsFormat::bgra8; break;
    case 71: case 72: info.format = DdsFormat::bc1; break;
    case 74: case 75: info.format = DdsFormat::bc2; break;
    case 77: case 78: info.format = DdsFormat::bc3; break;
    default: return false;
    }
    info.color_space = (n == 29 || n == 91 || n == 72 || n == 75 || n == 78) ?
        DdsColorSpace::srgb : DdsColorSpace::linear;
    info.stored_alpha = true;
    return true;
}

// Keep RGB565 endpoints as their exact 5/6-bit normalized values. Only quantize
// to eight bits after color interpolation, avoiding premature rounding drift.
void colors(const std::uint8_t* encoded, bool bc1, std::uint8_t (&palette)[4][4]) noexcept {
    const unsigned endpoint0 = le16(encoded), endpoint1 = le16(encoded + 2);
    const bool four_colors = !bc1 || endpoint0 > endpoint1;
    const unsigned channel0[3]{(endpoint0 >> 11) & 31u, (endpoint0 >> 5) & 63u, endpoint0 & 31u};
    const unsigned channel1[3]{(endpoint1 >> 11) & 31u, (endpoint1 >> 5) & 63u, endpoint1 & 31u};
    for (unsigned c = 0; c < 3; ++c) {
        const unsigned max = c == 1 ? 63 : 31;
        palette[0][c] = static_cast<std::uint8_t>((channel0[c] * 255 + max / 2) / max);
        palette[1][c] = static_cast<std::uint8_t>((channel1[c] * 255 + max / 2) / max);
        if (four_colors) {
            const unsigned divisor = 3 * max;
            palette[2][c] = static_cast<std::uint8_t>(((2 * channel0[c] + channel1[c]) * 255 + divisor / 2) / divisor);
            palette[3][c] = static_cast<std::uint8_t>(((channel0[c] + 2 * channel1[c]) * 255 + divisor / 2) / divisor);
        } else {
            const unsigned divisor = 2 * max;
            palette[2][c] = static_cast<std::uint8_t>(((channel0[c] + channel1[c]) * 255 + divisor / 2) / divisor);
            palette[3][c] = 0;
        }
    }
    palette[0][3] = palette[1][3] = palette[2][3] = 255;
    palette[3][3] = four_colors ? 255 : 0;
}
void block(const std::uint8_t* src, DdsFormat format, std::uint8_t (&rgba)[16][4]) noexcept {
    const bool bc1 = format == DdsFormat::bc1;
    const auto* color = src + (bc1 ? 0 : 8);
    std::uint8_t palette[4][4]{};
    colors(color, bc1, palette);
    const std::uint32_t indices = le32(color + 4);
    for (unsigned p = 0; p < 16; ++p) {
        const unsigned index = (indices >> (2 * p)) & 3u;
        for (unsigned c = 0; c < 4; ++c) rgba[p][c] = palette[index][c];
    }
    if (format == DdsFormat::bc2) {
        for (unsigned p = 0; p < 16; ++p)
            rgba[p][3] = static_cast<std::uint8_t>(((src[p / 2] >> (4 * (p & 1u))) & 15u) * 17);
    } else if (format == DdsFormat::bc3) {
        unsigned alpha[8]{src[0], src[1], 0, 0, 0, 0, 0, 0};
        if (alpha[0] > alpha[1]) {
            for (unsigned i = 2; i < 8; ++i) alpha[i] = ((8 - i) * alpha[0] + (i - 1) * alpha[1] + 3) / 7;
        } else {
            for (unsigned i = 2; i < 6; ++i) alpha[i] = ((6 - i) * alpha[0] + (i - 1) * alpha[1] + 2) / 5;
            alpha[6] = 0;
            alpha[7] = 255;
        }
        std::uint64_t alpha_indices = 0;
        for (unsigned i = 0; i < 6; ++i) alpha_indices |= static_cast<std::uint64_t>(src[i + 2]) << (8 * i);
        for (unsigned p = 0; p < 16; ++p) rgba[p][3] = static_cast<std::uint8_t>(alpha[(alpha_indices >> (3 * p)) & 7u]);
    }
}
} // namespace

DdsStatus DdsView::parse(const void* bytes, std::size_t size) noexcept {
    *this = DdsView{};
    if (!bytes) return failure(DdsError::invalid_buffer);
    if (size > max_file_bytes) return failure(DdsError::resource_limit);
    if (size < 128) return failure(DdsError::truncated, size);
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    if (le32(p) != fourcc('D', 'D', 'S', ' ') || le32(p + 4) != 124 || le32(p + 76) != 32)
        return failure(DdsError::invalid_header);
    DdsMetadata info;
    info.width = le32(p + 16);
    info.height = le32(p + 12);
    if (!info.width || !info.height) return failure(DdsError::invalid_header, 12);
    if (info.width > max_dimension || info.height > max_dimension) return failure(DdsError::resource_limit, 12);
    const auto flags = le32(p + 8);
    const auto caps2 = le32(p + 112);
    if ((flags & 0x00800000u) || le32(p + 24) > 1 || (caps2 & 0x0020fe00u))
        return failure(DdsError::unsupported_dimension, 24);
    if (caps2 != 0) return failure(DdsError::unsupported_layout, 112);
    info.mip_count = le32(p + 28);
    if (!info.mip_count) info.mip_count = 1;
    std::uint32_t possible_mips = 1;
    for (auto dimension = info.width > info.height ? info.width : info.height; dimension > 1; dimension >>= 1) ++possible_mips;
    if (info.mip_count > max_mips || info.mip_count > possible_mips)
        return failure(DdsError::invalid_header, 28);
    const auto pixel_flags = le32(p + 80);
    std::size_t offset = 128;
    if (pixel_flags & 4u) {
        if (pixel_flags & ~5u) return failure(DdsError::unsupported_format, 80);
        const auto code = le32(p + 84);
        if (code == fourcc('D', 'X', '1', '0')) {
            if (size < 148) return failure(DdsError::truncated, size);
            if (le32(p + 132) != 3 || le32(p + 140) != 1 || (le32(p + 136) & 4u))
                return failure(DdsError::unsupported_dimension, 132);
            if (le32(p + 136) != 0) return failure(DdsError::unsupported_layout, 136);
            const auto alpha = le32(p + 144);
            if (alpha > 4) return failure(DdsError::unsupported_layout, 144);
            info.alpha = static_cast<DdsAlphaMode>(alpha);
            info.has_dx10_header = true;
            if (!dxgi_format(le32(p + 128), info)) return failure(DdsError::unsupported_format, 128);
            offset = 148;
        } else {
            if (code == fourcc('D', 'X', 'T', '1')) info.format = DdsFormat::bc1;
            else if (code == fourcc('D', 'X', 'T', '3')) info.format = DdsFormat::bc2;
            else if (code == fourcc('D', 'X', 'T', '5')) info.format = DdsFormat::bc3;
            else return failure(DdsError::unsupported_format, 84);
            info.stored_alpha = true;
        }
    } else {
        if ((pixel_flags & ~0x41u) || !(pixel_flags & 0x40u) || le32(p + 88) != 32)
            return failure(DdsError::unsupported_format, 80);
        const auto red = le32(p + 92), green = le32(p + 96), blue = le32(p + 100), alpha = le32(p + 104);
        if (green != 0x0000ff00u) return failure(DdsError::unsupported_format, 96);
        if (red == 0x000000ffu && blue == 0x00ff0000u) info.format = DdsFormat::rgba8;
        else if (red == 0x00ff0000u && blue == 0x000000ffu) info.format = DdsFormat::bgra8;
        else return failure(DdsError::unsupported_format, 92);
        info.stored_alpha = (pixel_flags & 1u) != 0;
        if ((info.stored_alpha && alpha != 0xff000000u) || (!info.stored_alpha && alpha != 0))
            return failure(DdsError::unsupported_format, 104);
        if (!info.stored_alpha) info.alpha = DdsAlphaMode::opaque;
    }
    const bool bc = compressed(info.format);
    if ((bc && (flags & 8u)) || (!bc && (flags & 0x00080000u)))
        return failure(DdsError::unsupported_layout, 8);
    DdsMip parsed[max_mips]{};
    auto width = info.width, height = info.height;
    for (std::uint32_t i = 0; i < info.mip_count; ++i) {
        auto& level = parsed[i];
        level.width = width;
        level.height = height;
        level.offset = offset;
        std::size_t output_row;
        if (!multiply(width, 4, output_row) || !multiply(output_row, height, level.rgba_bytes) ||
            level.rgba_bytes > max_decoded_bytes) return failure(DdsError::resource_limit, offset);
        if (bc) {
            const std::size_t block_bytes = info.format == DdsFormat::bc1 ? 8 : 16;
            if (!multiply((width + 3u) / 4u, block_bytes, level.row_pitch) ||
                !multiply(level.row_pitch, (height + 3u) / 4u, level.size))
                return failure(DdsError::resource_limit, offset);
        } else {
            level.row_pitch = output_row;
            if (i == 0 && (flags & 8u)) {
                const auto declared_pitch = le32(p + 20);
                if (declared_pitch < output_row) return failure(DdsError::invalid_header, 20);
                if (declared_pitch != output_row && info.mip_count > 1)
                    return failure(DdsError::unsupported_layout, 20);
                level.row_pitch = declared_pitch;
            }
            if (!multiply(level.row_pitch, height, level.size)) return failure(DdsError::resource_limit, offset);
        }
        if (offset > size || level.size > size - offset) return failure(DdsError::truncated, size);
        offset += level.size; // Proven bounded by size above.
        if (width > 1) width >>= 1;
        if (height > 1) height >>= 1;
    }
    info.consumed_bytes = offset;
    info.trailing_bytes = size - offset;
    data_ = p;
    size_ = size;
    metadata_ = info;
    for (std::uint32_t i = 0; i < info.mip_count; ++i) mips_[i] = parsed[i];
    return {};
}
DdsStatus DdsView::mip(std::uint32_t index, DdsMip& result) const noexcept {
    result = {};
    if (!valid() || index >= metadata_.mip_count) return failure(DdsError::invalid_mip);
    result = mips_[index];
    return {};
}
DdsStatus DdsView::decode_rgba(std::uint32_t index, void* output, std::size_t capacity,
                              std::size_t row_pitch) const noexcept {
    if (!valid() || index >= metadata_.mip_count) return failure(DdsError::invalid_mip);
    if (!output) return failure(DdsError::invalid_buffer);
    const auto& level = mips_[index];
    const std::size_t row_bytes = static_cast<std::size_t>(level.width) * 4;
    if (row_pitch < row_bytes) return failure(DdsError::output_too_small);
    std::size_t prior_rows;
    if (!multiply(row_pitch, level.height - 1u, prior_rows) ||
        prior_rows > std::numeric_limits<std::size_t>::max() - row_bytes)
        return failure(DdsError::resource_limit);
    const auto needed = prior_rows + row_bytes;
    if (needed > capacity) return failure(DdsError::output_too_small);
    const auto input_address = reinterpret_cast<std::uintptr_t>(data_);
    const auto output_address = reinterpret_cast<std::uintptr_t>(output);
    const auto max_address = std::numeric_limits<std::uintptr_t>::max();
    if (size_ > max_address - input_address || needed > max_address - output_address)
        return failure(DdsError::invalid_buffer);
    if (output_address < input_address + size_ && input_address < output_address + needed)
        return failure(DdsError::overlapping_buffers);
    auto* dst = static_cast<std::uint8_t*>(output);
    const auto* src = data_ + level.offset;
    const auto format = metadata_.format;
    if (!compressed(format)) {
        for (std::uint32_t y = 0; y < level.height; ++y) {
            const auto* source_row = src + static_cast<std::size_t>(y) * level.row_pitch;
            auto* dest_row = dst + static_cast<std::size_t>(y) * row_pitch;
            for (std::uint32_t x = 0; x < level.width; ++x) {
                const auto s = static_cast<std::size_t>(x) * 4;
                dest_row[s] = source_row[s + (format == DdsFormat::bgra8 ? 2 : 0)];
                dest_row[s + 1] = source_row[s + 1];
                dest_row[s + 2] = source_row[s + (format == DdsFormat::bgra8 ? 0 : 2)];
                dest_row[s + 3] = metadata_.stored_alpha ? source_row[s + 3] : 255;
            }
        }
    } else {
        const std::size_t block_bytes = format == DdsFormat::bc1 ? 8 : 16;
        for (std::uint32_t by = 0; by < (level.height + 3u) / 4u; ++by) {
            for (std::uint32_t bx = 0; bx < (level.width + 3u) / 4u; ++bx) {
                std::uint8_t decoded[16][4];
                block(src + static_cast<std::size_t>(by) * level.row_pitch + bx * block_bytes, format, decoded);
                for (unsigned y = 0; y < 4 && by * 4u + y < level.height; ++y) {
                    auto* row = dst + (by * 4u + y) * row_pitch;
                    for (unsigned x = 0; x < 4 && bx * 4u + x < level.width; ++x) {
                        auto* pixel = row + (bx * 4u + x) * 4u;
                        for (unsigned c = 0; c < 4; ++c) pixel[c] = decoded[y * 4 + x][c];
                    }
                }
            }
        }
    }
    return {};
}
} // namespace cedar
