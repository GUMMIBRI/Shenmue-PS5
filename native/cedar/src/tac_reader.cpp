#include "cedar/tac_reader.hpp"

#include <cstring>
#include <limits>

namespace cedar {
namespace {
TacReaderStatus index_failure(TadStatus status) noexcept {
    return {TacReaderError::index_failure, status, {}, 0};
}

TacReaderStatus assets_failure(PosixAssetStatus status) noexcept {
    return {TacReaderError::assets_failure, {}, status, status.bytes_transferred};
}

TacReaderStatus entry_range_failure(std::uint32_t index) noexcept {
    return {TacReaderError::entry_range, {TadError::entry_out_of_range, index}, {}, 0};
}
}

void TacReader::close() noexcept {
    assets_ = nullptr;
    index_ = {};
    tac_path_length_ = 0;
    tac_path_[0] = '\0';
}

TacReaderStatus TacReader::open(const PosixReadOnlyAssets& assets, const void* tad_bytes,
                               std::size_t tad_length, std::string_view relative) noexcept {
    close();
    if (relative.empty())
        return assets_failure({PosixAssetError::invalid_path});
    if (relative.size() > PosixReadOnlyAssets::max_path_bytes)
        return assets_failure({PosixAssetError::path_too_long});

    // Keep candidates local until index parse, strict backend path validation,
    // and real TAC metadata binding have all succeeded.
    TadIndex candidate;
    const auto parsed = candidate.parse(tad_bytes, tad_length);
    if (!parsed) return index_failure(parsed);
    PosixAssetInfo info;
    const auto measured = assets.stat(relative, info);
    if (!measured) return assets_failure(measured);
    const auto paired = candidate.validate_tac_size(info.size);
    if (!paired) return index_failure(paired);

    std::memcpy(tac_path_, relative.data(), relative.size());
    tac_path_length_ = relative.size();
    tac_path_[tac_path_length_] = '\0';
    index_ = candidate;
    assets_ = &assets;
    return {};
}

TacReaderStatus TacReader::stat_hash(std::uint32_t hash1, std::uint32_t hash2,
                                    TadEntry& result) const noexcept {
    result = {};
    if (!opened()) return {TacReaderError::not_open};
    std::uint32_t entry_index;
    TadEntry entry;
    const auto found = index_.find_hash(hash1, hash2, entry_index, entry);
    if (!found) return index_failure(found);
    PosixAssetInfo current;
    const auto measured = assets_->stat(path(), current);
    if (!measured) return assets_failure(measured);
    const auto paired = index_.validate_tac_size(current.size);
    if (!paired) return index_failure(paired);
    const auto checked = TadIndex::validate_range(entry, current.size);
    if (!checked) return index_failure({checked.error, entry_index});
    result = entry;
    return {TacReaderError::none, {TadError::none, entry_index}, {}, 0};
}
TacReaderStatus TacReader::read_hash(std::uint32_t hash1, std::uint32_t hash2,
    std::uint64_t entry_offset, void* buffer, std::size_t length) const noexcept {
    if (!opened()) return {TacReaderError::not_open};
    if (!buffer && length) return assets_failure({PosixAssetError::invalid_buffer});
    if (length > PosixReadOnlyAssets::max_read_bytes)
        return assets_failure({PosixAssetError::read_too_large});
    TadEntry entry;
    const auto measured = stat_hash(hash1, hash2, entry);
    if (!measured) return measured;
    const auto entry_index = measured.index_status.entry_index;

    // Subtraction precedes addition; an entry may not borrow bytes from the next
    // entry even when those bytes are otherwise inside the physical archive.
    if (entry_offset > entry.size || length > entry.size - entry_offset ||
        entry_offset > std::numeric_limits<std::uint64_t>::max() - entry.offset)
        return entry_range_failure(entry_index);
    const auto absolute_offset = entry.offset + entry_offset;
    const auto read = assets_->read_exact(path(), absolute_offset, buffer, length);
    if (!read) return assets_failure(read);
    return {TacReaderError::none, {}, read, read.bytes_transferred};
}

} // namespace cedar
