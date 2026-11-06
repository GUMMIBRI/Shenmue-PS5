#include "cedar/mt5_geometry.hpp"

#include <cstring>
#include <limits>

namespace cedar {
namespace {
bool add(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}
bool multiply(std::size_t count, std::size_t stride, std::size_t& bytes) noexcept {
    if (count > std::numeric_limits<std::size_t>::max() / stride) return false;
    bytes = count * stride;
    return true;
}
bool overlap(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!an || !bn) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a);
    const auto y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < an : x - y < bn;
}
Mt5GeometryResult failure(Mt5GeometryError error, std::uint32_t node = UINT32_MAX,
                           std::uint32_t strip = UINT32_MAX, Mt5Status source = {}) noexcept {
    Mt5GeometryResult result;
    result.error = error;
    result.node_index = node;
    result.strip_index = strip;
    result.source_status = source;
    return result;
}
Mt5GeometryVertex converted(const Mt5Corner& corner, const Mt5StaticVertex& vertex) noexcept {
    Mt5GeometryVertex result;
    result.world_position = vertex.world_position;
    result.raw_local_position = vertex.local.position;
    result.raw_local_normal = vertex.local.normal;
    result.source_legacy_transformed_normal = vertex.source_legacy_transformed_normal;
    result.raw_vertex_index = corner.raw_vertex_index;
    result.source_node = corner.source_node;
    result.source_vertex_index = corner.vertex_index;
    result.global_vertex_index = corner.global_vertex_index;
    result.has_uv = corner.has_uv;
    result.has_color = corner.has_color;
    result.raw_u = corner.raw_u;
    result.raw_v = corner.raw_v;
    result.u = corner.u;
    result.v = corner.v;
    std::memcpy(result.bgra, corner.bgra, sizeof(result.bgra));
    return result;
}

Mt5GeometryResult walk(const Mt5View& view, bool writing, Mt5DrawRecord* draws,
                        std::size_t draw_capacity, Mt5GeometryVertex* vertices,
                        std::size_t vertex_capacity) noexcept {
    if (!view.parsed()) return failure(Mt5GeometryError::invalid_view);
    Mt5GeometryResult result;
    for (std::size_t node_index = 0; node_index < view.node_count(); ++node_index) {
        const auto node_id = static_cast<std::uint32_t>(node_index);
        const auto* node = view.node(node_index);
        if (!node) return failure(Mt5GeometryError::invalid_view, node_id);
        if (!node->mesh_offset) continue;
        Mt5StripCursor cursor;
        auto status = view.strips(node_index, cursor);
        if (!status) return failure(Mt5GeometryError::source_failure, node_id, UINT32_MAX, status);
        std::uint32_t strip_index = 0;
        for (;;) {
            Mt5Strip strip;
            status = cursor.next(strip);
            if (status.error == Mt5Error::end_of_strips) break;
            if (!status) return failure(Mt5GeometryError::source_failure, node_id, strip_index, status);
            if (strip.corner_count() >= 3) {
                const std::size_t triangles = strip.corner_count() - 2;
                std::size_t expanded, next_vertices, next_triangles, next_draws;
                if (!multiply(triangles, 3, expanded) ||
                    !add(result.required.vertices, expanded, next_vertices) ||
                    !add(result.required.triangles, triangles, next_triangles) ||
                    !add(result.required.draws, 1, next_draws))
                    return failure(Mt5GeometryError::size_overflow, node_id, strip_index);
                if (writing && (next_draws > draw_capacity || next_vertices > vertex_capacity))
                    return failure(Mt5GeometryError::insufficient_capacity, node_id, strip_index);
                std::size_t destination = result.required.vertices;
                for (std::uint32_t triangle = 0; triangle < triangles; ++triangle) {
                    Mt5Corner corners[3];
                    status = strip.triangle(triangle, corners);
                    if (!status) return failure(Mt5GeometryError::source_failure, node_id, strip_index, status);
                    for (const auto& corner : corners) {
                        Mt5StaticVertex vertex;
                        // A linked corner can belong to an earlier, different
                        // node. Never fetch from the current strip's node by default.
                        status = view.static_vertex(corner.source_node, corner.vertex_index, vertex);
                        if (!status) return failure(Mt5GeometryError::source_failure, node_id, strip_index, status);
                        if (writing) vertices[destination] = converted(corner, vertex);
                        ++destination;
                    }
                }
                if (writing) {
                    Mt5DrawRecord draw;
                    draw.node_index = node_id;
                    draw.strip_index = strip_index;
                    draw.first_vertex = result.required.vertices;
                    draw.vertex_count = expanded;
                    draw.triangle_count = triangles;
                    draw.texture_index = strip.texture_index();
                    draw.command = strip.command();
                    draw.opaque_strip_word = strip.opaque_strip_word();
                    draw.uv_size = strip.uv_size();
                    draw.mirror_u = strip.mirror_u();
                    draw.mirror_v = strip.mirror_v();
                    draw.uv_high_precision = strip.uv_high_precision();
                    draw.raw_attributes = strip.raw_attributes();
                    draw.raw_attribute_bytes = strip.raw_attribute_bytes();
                    draws[result.required.draws] = draw;
                }
                result.required = {next_draws, next_triangles, next_vertices};
            }
            if (strip_index == UINT32_MAX)
                return failure(Mt5GeometryError::size_overflow, node_id, strip_index);
            ++strip_index;
        }
    }
    return result;
}
}

