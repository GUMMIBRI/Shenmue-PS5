#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cedar {

enum class PosixAssetError {
    none, not_mounted, invalid_path, path_too_long, invalid_buffer,
    read_too_large, not_regular_file, out_of_range, unexpected_eof, system_error
};

struct PosixAssetStatus {
    PosixAssetError error = PosixAssetError::none;
    int system_error = 0; // errno captured at the failing system call, otherwise zero
    std::size_t bytes_transferred = 0;
    explicit operator bool() const noexcept { return error == PosixAssetError::none; }
};

struct PosixAssetInfo { std::uint64_t size = 0; };

// Original read-only POSIX backend: no format parsing, writes or asset duplication.
// Mount and every descendant component reject symlinks with descriptor-relative
// O_NOFOLLOW opens. Directory identities stay pinned while walking the path.
// This is not a sandbox against mount changes, hard links or privileged actors.
// Caller must synchronize mount/unmount/moves against other calls. Once mounted,
// simultaneous reads do not share stream positions. Files may still change on disk.
class PosixReadOnlyAssets {
public:
    static constexpr std::size_t max_path_bytes = 1023;
    static constexpr std::size_t max_component_bytes = 255;
    static constexpr std::size_t max_read_bytes = 64u * 1024u * 1024u;

    PosixReadOnlyAssets() noexcept = default;
    ~PosixReadOnlyAssets();
    PosixReadOnlyAssets(const PosixReadOnlyAssets&) = delete;
    PosixReadOnlyAssets& operator=(const PosixReadOnlyAssets&) = delete;
    PosixReadOnlyAssets(PosixReadOnlyAssets&& other) noexcept;
    PosixReadOnlyAssets& operator=(PosixReadOnlyAssets&& other) noexcept;

    // Requires a slash-separated absolute POSIX directory path. A failed mount
    // leaves the object unmounted, including when replacing an existing mount.
    PosixAssetStatus mount(std::string_view absolute_root) noexcept;
    void unmount() noexcept;
    bool mounted() const noexcept { return root_fd_ >= 0; }

    // Relative paths forbid empty/dot components, backslashes, colons and NULs.
    // stat clears info before attempting access; success reports an opened file.
    PosixAssetStatus stat(std::string_view relative_path, PosixAssetInfo& info) const noexcept;

    // Exact read into caller-owned storage. On failure, a prefix may have been
    // written: bytes_transferred reports its length. There is no zero-fill/fake
    // success on truncation. Null buffer is permitted only for a zero-byte read.
    PosixAssetStatus read_exact(std::string_view relative_path, std::uint64_t offset,
                                void* buffer, std::size_t length) const noexcept;
private:
    int root_fd_ = -1;
};

} // namespace cedar
