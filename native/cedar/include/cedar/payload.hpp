#pragma once

// Format evidence: MIT ShenmueDK; see docs/ARCHIVE-KEY-PROVENANCE.md.
#include <cstddef>
#include <cstdint>

namespace cedar {

enum class PayloadKind { unknown, gzip, pkf, ipac, pvrt, gbix };
enum class PayloadError {
    none, invalid_buffer, truncated, unknown_signature, unsupported_method,
    malformed_header, header_limit
};
struct PayloadStatus {
    PayloadError error = PayloadError::none;
    explicit operator bool() const noexcept { return error == PayloadError::none; }
};
struct GzipHeader {
    std::size_t header_bytes = 0;
    std::uint32_t modification_time = 0;
    std::uint16_t extra_bytes = 0;
    std::uint16_t opaque_header_crc16 = 0; // exposed only, not verified
    std::uint8_t flags = 0;
    std::uint8_t extra_flags = 0;
    std::uint8_t operating_system = 0;
};
struct PayloadInfo {
    PayloadKind kind = PayloadKind::unknown;
    // True only for an explicitly recognized gzip/DEFLATE envelope. False does
    // not establish that unknown or nested data is uncompressed.
    bool requires_gzip_decode = false;
    GzipHeader gzip{};
};

// Prefix recognition only: no payload decode, checksum, trailer or authenticity
// verification. Known raw container signatures do not prove a complete valid
// container. Gzip header scan has a 64KiB implementation bound and accepts only
// method 8, zero reserved flags and fully present optional header fields.
constexpr std::size_t gzip_header_limit = 64u * 1024u;
PayloadStatus parse_gzip_header(const void* prefix, std::size_t length,
                                GzipHeader& result) noexcept;
PayloadStatus probe_payload(const void* prefix, std::size_t length,
                             PayloadInfo& result) noexcept;

enum class IpacError {
    none, invalid_buffer, truncated, unknown_signature, unsupported_layout,
    malformed_index, too_many_entries, entry_out_of_range, invalid_entry_index,
    not_parsed
};
struct IpacStatus {
    IpacError error = IpacError::none;
    std::uint32_t entry_index = UINT32_MAX;
    explicit operator bool() const noexcept { return error == IpacError::none; }
};
struct IpacHeader {
    std::uint32_t dictionary_offset = 0;
    std::uint32_t file_count = 0;
    std::uint32_t content_size = 0;
};
struct IpacEntry {
    // Fixed-width on-disk bytes, not NUL-terminated strings or filesystem paths.
    char name[8]{};
    char extension[4]{};
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

// Allocation-free, non-owning view of the plain IPAC layout emitted by the
// compatible source: file data before dictionary_offset == content_size.
// Caller retains immutable complete container storage throughout use. Neither
// names nor entry payloads are interpreted, decoded or written to disk.
class IpacIndex {
public:
    static constexpr std::size_t header_bytes = 16;
    static constexpr std::size_t dictionary_entry_bytes = 20;
    static constexpr std::uint32_t max_entries = 1024u * 1024u;
    IpacStatus parse(const void* bytes, std::size_t length) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    const IpacHeader& header() const noexcept { return header_; }
    IpacStatus entry(std::uint32_t index, IpacEntry& result) const noexcept;
    // Returns a bounded member view; output pointer is null on error. The view
    // can contain compressed/unknown bytes and is not an extracted file.
    IpacStatus member(std::uint32_t index, const void*& bytes,
                       std::size_t& length) const noexcept;
private:
    const std::uint8_t* data_ = nullptr;
    IpacHeader header_{};
};

} // namespace cedar
