// SPDX-License-Identifier: GPL-3.0-only
// Source: brynnb/new-yokosuka b0df7f2720a7eecabce2e610fdedec35bf1243e3,
// tools/scripting/extract_dialogue_inventory.py parse_afs. Complete GPL retained.
#include "cedar/afs.hpp"
#include <limits>

namespace cedar {
namespace {
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
bool valid_buffer(const void* p, std::size_t n) noexcept {
    return p && n <= std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(p);
}
bool separate(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!valid_buffer(a, an) || !valid_buffer(b, bn)) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x + an <= y || y + bn <= x;
}
}
AfsStatus AfsIndex::parse(const void* data, std::size_t size, std::uint64_t archive) noexcept {
    table_ = nullptr; count_ = 0; archive_bytes_ = maximum_end_ = 0;
    if (!valid_buffer(data, size) || !separate(data, size, this, sizeof(*this)))
        return {AfsError::invalid_buffer, 0};
    if (size < 8 || archive < 8) return {AfsError::truncated, 0};
    const auto* p = static_cast<const std::uint8_t*>(data);
    if (p[0] != 'A' || p[1] != 'F' || p[2] != 'S' || p[3] != 0)
        return {AfsError::invalid_signature, 0};
    const auto count = u32(p + 4);
    if (count > max_members) return {AfsError::invalid_count, 4};
    const std::size_t end = 8 + std::size_t(count) * 8;
    if (end > size || end > archive) return {AfsError::truncated, 8};
    std::uint64_t maximum = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 8 + std::size_t(i) * 8;
        const std::uint64_t offset = u32(p + at), length = u32(p + at + 4);
        if (offset > archive || length > archive - offset || (length && offset < end))
            return {AfsError::invalid_extent, at};
        if (offset + length > maximum) maximum = offset + length;
    }
    table_ = p; count_ = count; archive_bytes_ = archive; maximum_end_ = maximum;
    return {};
}
AfsStatus AfsIndex::member(std::uint32_t index, AfsMember& output) const noexcept {
    if (!parsed()) return {AfsError::not_parsed, 0};
    if (index >= count_) return {AfsError::invalid_index, index};
    if (!separate(&output, sizeof(output), this, sizeof(*this)) ||
        !separate(&output, sizeof(output), table_, 8 + std::size_t(count_) * 8))
        return {AfsError::invalid_buffer, 0};
    const auto* p = table_ + 8 + std::size_t(index) * 8;
    output = {index, u32(p), u32(p + 4)};
    return {};
}
AfsStatus AfsIndex::directory_candidate(AfsDirectory& output) const noexcept {
    if (!parsed()) return {AfsError::not_parsed, 0};
    if (!separate(&output, sizeof(output), this, sizeof(*this)) ||
        !separate(&output, sizeof(output), table_, 8 + std::size_t(count_) * 8))
        return {AfsError::invalid_buffer, 0};
    const auto offset = (maximum_end_ + 0x7ffu) & ~std::uint64_t(0x7ffu);
    const auto length = std::uint64_t(count_) * directory_entry_bytes;
    if (!count_ || offset < 8 + std::uint64_t(count_) * 8 || offset > archive_bytes_ || length > archive_bytes_ - offset)
        return {AfsError::directory_unavailable, 0};
    output = {offset, length};
    return {};
}
AfsStatus AfsIndex::raw_name(const void* directory, std::size_t size,
    std::uint32_t index, const std::uint8_t*& output) const noexcept {
    if (!parsed()) return {AfsError::not_parsed, 0};
    if (index >= count_) return {AfsError::invalid_index, index};
    const auto expected = std::size_t(count_) * directory_entry_bytes;
    if (!valid_buffer(directory, size) || !separate(&output, sizeof(output), this, sizeof(*this)) ||
        !separate(&output, sizeof(output), directory, size) ||
        !separate(&output, sizeof(output), table_, 8 + std::size_t(count_) * 8))
        return {AfsError::invalid_buffer, 0};
    if (size != expected) return {AfsError::truncated, 0};
    output = static_cast<const std::uint8_t*>(directory) + std::size_t(index) * directory_entry_bytes;
    return {};
}
} // namespace cedar
