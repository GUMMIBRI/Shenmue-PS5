#include "cedar/pkf.hpp"

#include <cstring>

namespace cedar {
namespace {
constexpr std::uint32_t pakf = 0x464b4150u;
constexpr std::uint32_t dumy = 0x594d5544u;
constexpr std::uint32_t texn = 0x4e584554u;
constexpr std::uint32_t ipac = 0x43415049u;
std::uint32_t word32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

PkfStatus texture_at(const std::uint8_t* data, std::size_t offset,
                      std::size_t end, std::uint32_t index,
                      PkfTextureView& result) noexcept {
    result = {};
    if (offset > end || end - offset < 8) return {PkfError::truncated, index};
    if (word32(data + offset) != texn) return {PkfError::unsupported_node, index};
    const auto size = word32(data + offset + 4);
    // Eight-byte node header, followed by the compatible eight-byte TextureID.
    if (size < 16) return {PkfError::malformed_node, index};
    if (size > end - offset) return {PkfError::truncated, index};
    result.index = index;
    result.node_offset = offset;
    result.node_size = size;
    std::memcpy(result.texture_id, data + offset + 8, sizeof(result.texture_id));
    result.texture_body = {data + offset + 16, size - 16u};
    return {};
}
}

PkfStatus PkfView::parse(const void* bytes, std::size_t length) noexcept {
    data_ = nullptr;
    length_ = 0;
    header_ = {};
    first_texture_ = textures_end_ = dummy_size_ = 0;
    if (!bytes) return {PkfError::invalid_buffer};
    if (length < header_bytes) return {PkfError::truncated};
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    if (word32(data) != pakf) return {PkfError::unknown_signature};
    const PkfHeader candidate{word32(data + 4), word32(data + 8), word32(data + 12)};
    if (candidate.content_size < header_bytes) return {PkfError::malformed_node};
    if (candidate.content_size > length) return {PkfError::truncated};
    if (candidate.texture_count > max_textures) return {PkfError::too_many_textures};
    std::size_t cursor = header_bytes;
    std::size_t dummy_size = 0;
    if (candidate.content_size - cursor >= 4 && word32(data + cursor) == dumy) {
        if (candidate.content_size - cursor < 8) return {PkfError::truncated};
        dummy_size = word32(data + cursor + 4);
        if (dummy_size < 8) return {PkfError::malformed_node};
        if (dummy_size > candidate.content_size - cursor) return {PkfError::truncated};
        cursor += dummy_size;
    }
    const auto first_texture = cursor;
    if (candidate.texture_count > (candidate.content_size - cursor) / 16)
        return {PkfError::truncated};
    for (std::uint32_t index = 0; index < candidate.texture_count; ++index) {
        PkfTextureView texture;
        const auto status = texture_at(data, cursor, candidate.content_size, index, texture);
        if (!status) return status;
        cursor += texture.node_size;
    }

    // The compatible PKF reader seeks to exactly contentSize for optional IPAC.
    // Do not scan elsewhere or infer padding signatures from arbitrary bytes.
    if (length > candidate.content_size) {
        const auto trailing = length - candidate.content_size;
        if (trailing < 4) return {PkfError::truncated};
        if (word32(data + candidate.content_size) != ipac)
            return {PkfError::unsupported_trailing_data};
        IpacIndex index;
        const auto status = index.parse(data + candidate.content_size, trailing);
        if (!status) return {PkfError::ipac_failure, UINT32_MAX, status};
    }
    data_ = data;
    length_ = length;
    header_ = candidate;
    first_texture_ = first_texture;
    textures_end_ = cursor;
    dummy_size_ = dummy_size;
    return {};
}

PkfView::Cursor PkfView::textures() const noexcept {
    Cursor cursor;
    cursor.data_ = data_;
    cursor.offset_ = first_texture_;
    cursor.content_end_ = header_.content_size;
    cursor.count_ = header_.texture_count;
    return cursor;
}

PkfStatus PkfView::Cursor::next(PkfTextureView& texture) noexcept {
    texture = {};
    if (!data_) return {PkfError::not_parsed};
    if (index_ >= count_) return {PkfError::end_of_textures};
    const auto status = texture_at(data_, offset_, content_end_, index_, texture);
    if (!status) return status;
    offset_ += texture.node_size;
    ++index_;
    return {};
}

PkfStatus PkfView::texture(std::uint32_t index, PkfTextureView& result) const noexcept {
    result = {};
    if (!parsed()) return {PkfError::not_parsed};
    if (index >= header_.texture_count) return {PkfError::invalid_texture_index, index};
    auto cursor = textures();
    for (std::uint32_t current = 0; current <= index; ++current) {
        const auto status = cursor.next(result);
        if (!status) return status;
    }
    return {};
}

PkfStatus PkfView::optional_ipac(IpacIndex& result) const noexcept {
    result = {};
    if (!parsed()) return {PkfError::not_parsed};
    if (length_ == header_.content_size) return {PkfError::no_ipac};
    const auto status = result.parse(data_ + header_.content_size, length_ - header_.content_size);
    if (!status) return {PkfError::ipac_failure, UINT32_MAX, status};
    return {};
}

PkfBytes PkfView::dummy_body() const noexcept {
    if (!parsed() || !dummy_size_) return {};
    return {data_ + header_bytes + 8, dummy_size_ - 8};
}

PkfBytes PkfView::content_remainder() const noexcept {
    if (!parsed()) return {};
    return {data_ + textures_end_, header_.content_size - textures_end_};
}

} // namespace cedar
