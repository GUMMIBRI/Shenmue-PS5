#include "cedar/posix_assets.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(O_NOFOLLOW) || !defined(O_DIRECTORY) || !defined(O_CLOEXEC) || \
    !defined(O_NONBLOCK) || !defined(O_NOCTTY)
#error "Cedar POSIX assets requires no-follow descriptor opens; no weaker fallback exists"
#endif

namespace cedar {
namespace {
PosixAssetStatus failure(PosixAssetError error, int system_error = 0,
                         std::size_t transferred = 0) noexcept {
    return {error, system_error, transferred};
}

class OwnedFd {
public:
    explicit OwnedFd(int fd = -1) noexcept : fd_(fd) {}
    ~OwnedFd() { reset(); }
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    int get() const noexcept { return fd_; }
    int release() noexcept { const int fd = fd_; fd_ = -1; return fd; }
    void reset(int fd = -1) noexcept {
        // Do not retry close on EINTR: a descriptor may already have been released.
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }
private:
    int fd_;
};

PosixAssetStatus validate(std::string_view path, bool absolute) noexcept {
    if (path.empty() || (path.front() == '/') != absolute)
        return failure(PosixAssetError::invalid_path);
    if (path.size() > PosixReadOnlyAssets::max_path_bytes)
        return failure(PosixAssetError::path_too_long);
    if (absolute && path == "/") return {};
    std::size_t start = absolute ? 1 : 0;
    while (start < path.size()) {
        const auto slash = path.find('/', start);
        const auto end = slash == std::string_view::npos ? path.size() : slash;
        const auto component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..")
            return failure(PosixAssetError::invalid_path);
        if (component.size() > PosixReadOnlyAssets::max_component_bytes)
            return failure(PosixAssetError::path_too_long);
        for (const char c : component) {
            if (c == '\0' || c == ':' || c == '\\')
                return failure(PosixAssetError::invalid_path);
        }
        if (slash == std::string_view::npos) return {};
        start = end + 1;
    }
    return failure(PosixAssetError::invalid_path); // trailing slash
}

int open_directory_root() noexcept {
    int fd;
    do { fd = ::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }
    while (fd < 0 && errno == EINTR);
    return fd;
}

// Path must already have passed validate. Returned descriptor owns its lifetime.
PosixAssetStatus walk(int root, std::string_view relative, bool directory,
                      OwnedFd& output) noexcept {
    int parent = root;
    OwnedFd intermediate;
    std::size_t start = 0;
    while (start < relative.size()) {
        const auto slash = relative.find('/', start);
        const auto end = slash == std::string_view::npos ? relative.size() : slash;
        const auto length = end - start;
        char component[PosixReadOnlyAssets::max_component_bytes + 1];
        std::memcpy(component, relative.data() + start, length);
        component[length] = '\0';
        const bool final = end == relative.size();
        const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC |
            ((!final || directory) ? O_DIRECTORY : (O_NONBLOCK | O_NOCTTY));
        int fd;
        do { fd = ::openat(parent, component, flags); }
        while (fd < 0 && errno == EINTR);
        if (fd < 0) return failure(PosixAssetError::system_error, errno);
        if (final) { output.reset(fd); return {}; }
        // The next fd is acquired before releasing its parent, preventing
        // symlink/path replacement from redirecting a previously opened parent.
        intermediate.reset(fd);
        parent = intermediate.get();
        start = end + 1;
    }
    return failure(PosixAssetError::invalid_path);
}

PosixAssetStatus file_info(int fd, PosixAssetInfo& info) noexcept {
    struct ::stat metadata {};
    int result;
    do { result = ::fstat(fd, &metadata); } while (result < 0 && errno == EINTR);
    if (result < 0) return failure(PosixAssetError::system_error, errno);
    if (!S_ISREG(metadata.st_mode)) return failure(PosixAssetError::not_regular_file);
    if (metadata.st_size < 0) return failure(PosixAssetError::out_of_range);
    info.size = static_cast<std::uint64_t>(metadata.st_size);
    return {};
}
}

PosixReadOnlyAssets::~PosixReadOnlyAssets() { unmount(); }

PosixReadOnlyAssets::PosixReadOnlyAssets(PosixReadOnlyAssets&& other) noexcept
    : root_fd_(other.root_fd_) { other.root_fd_ = -1; }

PosixReadOnlyAssets& PosixReadOnlyAssets::operator=(PosixReadOnlyAssets&& other) noexcept {
    if (this != &other) {
        unmount();
        root_fd_ = other.root_fd_;
        other.root_fd_ = -1;
    }
    return *this;
}

void PosixReadOnlyAssets::unmount() noexcept {
    if (root_fd_ >= 0) ::close(root_fd_);
    root_fd_ = -1;
}

PosixAssetStatus PosixReadOnlyAssets::mount(std::string_view absolute_root) noexcept {
    unmount();
    const auto checked = validate(absolute_root, true);
    if (!checked) return checked;
    OwnedFd filesystem_root(open_directory_root());
    if (filesystem_root.get() < 0) return failure(PosixAssetError::system_error, errno);
    if (absolute_root == "/") {
        root_fd_ = filesystem_root.release();
        return {};
    }
    OwnedFd mounted;
    const auto opened = walk(filesystem_root.get(), absolute_root.substr(1), true, mounted);
    if (!opened) return opened;
    root_fd_ = mounted.release();
    return {};
}

PosixAssetStatus PosixReadOnlyAssets::stat(std::string_view relative,
                                         PosixAssetInfo& info) const noexcept {
    info = {};
    if (!mounted()) return failure(PosixAssetError::not_mounted);
    const auto checked = validate(relative, false);
    if (!checked) return checked;
    OwnedFd file;
    const auto opened = walk(root_fd_, relative, false, file);
    if (!opened) return opened;
    return file_info(file.get(), info);
}

PosixAssetStatus PosixReadOnlyAssets::read_exact(std::string_view relative,
    std::uint64_t offset, void* buffer, std::size_t length) const noexcept {
    if (!mounted()) return failure(PosixAssetError::not_mounted);
    if (!buffer && length) return failure(PosixAssetError::invalid_buffer);
    if (length > max_read_bytes) return failure(PosixAssetError::read_too_large);
    const auto checked = validate(relative, false);
    if (!checked) return checked;
    OwnedFd file;
    const auto opened = walk(root_fd_, relative, false, file);
    if (!opened) return opened;
    PosixAssetInfo info;
    const auto measured = file_info(file.get(), info);
    if (!measured) return measured;
    const auto max_offset = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (offset > info.size || length > info.size - offset ||
        offset > max_offset || length > max_offset - offset)
        return failure(PosixAssetError::out_of_range);

    std::size_t transferred = 0;
    auto* bytes = static_cast<unsigned char*>(buffer);
    while (transferred < length) {
        // Bound each syscall independently of the overall caller-buffer limit.
        constexpr std::size_t chunk_limit = 1024u * 1024u;
        const auto remaining = length - transferred;
        const auto chunk = remaining < chunk_limit ? remaining : chunk_limit;
        ssize_t count;
        do {
            count = ::pread(file.get(), bytes + transferred, chunk,
                            static_cast<off_t>(offset + transferred));
        } while (count < 0 && errno == EINTR);
        if (count < 0) return failure(PosixAssetError::system_error, errno, transferred);
        if (!count) return failure(PosixAssetError::unexpected_eof, 0, transferred);
        transferred += static_cast<std::size_t>(count);
    }
    return {PosixAssetError::none, 0, transferred};
}

} // namespace cedar
