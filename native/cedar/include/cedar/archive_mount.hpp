#pragma once
#include "cedar/tac_reader.hpp"

namespace cedar {
enum class ArchiveMountError { none, invalid_buffer, index_too_large, assets_failure, archive_failure };
struct ArchiveMountStatus {
    ArchiveMountError error{ArchiveMountError::none};
    PosixAssetStatus assets_status{};
    TacReaderStatus reader_status{};
    std::size_t index_bytes_read{};
    explicit operator bool() const noexcept { return error == ArchiveMountError::none; }
};
// Loads just a bounded TAD index into caller storage and binds its TAC in place.
// The mounted assets object and index storage must outlive this mount/readers;
// no remount, mutation or overlapping storage while bound. No payload extraction.
class ArchiveMount final {
public:
    ArchiveMount() noexcept = default;
    ArchiveMount(const ArchiveMount&) = delete;
    ArchiveMount& operator=(const ArchiveMount&) = delete;
    ArchiveMount(ArchiveMount&&) = delete;
    ArchiveMount& operator=(ArchiveMount&&) = delete;
    ArchiveMountStatus open(const PosixReadOnlyAssets& assets,
                            std::string_view tad_relative, std::string_view tac_relative,
                            void* index_storage, std::size_t capacity) noexcept;
    void close() noexcept { reader_.close(); }
    const TacReader* reader() const noexcept { return reader_.opened() ? &reader_ : nullptr; }
private:
    TacReader reader_{};
};
} // namespace cedar
