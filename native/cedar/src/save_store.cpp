#include "cedar/save_store.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(O_NOFOLLOW) || !defined(O_DIRECTORY) || !defined(O_CLOEXEC) || !defined(O_NONBLOCK)
#error "Cedar save store requires no-follow descriptor opens without fallback"
#endif

namespace cedar {
namespace {
SaveStatus failed(SaveError error, int native = 0, std::size_t bytes = 0) noexcept {
    SaveStatus status;
    status.error = error;
    status.system_error = native;
    status.bytes_transferred = bytes;
    return status;
}
class Fd {
public:
    explicit Fd(int fd = -1) noexcept : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const noexcept { return fd_; }
    int release() noexcept { const int result = fd_; fd_ = -1; return result; }
    void replace(int fd) noexcept { if (fd_ >= 0) ::close(fd_); fd_ = fd; }
private:
    int fd_;
};
bool alnum_ascii(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool slot_name(std::string_view slot, char (&name)[SaveStore::max_slot_bytes + 1]) noexcept {
    if (slot.empty() || slot.size() > SaveStore::max_slot_bytes || !alnum_ascii(slot.front())) return false;
    for (char c : slot) if (!alnum_ascii(c) && c != '_' && c != '-' && c != '.') return false;
    std::memcpy(name, slot.data(), slot.size());
    name[slot.size()] = '\0';
    return true;
}
int sync_fd(int fd) noexcept {
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    return result;
}
SaveStatus remove_own_temp(int root, const char* temp, SaveStatus status) noexcept {
    std::memcpy(status.temporary_name, temp, std::strlen(temp) + 1);
    int result;
    do { result = ::unlinkat(root, temp, 0); } while (result < 0 && errno == EINTR);
    if (result < 0 && errno != ENOENT) {
        status.cleanup_system_error = errno;
        status.temporary_may_remain = true;
    }
    return status;
}
}

SaveStore::~SaveStore() { unmount(); }
SaveStore::SaveStore(SaveStore&& other) noexcept : root_fd_(other.root_fd_), sequence_(other.sequence_) {
    other.root_fd_ = -1;
}
SaveStore& SaveStore::operator=(SaveStore&& other) noexcept {
    if (this != &other) {
        unmount();
        root_fd_ = other.root_fd_;
        sequence_ = other.sequence_;
        other.root_fd_ = -1;
    }
    return *this;
}
void SaveStore::unmount() noexcept {
    if (root_fd_ >= 0) ::close(root_fd_); // Never retry ambiguous close EINTR.
    root_fd_ = -1;
}
SaveStatus SaveStore::mount(std::string_view root) noexcept {
    unmount();
    if (root.size() < 2 || root.size() > 1023 || root.front() != '/' || root.back() == '/')
        return failed(SaveError::invalid_root);
    // Validate the entire path before opening anything. No filesystem writes.
    std::size_t start = 1;
    while (start < root.size()) {
        const auto end = root.find('/', start);
        const auto part = root.substr(start, end == std::string_view::npos ? root.size() - start : end - start);
        if (part.empty() || part.size() > 255 || part == "." || part == "..") return failed(SaveError::invalid_root);
        for (char c : part) if (c == '\0' || c == ':' || c == '\\') return failed(SaveError::invalid_root);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    int fd;
    do { fd = ::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); } while (fd < 0 && errno == EINTR);
    if (fd < 0) return failed(SaveError::system_error, errno);
    Fd directory(fd);
    start = 1;
    while (start < root.size()) {
        const auto slash = root.find('/', start);
        const auto end = slash == std::string_view::npos ? root.size() : slash;
        char component[256];
        const auto length = end - start;
        std::memcpy(component, root.data() + start, length);
        component[length] = '\0';
        do { fd = ::openat(directory.get(), component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }
        while (fd < 0 && errno == EINTR);
        if (fd < 0) return failed(SaveError::system_error, errno);
        directory.replace(fd);
        start = end + 1;
    }
    root_fd_ = directory.release();
    return {};
}
SaveStatus SaveStore::load(std::string_view slot, void* buffer, std::size_t capacity,
                           std::size_t& blob_size) const noexcept {
    blob_size = 0;
    if (!mounted()) return failed(SaveError::not_mounted);
    char name[max_slot_bytes + 1];
    if (!slot_name(slot, name)) return failed(SaveError::invalid_slot);
    if (!buffer && capacity) return failed(SaveError::invalid_buffer);
    int fd;
    do { fd = ::openat(root_fd_, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK); }
    while (fd < 0 && errno == EINTR);
    if (fd < 0) return failed(SaveError::system_error, errno);
    Fd file(fd);
    struct ::stat info {};
    int result;
    do { result = ::fstat(fd, &info); } while (result < 0 && errno == EINTR);
    if (result < 0) return failed(SaveError::system_error, errno);
    if (!S_ISREG(info.st_mode)) return failed(SaveError::not_regular_file);
    if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > max_blob_bytes) return failed(SaveError::too_large);
    blob_size = static_cast<std::size_t>(info.st_size);
    if (capacity < blob_size) return failed(SaveError::buffer_too_small);
    auto* bytes = static_cast<unsigned char*>(buffer);
    std::size_t done = 0;
    while (done < blob_size) {
        ssize_t count;
        do { count = ::pread(fd, bytes + done, blob_size - done, static_cast<off_t>(done)); }
        while (count < 0 && errno == EINTR);
        if (count < 0) return failed(SaveError::system_error, errno, done);
        if (count == 0) return failed(SaveError::unexpected_eof, 0, done);
        done += static_cast<std::size_t>(count);
    }
    SaveStatus status;
    status.bytes_transferred = done;
    return status;
}
SaveStatus SaveStore::store(std::string_view slot, const void* data, std::size_t size,
                            bool allow_replace) noexcept {
    if (!mounted()) return failed(SaveError::not_mounted);
    char name[max_slot_bytes + 1];
    if (!slot_name(slot, name)) return failed(SaveError::invalid_slot);
    if (!data && size) return failed(SaveError::invalid_buffer);
    if (size > max_blob_bytes) return failed(SaveError::too_large);
    char temp[80]{};
    int fd = -1;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        ++sequence_;
        const int count = std::snprintf(temp, sizeof(temp), ".cedar-tmp-%ld-%llu",
            static_cast<long>(::getpid()), static_cast<unsigned long long>(sequence_));
        if (count < 0 || static_cast<std::size_t>(count) >= sizeof(temp)) return failed(SaveError::system_error, EOVERFLOW);
        do { fd = ::openat(root_fd_, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600); }
        while (fd < 0 && errno == EINTR);
        if (fd >= 0) break;
        if (errno != EEXIST) return failed(SaveError::system_error, errno);
    }
    if (fd < 0) return failed(SaveError::temporary_collision);
    Fd file(fd);
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t done = 0;
    while (done < size) {
        ssize_t count;
        do { count = ::write(fd, bytes + done, size - done); } while (count < 0 && errno == EINTR);
        if (count <= 0) return remove_own_temp(root_fd_, temp,
            failed(SaveError::system_error, count < 0 ? errno : EIO, done));
        done += static_cast<std::size_t>(count);
    }
    if (sync_fd(fd) < 0) return remove_own_temp(root_fd_, temp, failed(SaveError::system_error, errno, done));
    // close may release a descriptor even on EINTR. Never retry it.
    if (::close(file.release()) < 0) return remove_own_temp(root_fd_, temp, failed(SaveError::system_error, errno, done));
    const int commit = allow_replace ? ::renameat(root_fd_, temp, root_fd_, name) :
        ::linkat(root_fd_, temp, root_fd_, name, 0);
    if (commit < 0) {
        const int native = errno;
        if (native == EINTR) {
            auto status = failed(SaveError::commit_uncertain, native, done);
            status.temporary_may_remain = true;
            std::memcpy(status.temporary_name, temp, std::strlen(temp) + 1);
            return status;
        }
        return remove_own_temp(root_fd_, temp, failed(
            !allow_replace && native == EEXIST ? SaveError::slot_exists : SaveError::system_error,
            native, done));
    }
    SaveStatus status;
    status.committed = true;
    status.bytes_transferred = done;
    if (!allow_replace) {
        status = remove_own_temp(root_fd_, temp, status);
        if (status.temporary_may_remain) {
            status.error = SaveError::cleanup_failed;
            status.system_error = status.cleanup_system_error;
        }
    }
    if (sync_fd(root_fd_) < 0) {
        status.error = SaveError::durability_uncertain;
        status.system_error = errno;
    }
    return status;
}
} // namespace cedar
