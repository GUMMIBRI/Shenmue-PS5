#include "cedar/archive_set.hpp"

namespace cedar {
namespace {
bool missing_key(const TacReaderStatus& status) noexcept {
    return status.error == TacReaderError::index_failure &&
           status.index_status.error == TadError::not_found;
}
ArchiveSetStatus reader_failure(std::size_t index, TacReaderStatus status) noexcept {
    return {ArchiveSetError::reader_failure, index, status, status.bytes_transferred};
}
} // namespace

void ArchiveSet::unbind() noexcept {
    readers_ = nullptr;
    count_ = 0;
}

ArchiveSetStatus ArchiveSet::bind(const TacReader* const* readers, std::size_t count) noexcept {
    unbind();
    if (!readers || !count || count > max_archives)
        return {ArchiveSetError::invalid_config};
    for (std::size_t i = 0; i < count; ++i) {
        if (!readers[i]) return {ArchiveSetError::invalid_config, i};
        if (!readers[i]->opened())
            return {ArchiveSetError::invalid_config, i, {TacReaderError::not_open}};
        for (std::size_t earlier = 0; earlier < i; ++earlier)
            if (readers[i] == readers[earlier]) return {ArchiveSetError::invalid_config, i};
    }
    readers_ = readers;
    count_ = count;
    return {};
}

ArchiveSetStatus ArchiveSet::stat_hash(std::uint32_t hash1, std::uint32_t hash2,
                                     TadEntry& entry) const noexcept {
    entry = {};
    if (!bound()) return {ArchiveSetError::not_bound};
    for (std::size_t i = 0; i < count_; ++i) {
        // Null would violate the immutable-array contract; still fail closed
        // rather than treating the damaged priority slot as a missing key.
        if (!readers_[i]) return {ArchiveSetError::invalid_config, i};
        TadEntry candidate{};
        const auto result = readers_[i]->stat_hash(hash1, hash2, candidate);
        if (result) {
            entry = candidate;
            return {ArchiveSetError::none, i, result};
        }
        if (!missing_key(result)) return reader_failure(i, result);
    }
    return {ArchiveSetError::not_found};
}

ArchiveSetStatus ArchiveSet::read_hash(std::uint32_t hash1, std::uint32_t hash2,
    std::uint64_t entry_offset, void* buffer, std::size_t length) const noexcept {
    if (!bound()) return {ArchiveSetError::not_bound};
    if (!buffer && length) return {ArchiveSetError::invalid_buffer};
    if (length > PosixReadOnlyAssets::max_read_bytes)
        return {ArchiveSetError::read_too_large};
    TadEntry selected_entry{};
    const auto selected = stat_hash(hash1, hash2, selected_entry);
    if (!selected) return selected;
    const auto index = selected.archive_index;
    const auto result = readers_[index]->read_hash(hash1, hash2, entry_offset, buffer, length);
    if (!result) return reader_failure(index, result);
    return {ArchiveSetError::none, index, result, result.bytes_transferred};
}
} // namespace cedar
