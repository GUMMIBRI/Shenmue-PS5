#include "cedar/assets.hpp"

#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cedar {
namespace {
AssetError fail(AssetErrorCode code, const char* message) { return {code, message}; }
AssetError fs_fail(const std::error_code& ec) {
    return {AssetErrorCode::filesystem_error, ec.message()};
}
}

AssetError ReadOnlyAssets::mount(const std::filesystem::path& root) {
    // Failed remounts must not leave a previous owner's mount active.
    root_.clear();
    if (root.empty()) return fail(AssetErrorCode::invalid_root, "Asset root is empty");
    std::error_code ec;
    auto resolved = std::filesystem::canonical(root, ec);
    if (ec) return fs_fail(ec);
    const bool directory = std::filesystem::is_directory(resolved, ec);
    if (ec) return fs_fail(ec);
    if (!directory) return fail(AssetErrorCode::invalid_root, "Asset root is not a directory");
    root_ = std::move(resolved);
    return {};
}

AssetResult<std::filesystem::path> ReadOnlyAssets::resolve(
    const std::filesystem::path& relative) const {
    if (!mounted()) return {{}, fail(AssetErrorCode::not_mounted, "No asset root mounted")};
    if (relative.empty() || relative.has_root_path())
        return {{}, fail(AssetErrorCode::invalid_path, "Expected a nonempty relative asset path")};
    for (const auto& part : relative) {
        if (part == ".." || part == ".")
            return {{}, fail(AssetErrorCode::invalid_path, "Dot path components are not allowed")};
        // Colons also exclude NTFS alternate data streams and drive-relative paths.
        const auto spelling = part.native();
        if (spelling.find(static_cast<std::filesystem::path::value_type>(':')) != spelling.npos ||
            spelling.find(static_cast<std::filesystem::path::value_type>(0)) != spelling.npos)
            return {{}, fail(AssetErrorCode::invalid_path, "Unsupported asset path component")};
    }
    std::error_code ec;
    auto candidate = std::filesystem::canonical(root_ / relative, ec);
    if (ec) return {{}, fs_fail(ec)};
    // Compare actual ancestor identities, handling Windows case and junction aliases.
    bool contained = false;
    for (auto parent = candidate.parent_path(); !parent.empty();) {
        const bool same = std::filesystem::equivalent(parent, root_, ec);
        if (ec) return {{}, fs_fail(ec)};
        if (same) { contained = true; break; }
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    if (!contained)
        return {{}, fail(AssetErrorCode::outside_root, "Resolved asset escapes the mounted root")};
    const bool regular = std::filesystem::is_regular_file(candidate, ec);
    if (ec) return {{}, fs_fail(ec)};
    if (!regular)
        return {{}, fail(AssetErrorCode::not_regular_file, "Asset is not a regular file")};
    return {std::move(candidate), {}};
}

AssetResult<AssetInfo> ReadOnlyAssets::stat(const std::filesystem::path& relative) const {
    const auto resolved = resolve(relative);
    if (!resolved) return {{}, resolved.error};
    std::error_code ec;
    const auto size = std::filesystem::file_size(resolved.value, ec);
    if (ec) return {{}, fs_fail(ec)};
    if (size > std::numeric_limits<std::uint64_t>::max())
        return {{}, fail(AssetErrorCode::out_of_range, "File size exceeds supported range")};
    return {{relative, static_cast<std::uint64_t>(size)}, {}};
}

AssetResult<std::vector<std::uint8_t>> ReadOnlyAssets::read(
    const std::filesystem::path& relative, std::uint64_t offset, std::size_t length) const {
    if (length > max_read_bytes_)
        return {{}, fail(AssetErrorCode::read_too_large, "Read exceeds configured allocation limit")};
    const auto resolved = resolve(relative);
    if (!resolved) return {{}, resolved.error};
    std::error_code ec;
    const auto size = std::filesystem::file_size(resolved.value, ec);
    if (ec) return {{}, fs_fail(ec)};
    if (offset > size || length > size - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        length > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return {{}, fail(AssetErrorCode::out_of_range, "Read lies outside the file or stream range")};
    std::ifstream input(resolved.value, std::ios::binary);
    if (!input) return {{}, fail(AssetErrorCode::open_failed, "Cannot open asset for reading")};
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) return {{}, fail(AssetErrorCode::read_failed, "Cannot seek asset")};
    std::vector<std::uint8_t> data;
    try { data.resize(length); }
    catch (const std::bad_alloc&) {
        return {{}, fail(AssetErrorCode::read_too_large, "Asset read allocation failed")};
    }
    catch (const std::length_error&) {
        return {{}, fail(AssetErrorCode::read_too_large, "Asset read exceeds container capacity")};
    }
    if (length) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(length));
        if (!input || input.gcount() != static_cast<std::streamsize>(length))
            return {{}, fail(AssetErrorCode::read_failed, "Asset read was incomplete")};
    }
    return {std::move(data), {}};
}

} // namespace cedar
