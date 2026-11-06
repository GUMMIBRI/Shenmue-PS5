// Original explicit-priority archive composition; no game resource naming rules.
#pragma once
#include "cedar/tac_reader.hpp"

namespace cedar {
enum class ArchiveSetError : std::uint8_t {
    none, not_bound, invalid_config, not_found, invalid_buffer,
    read_too_large, reader_failure
};
struct ArchiveSetStatus {
    static constexpr std::size_t no_archive = static_cast<std::size_t>(-1);
    ArchiveSetError error{ArchiveSetError::none};
    // Selected reader on success, failing reader for reader/config failure, or
    // no_archive when there is no corresponding list member.
    std::size_t archive_index{no_archive};
    TacReaderStatus reader_status{};
    std::size_t bytes_transferred{};
    explicit operator bool() const noexcept { return error == ArchiveSetError::none; }
};

// Borrows the pointer ARRAY as well as its readers: retain both unchanged and
// unmoved while bound. Readers retain their own TAD/storage lifetime contracts.
// Synchronize bind/unbind and all backing reader/storage changes against use.
class ArchiveSet final {
public:
    static constexpr std::size_t max_archives = 32;
    ArchiveSet() noexcept = default;
    ArchiveSet(const ArchiveSet&) = delete;
    ArchiveSet& operator=(const ArchiveSet&) = delete;
    // Earlier list positions have explicitly higher priority. Empty/null lists,
    // null/unopened/duplicate readers, or more than 32 entries are rejected.
    // Any bind attempt discards the previous binding before validation.
    ArchiveSetStatus bind(const TacReader* const* ordered_readers, std::size_t count) noexcept;
    void unbind() noexcept;
    bool bound() const noexcept { return readers_ != nullptr; }
    std::size_t size() const noexcept { return count_; }
    // Clears entry before lookup. Advances only on an exact index not_found.
    // Other failures stop immediately and preserve the failing priority index.
    ArchiveSetStatus stat_hash(std::uint32_t hash1, std::uint32_t hash2,
                               TadEntry& entry) const noexcept;
    // Raw stored bytes only. Selects by stat_hash then reads ONLY that reader;
    // a later read failure never falls back to an older/lower-priority archive.
    // Buffer prefixes can change on failure; bytes_transferred reports them.
    ArchiveSetStatus read_hash(std::uint32_t hash1, std::uint32_t hash2,
                               std::uint64_t entry_offset, void* buffer,
                               std::size_t length) const noexcept;
private:
    const TacReader* const* readers_{};
    std::size_t count_{};
};
} // namespace cedar
