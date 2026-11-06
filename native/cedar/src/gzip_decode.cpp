// Original RFC1952 envelope/checksum wrapper; inflate implementation is miniz MIT.
// Exact source pin/notices: third_party/miniz/PROVENANCE.md and LICENSE.
#include "cedar/gzip_decode.hpp"
#include "miniz_tinfl.h"
#include <array>
#include <new>
#include <limits>

namespace cedar {
namespace {
constexpr std::array<std::uint32_t, 256> crc_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        auto n = i;
        for (unsigned bit = 0; bit < 8; ++bit)
            n = (n >> 1) ^ ((n & 1u) ? 0xedb88320u : 0u);
        table[i] = n;
    }
    return table;
}
constexpr auto crc_words = crc_table();
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t length) noexcept {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i)
        crc = crc_words[(crc ^ bytes[i]) & 0xffu] ^ (crc >> 8);
    return ~crc;
}
std::uint32_t little32(const std::uint8_t* p) noexcept {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
        (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}
bool overlap(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!an || !bn) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a);
    const auto y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < an : x - y < bn;
}
}
GzipStatus decode_gzip(const void* input, std::size_t length, void* output,
                       std::size_t capacity, GzipWorkspace& workspace) noexcept {
    static_assert(sizeof(tinfl_decompressor) <= sizeof(workspace.state), "Increase workspace for pinned miniz");
    static_assert(alignof(tinfl_decompressor) <= alignof(GzipWorkspace), "Invalid inflate alignment");
    GzipStatus result;
    if (!input || (!output && capacity)) { result.error = GzipError::invalid_buffer; return result; }
    if (length > gzip_input_limit || capacity > gzip_output_limit) {
        result.error = GzipError::size_limit; return result;
    }
    const auto address_limit = std::numeric_limits<std::uintptr_t>::max();
    if (length > address_limit - reinterpret_cast<std::uintptr_t>(input) ||
        capacity > address_limit - reinterpret_cast<std::uintptr_t>(output)) {
        result.error = GzipError::invalid_buffer; return result;
    }
    if (overlap(input, length, output, capacity) ||
        overlap(input, length, &workspace, sizeof(workspace)) ||
        overlap(output, capacity, &workspace, sizeof(workspace))) {
        result.error = GzipError::overlapping_buffers; return result;
    }
    const auto* source = static_cast<const std::uint8_t*>(input);
    auto* destination = static_cast<std::uint8_t*>(output);
    std::uint8_t empty_output{};
    do {
        if (result.members_verified == gzip_member_limit) {
            result.error = GzipError::member_limit; return result;
        }
        GzipHeader header;
        const auto header_status = parse_gzip_header(source + result.input_consumed,
                                                     length - result.input_consumed, header);
        if (!header_status) {
            result.error = GzipError::header_failure;
            result.header_status = header_status;
            return result;
        }
        const auto* member = source + result.input_consumed;
        if ((header.flags & 0x02u) &&
            (crc32(member, header.header_bytes - 2) & 0xffffu) != header.opaque_header_crc16) {
            result.error = GzipError::header_checksum; return result;
        }
        result.input_consumed += header.header_bytes;
        if (length - result.input_consumed < 8) { result.error = GzipError::truncated; return result; }
        auto* state = new (workspace.state) tinfl_decompressor;
        tinfl_init(state);
        std::size_t in_bytes = length - result.input_consumed;
        std::size_t out_bytes = capacity - result.output_written;
        auto* member_output = output ? destination + result.output_written : &empty_output;
        const auto status = tinfl_decompress(state, source + result.input_consumed,
            &in_bytes, member_output, member_output, &out_bytes,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        result.input_consumed += in_bytes;
        result.output_written += out_bytes;
        if (status != TINFL_STATUS_DONE) {
            result.error = status == TINFL_STATUS_HAS_MORE_OUTPUT ?
                GzipError::output_too_small : GzipError::deflate_failure;
            return result;
        }
        if (length - result.input_consumed < 8) { result.error = GzipError::truncated; return result; }
        const auto* trailer = source + result.input_consumed;
        if (little32(trailer) != crc32(member_output, out_bytes)) {
            result.error = GzipError::payload_checksum; return result;
        }
        if (little32(trailer + 4) != out_bytes) {
            result.error = GzipError::decoded_size_mismatch; return result;
        }
        result.input_consumed += 8;
        ++result.members_verified;
    } while (result.input_consumed < length);
    return result;
}
} // namespace cedar