Mt5GeometryResult measure_mt5_geometry(const Mt5View& view) noexcept {
    return walk(view, false, nullptr, 0, nullptr, 0);
}

Mt5GeometryResult extract_mt5_geometry(const Mt5View& view,
    Mt5DrawRecord* draws, std::size_t draw_capacity,
    Mt5GeometryVertex* vertices, std::size_t vertex_capacity) noexcept {
    auto measured = measure_mt5_geometry(view);
    if (!measured) return measured;
    if ((!draws && draw_capacity) || (!vertices && vertex_capacity)) {
        measured.error = Mt5GeometryError::invalid_buffer;
        return measured;
    }
    if (measured.required.draws > draw_capacity || measured.required.vertices > vertex_capacity) {
        measured.error = Mt5GeometryError::insufficient_capacity;
        return measured;
    }
    std::size_t draw_bytes, vertex_bytes, node_bytes;
    if (!multiply(draw_capacity, sizeof(Mt5DrawRecord), draw_bytes) ||
        !multiply(vertex_capacity, sizeof(Mt5GeometryVertex), vertex_bytes) ||
        !multiply(view.node_count(), sizeof(Mt5Node), node_bytes)) {
        measured.error = Mt5GeometryError::size_overflow;
        return measured;
    }
    const auto* node_storage = view.node_count() ? view.node(0) : nullptr;
    if (overlap(draws, draw_bytes, vertices, vertex_bytes) ||
        overlap(draws, draw_bytes, &view, sizeof(view)) ||
        overlap(vertices, vertex_bytes, &view, sizeof(view)) ||
        overlap(draws, draw_bytes, node_storage, node_bytes) ||
        overlap(vertices, vertex_bytes, node_storage, node_bytes)) {
        measured.error = Mt5GeometryError::overlapping_storage;
        return measured;
    }
    const auto emitted = walk(view, true, draws, draw_capacity, vertices, vertex_capacity);
    if (!emitted) {
        measured.error = emitted.error;
        measured.source_status = emitted.source_status;
        measured.node_index = emitted.node_index;
        measured.strip_index = emitted.strip_index;
        return measured; // preflight counts remain truthful; all output invalid
    }
    // Under the immutable-source contract both passes must describe the same
    // geometry. Refuse validity if an external mutation violated that contract.
    if (emitted.required.draws != measured.required.draws ||
        emitted.required.triangles != measured.required.triangles ||
        emitted.required.vertices != measured.required.vertices) {
        measured.error = Mt5GeometryError::source_failure;
        return measured;
    }
    measured.written = measured.required;
    return measured;
}

} // namespace cedar
