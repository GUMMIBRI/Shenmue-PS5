// Original native app-owned blob storage, not a foreign game-save implementation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cedar {
enum class SaveError : std::uint8_t {
    none, not_mounted, invalid_root, invalid_slot, invalid_buffer, too_large,
    buffer_too_small, not_regular_file, unexpected_eof, system_error,
    temporary_collision, slot_exists, cleanup_failed, commit_uncertain, durability_uncertain
};
struct SaveStatus {
    SaveError error{SaveError::none};
    int system_error{};
    int cleanup_system_error{};
    std::size_t bytes_transferred{};
    bool committed{}; // linkat/renameat returned success; not game-save validity.
    bool temporary_may_remain{};
    char temporary_name[80]{}; // Recovery hint in the mounted root, never an absolute path.
    explicit operator bool() const noexcept { return error == SaveError::none; }
};

// Caller MUST designate a newly provisioned app-owned save directory. No default
// path, directory creation, owner-save import, migration, or foreign save encoding.
// Never mount an original game's save directory. Replacement requires explicit opt-in.
// Root and every component must be pre-existing nonsymlink directories. The root
// must be trusted: no protection against privileged mount changes/hard links or
// another actor replacing our private temporary directory entries. Synchronize
// calls on each object. Concurrent distinct objects use exclusive temporary files;
// concurrent replacement commits to a slot are last-successful-rename wins.
class SaveStore final {
public:
    static constexpr std::size_t max_blob_bytes = 16u * 1024u * 1024u;
    static constexpr std::size_t max_slot_bytes = 96;
    SaveStore() noexcept = default;
    ~SaveStore();
    SaveStore(const SaveStore&) = delete;
    SaveStore& operator=(const SaveStore&) = delete;
    SaveStore(SaveStore&& other) noexcept;
    SaveStore& operator=(SaveStore&& other) noexcept;
    SaveStatus mount(std::string_view app_owned_absolute_root) noexcept;
    void unmount() noexcept;
    bool mounted() const noexcept { return root_fd_ >= 0; }
    // Slot names begin with an ASCII letter/digit; remaining characters may also
    // contain underscore, hyphen or dot. No path components, NULs or hidden names.
    // blob_size is cleared first, then reports the opened file size if available.
    // Failed reads may have copied the prefix reported in bytes_transferred.
    SaveStatus load(std::string_view slot, void* buffer, std::size_t capacity,
                    std::size_t& blob_size) const noexcept;
    // Exclusive temp -> complete write -> file fsync -> close -> commit -> dir
    // fsync. Default commit uses linkat then unlinks the temp, atomically refusing
    // an existing slot. Explicit allow_replace uses renameat. Before commit
    // failure, the previous slot is preserved. Directory fsync failure after
    // commit returns durability_uncertain with committed=true, without rollback.
    // A commit interrupted by a signal returns commit_uncertain and preserves
    // any remaining temp for recovery; it never guesses whether commit occurred.
    SaveStatus store(std::string_view slot, const void* data, std::size_t size,
                     bool allow_replace = false) noexcept;
private:
    int root_fd_{-1};
    std::uint64_t sequence_{};
};
} // namespace cedar
