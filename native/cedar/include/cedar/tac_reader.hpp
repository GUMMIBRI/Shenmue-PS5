#pragma once

#include "cedar/posix_assets.hpp"
#include "cedar/tad.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cedar {

enum class TacReaderError { none, not_open, index_failure, assets_failure, entry_range };

struct TacReaderStatus {
    TacReaderError error = TacReaderError::none;
    TadStatus index_status{};
    PosixAssetStatus assets_status{};
    std::size_t bytes_transferred = 0;
    explicit operator bool() const noexcept { return error == TacReaderError::none; }
};

// Connects the original bounded TAD index to the read-only POSIX asset backend.
// Does not allocate, extract, name-resolve, decompress or authenticate anything.
// Returned bytes are the raw stored entry bytes, possibly compressed. A matching
// length is a structural check, not proof of TAC/TAD identity or data integrity.
//
// Caller retains immutable full TAD storage and the mounted assets object for
// this reader's lifetime, and must not remount/move that object while bound.
// Caller synchronizes open/close against reads. External content or path changes
// between stat and read are not prevented; the POSIX backend performs its own
// safe open/range checks, but offers no stable-file/integrity guarantee.
class TacReader {
public:
    TacReaderStatus open(const PosixReadOnlyAssets& assets, const void* tad_bytes,
                         std::size_t tad_length, std::string_view tac_relative_path) noexcept;
    void close() noexcept;
    bool opened() const noexcept { return assets_ != nullptr; }
    const TadHeader& header() const noexcept { return index_.header(); }
    // Resolves the first matching hash and verifies the actual TAC length/range.
    // Clears result on failure; this is structural metadata, not authentication.
    TacReaderStatus stat_hash(std::uint32_t hash1, std::uint32_t hash2,
                              TadEntry& result) const noexcept;

    // Reads only a requested subrange inside the first matching hash-pair entry.
    // No file name/hash algorithm is inferred. On failure, a caller-buffer prefix
    // may have changed; bytes_transferred accurately reports completed bytes.
    TacReaderStatus read_hash(std::uint32_t hash1, std::uint32_t hash2,
                               std::uint64_t entry_offset, void* buffer,
                               std::size_t length) const noexcept;
private:
    std::string_view path() const noexcept { return {tac_path_, tac_path_length_}; }
    const PosixReadOnlyAssets* assets_ = nullptr;
    TadIndex index_{};
    char tac_path_[PosixReadOnlyAssets::max_path_bytes + 1]{};
    std::size_t tac_path_length_ = 0;
};

} // namespace cedar
