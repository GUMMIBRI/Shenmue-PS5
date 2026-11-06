#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cedar {

enum class AssetErrorCode {
    none, not_mounted, invalid_root, invalid_path, outside_root,
    not_regular_file, filesystem_error, out_of_range, read_too_large,
    open_failed, read_failed
};

struct AssetError {
    AssetErrorCode code = AssetErrorCode::none;
    std::string message;
    explicit operator bool() const noexcept { return code != AssetErrorCode::none; }
};

template<class T> struct AssetResult {
    T value{};
    AssetError error{};
    explicit operator bool() const noexcept { return !static_cast<bool>(error); }
};

struct AssetInfo {
    std::filesystem::path relative_path;
    std::uint64_t size = 0;
};

// Accesses owner-supplied files in place; never writes, extracts or recognizes formats.
// Canonical containment assumes the mounted directory is not concurrently modified.
// This is a path hygiene boundary, not a sandbox against hostile filesystem races.
class ReadOnlyAssets {
public:
    explicit ReadOnlyAssets(std::size_t max_read_bytes = 64u * 1024u * 1024u)
        : max_read_bytes_(max_read_bytes) {}
    AssetError mount(const std::filesystem::path& root);
    bool mounted() const noexcept { return !root_.empty(); }
    const std::filesystem::path& root() const noexcept { return root_; }
    AssetResult<AssetInfo> stat(const std::filesystem::path& relative_path) const;
    AssetResult<std::vector<std::uint8_t>> read(const std::filesystem::path& relative_path,
                                             std::uint64_t offset,
                                             std::size_t length) const;
private:
    AssetResult<std::filesystem::path> resolve(const std::filesystem::path&) const;
    std::filesystem::path root_;
    std::size_t max_read_bytes_;
};

} // namespace cedar
