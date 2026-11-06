#pragma once
#include <cstddef>
#include <cstdint>
#include "cedar/payload.hpp"

namespace cedar {
enum class GzipError {
    none, invalid_buffer, size_limit, overlapping_buffers, header_failure,
    header_checksum, deflate_failure, output_too_small, truncated,
    payload_checksum, decoded_size_mismatch, member_limit
};
struct GzipStatus {
    GzipError error{GzipError::none};
    PayloadStatus header_status{};
    std::size_t input_consumed{}, output_written{}, members_verified{};
    explicit operator bool() const noexcept { return error == GzipError::none; }
};
// Caller-owned inflate state, separate from input and output. No heap allocation.
// One decode at a time per workspace; state is reset for each gzip member.
struct alignas(16) GzipWorkspace { std::uint8_t state[16384]{}; };
constexpr std::size_t gzip_input_limit = 64u * 1024u * 1024u;
constexpr std::size_t gzip_output_limit = 64u * 1024u * 1024u;
constexpr std::size_t gzip_member_limit = 1024;

// RFC1952 gzip only, including concatenated members. Validates optional header
// CRC, member CRC32 and ISIZE; rejects trailing nonmember bytes. No zlib/raw-deflate
// guessing. On failure output is untrusted and may have changed; output_written
// reports bytes produced, not bytes authenticated. CRC is not cryptographic proof.
GzipStatus decode_gzip(const void* input, std::size_t input_bytes,
                       void* output, std::size_t output_capacity,
                       GzipWorkspace& workspace) noexcept;
} // namespace cedar
