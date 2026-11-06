// SPDX-License-Identifier: GPL-3.0-only
// Adapted from brynnb/new-yokosuka parse_afs; see docs/AFS-SCOPE.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class AfsError : std::uint8_t {
    none, invalid_buffer, truncated, invalid_signature, invalid_count,
    invalid_extent, not_parsed, invalid_index, directory_unavailable
};
struct AfsStatus {
    AfsError error{AfsError::none};
    std::size_t offset{};
    explicit operator bool() const noexcept { return error == AfsError::none; }
};
struct AfsMember { std::uint32_t index{}, offset{}, bytes{}; };
struct AfsDirectory { std::uint64_t offset{}, bytes{}; };
// Borrows only the header/table, not the archive's payload. Caller supplies the
// actual read-only archive extent and retains immutable table bytes while used.
// Does not infer a member type, load voice/text, or pair members by guessed names.
class AfsIndex {
public:
    static constexpr std::uint32_t max_members = 1000000;
    static constexpr std::size_t directory_entry_bytes = 0x30;
    AfsStatus parse(const void* table, std::size_t table_bytes,
                    std::uint64_t actual_archive_bytes) noexcept;
    bool parsed() const noexcept { return table_ != nullptr; }
    std::uint32_t member_count() const noexcept { return count_; }
    AfsStatus member(std::uint32_t index, AfsMember& output) const noexcept;
    // Reference profile's candidate directory: sector-aligned maximum member
    // end. In-bounds does not prove a directory exists; caller must establish it.
    AfsStatus directory_candidate(AfsDirectory& output) const noexcept;
    // Returns the borrowed exact first32 filename bytes of an explicitly read
    // directory. No ASCII replacement/normalization or fallback filename.
    AfsStatus raw_name(const void* directory, std::size_t directory_bytes,
                       std::uint32_t index, const std::uint8_t*& output) const noexcept;
private:
    const std::uint8_t* table_{};
    std::uint32_t count_{};
    std::uint64_t archive_bytes_{}, maximum_end_{};
};
} // namespace cedar
