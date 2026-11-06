// SPDX-License-Identifier: GPL-3.0-only
// Original read-only I/O composition with the GPL-licensed AfsIndex.
#pragma once
#include "cedar/afs.hpp"
#include "cedar/posix_assets.hpp"
#include <string_view>

namespace cedar {
enum class AfsReaderError : std::uint8_t {
    none, not_open, invalid_buffer, invalid_path, storage_too_small,
    index_failure, assets_failure, archive_changed, member_range,
    directory_range, read_too_large
};
enum class AfsReaderStage : std::uint8_t { none, metadata, header, table, index, member, directory };
struct AfsReaderStatus {
    AfsReaderError error{AfsReaderError::none};
    AfsReaderStage stage{AfsReaderStage::none};
    AfsStatus index_status{};
    PosixAssetStatus assets_status{};
    std::size_t required_table_bytes{};
    // Caller-buffer prefix only. Local 8-byte header reads are reported in
    // assets_status.bytes_transferred, never falsely counted as caller writes.
    std::size_t bytes_transferred{};
    std::uint64_t archive_bytes{}, expected_archive_bytes{};
    explicit operator bool() const noexcept { return error == AfsReaderError::none; }
};

// Borrows actual-read table bytes and mounted assets; no allocated file buffer.
// Caller serializes all methods, including const reads (size-change latch), and
// keeps table immutable/assets unmoved+mounted until close or a new open attempt.
class AfsReader final {
public:
    static constexpr std::size_t max_table_bytes = 8u + std::size_t(AfsIndex::max_members) * 8u;
    AfsReader() noexcept = default;
    AfsReader(const AfsReader&) = delete;
    AfsReader& operator=(const AfsReader&) = delete;
    AfsReaderStatus open(const PosixReadOnlyAssets& assets, std::string_view relative_path,
                         void* table_storage, std::size_t capacity) noexcept;
    void close() noexcept;
    bool opened() const noexcept { return assets_ && !archive_changed_; }
    bool requires_reopen() const noexcept { return archive_changed_; }
    std::uint32_t member_count() const noexcept { return opened() ? index_.member_count() : 0; }
    AfsReaderStatus stat_member(std::uint32_t index, AfsMember& output) const noexcept;
    AfsReaderStatus read_member(std::uint32_t index, std::uint64_t member_offset,
                                void* output, std::size_t length) const noexcept;
    // An in-bounds sector-aligned candidate is not proof of a name directory.
    AfsReaderStatus directory_candidate(AfsDirectory& output) const noexcept;
    // Explicit raw directory-span request only. Caller supplies a range with
    // exactly count*0x30 bytes, inside the archive and outside the header/table.
    // This reads bytes, never authenticates a directory or guesses filenames.
    AfsReaderStatus read_directory_range(const AfsDirectory& requested_directory,
        std::uint64_t directory_offset, void* output, std::size_t length) const noexcept;
private:
    AfsReaderStatus current_extent() const noexcept;
    bool safe_output(const void* output, std::size_t length) const noexcept;
    std::string_view path() const noexcept { return {path_, path_bytes_}; }
    const PosixReadOnlyAssets* assets_{};
    const std::uint8_t* table_{};
    std::size_t table_bytes_{}, path_bytes_{};
    std::uint64_t archive_bytes_{};
    mutable bool archive_changed_{};
    mutable std::uint64_t changed_archive_bytes_{};
    AfsIndex index_{};
    char path_[PosixReadOnlyAssets::max_path_bytes + 1]{};
};
} // namespace cedar
