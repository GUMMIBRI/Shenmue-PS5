// SPDX-License-Identifier: GPL-3.0-only
#include "cedar/afs_reader.hpp"
#include <cstring>
#include <limits>

namespace cedar {
namespace {
struct Span { std::uintptr_t begin{}, end{}; };
bool span(const void* pointer, std::size_t size, Span& result) noexcept {
    if (!pointer && size) return false;
    result.begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (size > std::numeric_limits<std::uintptr_t>::max() - result.begin) return false;
    result.end = result.begin + size;
    return true;
}
bool disjoint(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    Span x{}, y{};
    return span(a, an, x) && span(b, bn, y) &&
        (!an || !bn || x.end <= y.begin || y.end <= x.begin);
}
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
AfsReaderStatus error(AfsReaderError code, AfsReaderStage stage) noexcept {
    AfsReaderStatus result{};
    result.error = code;
    result.stage = stage;
    return result;
}
} // namespace

void AfsReader::close() noexcept {
    assets_ = nullptr;
    table_ = nullptr;
    table_bytes_ = path_bytes_ = 0;
    archive_bytes_ = changed_archive_bytes_ = 0;
    archive_changed_ = false;
    index_ = {};
    path_[0] = '\0';
}
bool AfsReader::safe_output(const void* output, std::size_t length) const noexcept {
    Span unused{};
    return span(output, length, unused) && disjoint(output, length, this, sizeof(*this)) &&
        disjoint(output, length, table_, table_bytes_) &&
        (!assets_ || disjoint(output, length, assets_, sizeof(*assets_)));
}
AfsReaderStatus AfsReader::open(const PosixReadOnlyAssets& assets, std::string_view relative,
                               void* storage, std::size_t capacity) noexcept {
    close(); // Failed reopen never leaves an earlier reader/index usable.
    if (relative.empty() || relative.size() > PosixReadOnlyAssets::max_path_bytes ||
        !disjoint(relative.data(), relative.size(), this, sizeof(*this)))
        return error(AfsReaderError::invalid_path, AfsReaderStage::header);
    if (!safe_output(storage, capacity) || !disjoint(storage, capacity, &assets, sizeof(assets)) ||
        !disjoint(storage, capacity, relative.data(), relative.size()))
        return error(AfsReaderError::invalid_buffer, AfsReaderStage::table);
    char candidate_path[PosixReadOnlyAssets::max_path_bytes + 1]{};
    std::memcpy(candidate_path, relative.data(), relative.size());
    const std::string_view candidate(candidate_path, relative.size());
    PosixAssetInfo info{};
    AfsReaderStatus result{};
    result.stage = AfsReaderStage::metadata;
    result.assets_status = assets.stat(candidate, info); // Actual strict provider path validation.
    if (!result.assets_status) { result.error = AfsReaderError::assets_failure; return result; }
    result.archive_bytes = result.expected_archive_bytes = info.size;
    if (info.size < 8) {
        result.error = AfsReaderError::index_failure;
        result.index_status = {AfsError::truncated, 0};
        return result;
    }
    std::uint8_t header[8]{};
    result.stage = AfsReaderStage::header;
    result.assets_status = assets.read_exact(candidate, 0, header, sizeof(header));
    if (!result.assets_status) { result.error = AfsReaderError::assets_failure; return result; }
    if (header[0] != 'A' || header[1] != 'F' || header[2] != 'S' || header[3] != 0) {
        result.error = AfsReaderError::index_failure;
        result.index_status = {AfsError::invalid_signature, 0};
        return result;
    }
    const auto count = u32(header + 4);
    if (count > AfsIndex::max_members) {
        result.error = AfsReaderError::index_failure;
        result.index_status = {AfsError::invalid_count, 4};
        return result;
    }
    result.required_table_bytes = 8 + std::size_t(count) * 8;
    if (result.required_table_bytes > info.size) {
        result.error = AfsReaderError::index_failure;
        result.index_status = {AfsError::truncated, 8};
        return result;
    }
    result.stage = AfsReaderStage::table;
    if (capacity < result.required_table_bytes) { result.error = AfsReaderError::storage_too_small; return result; }
    result.assets_status = assets.read_exact(candidate, 0, storage, result.required_table_bytes);
    result.bytes_transferred = result.assets_status.bytes_transferred;
    if (!result.assets_status) { result.error = AfsReaderError::assets_failure; return result; }
    if (std::memcmp(storage, header, sizeof(header)) != 0) {
        result.error = AfsReaderError::archive_changed;
        return result;
    }
    PosixAssetInfo after{};
    result.assets_status = assets.stat(candidate, after);
    if (!result.assets_status) { result.error = AfsReaderError::assets_failure; return result; }
    result.archive_bytes = after.size;
    if (after.size != info.size) { result.error = AfsReaderError::archive_changed; return result; }
    AfsIndex parsed;
    result.stage = AfsReaderStage::index;
    result.index_status = parsed.parse(storage, result.required_table_bytes, after.size);
    if (!result.index_status) { result.error = AfsReaderError::index_failure; return result; }
    std::memcpy(path_, candidate_path, relative.size() + 1);
    path_bytes_ = relative.size();
    index_ = parsed;
    table_ = static_cast<const std::uint8_t*>(storage);
    table_bytes_ = result.required_table_bytes;
    archive_bytes_ = after.size;
    assets_ = &assets;
    return result;
}

AfsReaderStatus AfsReader::current_extent() const noexcept {
    if (!assets_) return error(AfsReaderError::not_open, AfsReaderStage::metadata);
    AfsReaderStatus result{};
    result.stage = AfsReaderStage::metadata;
    result.expected_archive_bytes = archive_bytes_;
    if (archive_changed_) {
        result.error = AfsReaderError::archive_changed;
        result.archive_bytes = changed_archive_bytes_;
        return result;
    }
    PosixAssetInfo info{};
    result.assets_status = assets_->stat(path(), info);
    if (!result.assets_status) { result.error = AfsReaderError::assets_failure; return result; }
    result.archive_bytes = info.size;
    if (info.size != archive_bytes_) {
        archive_changed_ = true;
        changed_archive_bytes_ = info.size;
        result.error = AfsReaderError::archive_changed;
    }
    return result;
}
AfsReaderStatus AfsReader::stat_member(std::uint32_t index, AfsMember& output) const noexcept {
    if (!safe_output(&output, sizeof(output))) return error(AfsReaderError::invalid_buffer, AfsReaderStage::member);
    output = {};
    auto result = current_extent();
    if (!result) return result;
    result.stage = AfsReaderStage::member;
    AfsMember candidate{};
    result.index_status = index_.member(index, candidate);
    if (!result.index_status) { result.error = AfsReaderError::index_failure; return result; }
    if (candidate.offset > result.archive_bytes || candidate.bytes > result.archive_bytes - candidate.offset) {
        result.error = AfsReaderError::member_range;
        return result;
    }
    output = candidate;
    return result;
}
AfsReaderStatus AfsReader::read_member(std::uint32_t index, std::uint64_t offset,
                                      void* output, std::size_t length) const noexcept {
    if (!safe_output(output, length)) return error(AfsReaderError::invalid_buffer, AfsReaderStage::member);
    if (length > PosixReadOnlyAssets::max_read_bytes) return error(AfsReaderError::read_too_large, AfsReaderStage::member);
    AfsMember member{};
    auto result = stat_member(index, member);
    if (!result) return result;
    if (offset > member.bytes || length > member.bytes - offset ||
        offset > std::numeric_limits<std::uint64_t>::max() - member.offset) {
        result.error = AfsReaderError::member_range;
        return result;
    }
    result.assets_status = assets_->read_exact(path(), std::uint64_t{member.offset} + offset, output, length);
    result.bytes_transferred = result.assets_status.bytes_transferred;
    if (!result.assets_status) result.error = AfsReaderError::assets_failure;
    return result;
}
AfsReaderStatus AfsReader::directory_candidate(AfsDirectory& output) const noexcept {
    if (!safe_output(&output, sizeof(output))) return error(AfsReaderError::invalid_buffer, AfsReaderStage::directory);
    output = {};
    auto result = current_extent();
    if (!result) return result;
    result.stage = AfsReaderStage::directory;
    result.index_status = index_.directory_candidate(output);
    if (!result.index_status) result.error = AfsReaderError::index_failure;
    return result;
}
AfsReaderStatus AfsReader::read_directory_range(const AfsDirectory& directory, std::uint64_t offset,
                                               void* output, std::size_t length) const noexcept {
    if (!safe_output(output, length) || !disjoint(output, length, &directory, sizeof(directory)))
        return error(AfsReaderError::invalid_buffer, AfsReaderStage::directory);
    if (length > PosixReadOnlyAssets::max_read_bytes)
        return error(AfsReaderError::read_too_large, AfsReaderStage::directory);
    auto result = current_extent();
    if (!result) return result;
    result.stage = AfsReaderStage::directory;
    const std::uint64_t expected = std::uint64_t{index_.member_count()} * AfsIndex::directory_entry_bytes;
    if (directory.bytes != expected || directory.offset < table_bytes_ ||
        directory.offset > result.archive_bytes || directory.bytes > result.archive_bytes - directory.offset ||
        offset > directory.bytes || length > directory.bytes - offset ||
        offset > std::numeric_limits<std::uint64_t>::max() - directory.offset) {
        result.error = AfsReaderError::directory_range;
        return result;
    }
    result.assets_status = assets_->read_exact(path(), directory.offset + offset, output, length);
    result.bytes_transferred = result.assets_status.bytes_transferred;
    if (!result.assets_status) result.error = AfsReaderError::assets_failure;
    return result;
}
} // namespace cedar
