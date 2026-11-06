#include "cedar/texture_resource.hpp"
#include <limits>

namespace cedar {
namespace {
struct Range { std::uintptr_t begin{}, end{}; };
bool make_range(const void* pointer, std::size_t length, Range& result) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    result = {begin, begin + length};
    return true;
}
bool overlaps(const Range& a, const Range& b) noexcept {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
bool dds_signature(const std::uint8_t* bytes, std::size_t size) noexcept {
    return size >= 4 && bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ';
}
} // namespace

TextureResourceStatus decode_texture_resource(const TextureResourceRequest& request,
                                               GzipWorkspace& workspace) noexcept {
    TextureResourceStatus result;
    if (!request.source || !request.rgba) {
        result.error = TextureResourceError::invalid_buffer;
        return result;
    }
    if (request.source_bytes > DdsView::max_file_bytes) {
        result.error = TextureResourceError::size_limit;
        return result;
    }
    Range source, rgba, arguments;
    if (!make_range(request.source, request.source_bytes, source) ||
        !make_range(request.rgba, request.rgba_capacity, rgba) ||
        !make_range(&request, sizeof(request), arguments)) {
        result.error = TextureResourceError::address_overflow;
        return result;
    }
    if (overlaps(source, rgba) || overlaps(arguments, rgba)) {
        result.error = TextureResourceError::overlapping_buffers;
        return result;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(request.source);
    if (request.source_bytes < 2) {
        result.error = TextureResourceError::truncated_signature;
        return result;
    }
    const bool is_gzip = bytes[0] == 0x1fu && bytes[1] == 0x8bu;
    if (!is_gzip && !dds_signature(bytes, request.source_bytes)) {
        result.error = request.source_bytes < 4 ? TextureResourceError::truncated_signature :
            TextureResourceError::unsupported_signature;
        return result;
    }
    const void* dds_bytes = request.source;
    std::size_t dds_size = request.source_bytes;
    if (is_gzip) {
        result.kind = TextureResourceKind::gzip_dds;
        if (!request.inflate_buffer) {
            result.error = TextureResourceError::invalid_buffer;
            return result;
        }
        if (request.source_bytes > gzip_input_limit || request.inflate_capacity > gzip_output_limit) {
            result.error = TextureResourceError::size_limit;
            return result;
        }
        Range scratch, state;
        if (!make_range(request.inflate_buffer, request.inflate_capacity, scratch) ||
            !make_range(&workspace, sizeof(workspace), state)) {
            result.error = TextureResourceError::address_overflow;
            return result;
        }
        if (overlaps(source, scratch) || overlaps(source, state) || overlaps(rgba, scratch) ||
            overlaps(rgba, state) || overlaps(scratch, state) || overlaps(arguments, scratch) ||
            overlaps(arguments, state)) {
            result.error = TextureResourceError::overlapping_buffers;
            return result;
        }
        result.gzip_attempted = true;
        result.gzip_status = decode_gzip(request.source, request.source_bytes,
                                         request.inflate_buffer, request.inflate_capacity, workspace);
        result.source_consumed = result.gzip_status.input_consumed;
        result.inflated_bytes = result.gzip_status.output_written;
        if (!result.gzip_status) {
            result.error = TextureResourceError::gzip_failure;
            return result;
        }
        dds_bytes = request.inflate_buffer;
        dds_size = result.inflated_bytes;
        // Parsing requires the uncompressed bytes to be DDS; no recursive gzip
        // or other package interpretation is attempted after verified inflation.
    } else result.kind = TextureResourceKind::dds;

    DdsView view;
    result.dds_attempted = true;
    result.dds_status = view.parse(dds_bytes, dds_size);
    if (!result.dds_status) {
        result.error = TextureResourceError::dds_failure;
        return result;
    }
    result.dds_parsed = true;
    result.metadata = view.metadata();
    if (!is_gzip) result.source_consumed = result.metadata.consumed_bytes;
    DdsMip mip;
    result.dds_status = view.mip(request.mip_index, mip);
    if (!result.dds_status) {
        result.error = TextureResourceError::dds_failure;
        return result;
    }
    // The decoder independently checks all output geometry/range/overlap before
    // its first RGBA write. Multiplication below is performed only after success.
    result.dds_status = view.decode_rgba(request.mip_index, request.rgba,
                                          request.rgba_capacity, request.rgba_row_pitch);
    if (!result.dds_status) {
        result.error = TextureResourceError::dds_failure;
        return result;
    }
    result.mip = mip;
    result.rgba_bytes = mip.rgba_bytes;
    result.rgba_span = request.rgba_row_pitch * (mip.height - 1u) +
        static_cast<std::size_t>(mip.width) * 4;
    result.output_valid = true;
    return result;
}
} // namespace cedar
