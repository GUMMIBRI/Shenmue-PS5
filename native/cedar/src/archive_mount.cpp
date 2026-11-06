#include "cedar/archive_mount.hpp"
#include <limits>

namespace cedar {
ArchiveMountStatus ArchiveMount::open(const PosixReadOnlyAssets& assets,
    std::string_view tad, std::string_view tac, void* storage, std::size_t capacity) noexcept {
    close();
    ArchiveMountStatus result;
    if (!storage) { result.error = ArchiveMountError::invalid_buffer; return result; }
    const auto base = reinterpret_cast<std::uintptr_t>(storage);
    const auto max = std::numeric_limits<std::uintptr_t>::max();
    const auto self = reinterpret_cast<std::uintptr_t>(this);
    const auto root = reinterpret_cast<std::uintptr_t>(&assets);
    if (capacity > max - base || (base < self + sizeof(*this) && self < base + capacity) ||
        (base < root + sizeof(assets) && root < base + capacity)) {
        result.error = ArchiveMountError::invalid_buffer; return result;
    }
    const auto aliases_storage = [base, capacity](std::string_view name) noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(name.data());
        return name.size() > max - address ||
            (name.size() && base < address + name.size() && address < base + capacity);
    };
    if (aliases_storage(tad) || aliases_storage(tac)) {
        result.error = ArchiveMountError::invalid_buffer; return result;
    }
    PosixAssetInfo info;
    result.assets_status = assets.stat(tad, info);
    if (!result.assets_status) { result.error = ArchiveMountError::assets_failure; return result; }
    constexpr auto index_limit = TadIndex::header_bytes +
        static_cast<std::size_t>(TadIndex::max_entries) * TadIndex::entry_bytes;
    if (info.size > index_limit || info.size > capacity) {
        result.error = ArchiveMountError::index_too_large; return result;
    }
    const auto length = static_cast<std::size_t>(info.size);
    result.assets_status = assets.read_exact(tad, 0, storage, length);
    result.index_bytes_read = result.assets_status.bytes_transferred;
    if (!result.assets_status) { result.error = ArchiveMountError::assets_failure; return result; }
    result.reader_status = reader_.open(assets, storage, length, tac);
    if (!result.reader_status) result.error = ArchiveMountError::archive_failure;
    return result;
}
} // namespace cedar
