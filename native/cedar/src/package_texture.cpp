#include "cedar/package_texture.hpp"
#include <limits>

namespace cedar {
namespace {
struct Range { std::uintptr_t begin{}, end{}; };
bool address_range(const void* pointer, std::size_t length, Range& result) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    result = {begin, begin + length};
    return true;
}
bool overlaps(const Range& a, const Range& b) noexcept {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
std::uint32_t signature(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
} // namespace

PackageTextureStatus decode_package_texture(const PackageTextureRequest& request,
                                             GzipWorkspace& workspace) noexcept {
    PackageTextureStatus result;
    result.source_bytes = request.source_bytes;
    if (!request.source || !request.rgba) {
        result.error = PackageTextureError::invalid_buffer;
        return result;
    }
    if (request.source_bytes > package_texture_input_limit) {
        result.error = PackageTextureError::size_limit;
        return result;
    }
    Range source, output, arguments;
    if (!address_range(request.source, request.source_bytes, source) ||
        !address_range(request.rgba, request.rgba_capacity, output) ||
        !address_range(&request, sizeof(request), arguments)) {
        result.error = PackageTextureError::address_overflow;
        return result;
    }
    if (overlaps(source, output) || overlaps(arguments, output)) {
        result.error = PackageTextureError::overlapping_buffers;
        return result;
    }
    if (request.source_bytes < 2) {
        result.error = PackageTextureError::truncated_signature;
        return result;
    }
    const auto* input = static_cast<const std::uint8_t*>(request.source);
    const bool wrapped = input[0] == 0x1fu && input[1] == 0x8bu;
    if (!wrapped && (request.source_bytes < 4 || signature(input) != 0x464b4150u)) {
        result.error = request.source_bytes < 4 ? PackageTextureError::truncated_signature :
            PackageTextureError::unsupported_signature;
        return result;
    }
    const void* package = request.source;
    std::size_t package_size = request.source_bytes;
    if (wrapped) {
        if (!request.inflate_buffer) {
            result.error = PackageTextureError::invalid_buffer;
            return result;
        }
        if (request.source_bytes > gzip_input_limit || request.inflate_capacity > gzip_output_limit) {
            result.error = PackageTextureError::size_limit;
            return result;
        }
        Range inflated, state;
        if (!address_range(request.inflate_buffer, request.inflate_capacity, inflated) ||
            !address_range(&workspace, sizeof(workspace), state)) {
            result.error = PackageTextureError::address_overflow;
            return result;
        }
        if (overlaps(source, inflated) || overlaps(source, state) || overlaps(output, inflated) ||
            overlaps(output, state) || overlaps(inflated, state) || overlaps(arguments, inflated) ||
            overlaps(arguments, state)) {
            result.error = PackageTextureError::overlapping_buffers;
            return result;
        }
        result.gzip_attempted = true;
        result.gzip_status = decode_gzip(request.source, request.source_bytes,
                                         request.inflate_buffer, request.inflate_capacity, workspace);
        result.source_consumed = result.gzip_status.input_consumed;
        result.inflated_bytes = result.gzip_status.output_written;
        if (!result.gzip_status) {
            result.error = PackageTextureError::gzip_failure;
            return result;
        }
        package = request.inflate_buffer;
        package_size = result.inflated_bytes;
    }
    result.package_bytes = package_size;
    PkfView view;
    result.pkf_attempted = true;
    result.pkf_status = view.parse(package, package_size);
    if (!result.pkf_status) {
        result.error = PackageTextureError::pkf_failure;
        return result;
    }
    result.pkf_parsed = true;
    result.package_header = view.header();
    if (!wrapped) result.source_consumed = request.source_bytes;
    PkfTextureView texture;
    result.pkf_status = view.texture(request.texture_index, texture);
    if (!result.pkf_status) {
        result.error = PackageTextureError::pkf_failure;
        return result;
    }
    result.texture_selected = true;
    for (unsigned i = 0; i < 8; ++i) result.texture_id[i] = texture.texture_id[i];
    const auto body = texture.texture_body;
    result.texture_bytes = body.size;
    if (!body.data || body.size < 4) {
        result.error = PackageTextureError::unsupported_texture;
        return result;
    }
    const auto code = signature(body.data);
    auto dds_body = PvrDdsBytes{body.data, body.size};
    bool use_dds = code == 0x20534444u;
    if (code == 0x54525650u || code == 0x58494247u) {
        PvrDdsView envelope;
        result.pvr_dds_attempted = true;
        result.pvr_dds_status = envelope.parse(body.data, body.size);
        if (result.pvr_dds_status) {
            result.pvr_dds_parsed = true;
            result.pvr_dds_metadata = envelope.metadata();
            dds_body = envelope.dds_bytes();
            use_dds = true;
        } else if (result.pvr_dds_status.error != PvrDdsError::not_dds_profile) {
            result.error = PackageTextureError::pvr_dds_failure;
            return result; // A malformed DDS wrapper must not become normal PVR.
        }
    }
    if (use_dds) { // Exact DDS start or primary-source-proven PVRT transport.
        result.kind = PackageTextureKind::dds;
        DdsView dds;
        result.dds_attempted = true;
        result.dds_status = dds.parse(dds_body.data, dds_body.size);
        if (!result.dds_status) {
            result.error = PackageTextureError::dds_failure;
            return result;
        }
        result.dds_parsed = true;
        result.dds_metadata = dds.metadata();
        result.texture_consumed = result.dds_metadata.consumed_bytes +
            (result.pvr_dds_parsed ? result.pvr_dds_metadata.dds_offset : 0);
        DdsMip selected;
        result.dds_status = dds.mip(request.mip_index, selected);
        if (!result.dds_status) {
            result.error = PackageTextureError::dds_failure;
            return result;
        }
        result.dds_status = dds.decode_rgba(request.mip_index, request.rgba,
                                            request.rgba_capacity, request.rgba_row_pitch);
        if (!result.dds_status) {
            result.error = PackageTextureError::dds_failure;
            return result;
        }
        result.dds_mip = selected;
        result.rgba_bytes = selected.rgba_bytes;
        result.rgba_span = request.rgba_row_pitch * (selected.height - 1u) +
            static_cast<std::size_t>(selected.width) * 4;
    } else if (code == 0x54525650u || code == 0x58494247u) { // PVRT / compatible GBIX.
        result.kind = PackageTextureKind::pvr;
        PvrView pvr;
        result.pvr_attempted = true;
        result.pvr_status = pvr.parse(body.data, body.size);
        if (!result.pvr_status) {
            result.error = PackageTextureError::pvr_failure;
            return result;
        }
        result.pvr_parsed = true;
        result.pvr_metadata = pvr.metadata();
        result.texture_consumed = result.pvr_metadata.consumed_bytes;
        if (request.mip_index != 0) {
            result.error = PackageTextureError::unsupported_mip;
            return result;
        }
        result.pvr_status = pvr.decode_rgba(request.rgba, request.rgba_capacity, request.rgba_row_pitch);
        if (!result.pvr_status) {
            result.error = PackageTextureError::pvr_failure;
            return result;
        }
        result.rgba_bytes = result.pvr_metadata.rgba_bytes;
        result.rgba_span = request.rgba_row_pitch * (result.pvr_metadata.height - 1u) +
            static_cast<std::size_t>(result.pvr_metadata.width) * 4;
    } else {
        result.error = PackageTextureError::unsupported_texture;
        return result;
    }
    // Both decoders validate the pitched span before writes. Arithmetic above is
    // reached only after that validation; no speculative successful output exists.
    result.output_valid = true;
    return result;
}
} // namespace cedar
