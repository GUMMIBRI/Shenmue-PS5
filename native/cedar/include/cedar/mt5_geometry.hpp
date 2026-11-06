#pragma once

#include "cedar/mt5.hpp"

#include <cstddef>
#include <cstdint>

namespace cedar {

struct Mt5GeometryCounts {
    std::size_t draws = 0;
    std::size_t triangles = 0;
    std::size_t vertices = 0; // expanded, sequential triangle vertices; no index buffer
};
enum class Mt5GeometryError {
    none, invalid_view, source_failure, size_overflow, invalid_buffer,
    insufficient_capacity, overlapping_storage
};
struct Mt5GeometryResult {
    Mt5GeometryError error = Mt5GeometryError::none;
    Mt5Status source_status{};
    std::uint32_t node_index = UINT32_MAX, strip_index = UINT32_MAX;
    // Complete successful preflight counts; zero when preflight itself fails.
    Mt5GeometryCounts required{};
    // Nonzero only on complete successful extraction, never partial validity.
    Mt5GeometryCounts written{};
    explicit operator bool() const noexcept { return error == Mt5GeometryError::none; }
};

// One record per source strip with at least three corners. The vertex range is
// a triangle list using the compatible primary's winding, including degenerates.
// Texture index is an unresolved source reference, not a fabricated material.
struct Mt5DrawRecord {
    std::uint32_t node_index = 0, strip_index = 0;
    std::size_t first_vertex = 0, vertex_count = 0, triangle_count = 0;
    std::uint16_t texture_index = 0, command = 0, opaque_strip_word = 0, uv_size = 0;
    bool mirror_u = false, mirror_v = false, uv_high_precision = false;
    const std::uint8_t* raw_attributes = nullptr; // borrowed immutable source bytes
    std::size_t raw_attribute_bytes = 0;
};

struct Mt5GeometryVertex {
    Mt5Vec3 world_position{};
    Mt5Vec3 raw_local_position{}, raw_local_normal{};
    // Compatible reader's translated normal, deliberately not a lighting normal.
    Mt5Vec3 source_legacy_transformed_normal{};
    std::int32_t raw_vertex_index = 0;
    std::uint32_t source_node = UINT32_MAX, source_vertex_index = 0, global_vertex_index = 0;
    bool has_uv = false, has_color = false;
    std::uint16_t raw_u = 0, raw_v = 0;
    float u = 0, v = 0;
    std::uint8_t bgra[4]{};
};

// Both operations walk actual triangles and preflight each source-node static
// vertex transform. No allocation, extraction, rasterization or game frame.
Mt5GeometryResult measure_mt5_geometry(const Mt5View& view) noexcept;

// Two-pass extraction: complete preflight, capacity/overlap checks, then writes.
// Only complete success makes output valid. On unexpected second-pass failure,
// output may have changed but written stays zero; discard it. No default faces,
// deduplication, material binding, lighting normals or cleanup are invented.
//
// Caller keeps the view, input bytes and node workspace immutable/disjoint from
// outputs throughout BOTH passes. Checks cover output buffers, view object and
// exposed node workspace, but cannot prove disjointness from the full underlying
// input byte range hidden by Mt5View. Draw attribute spans borrow that storage
// after return; retain it until the draw metadata is no longer used.
Mt5GeometryResult extract_mt5_geometry(const Mt5View& view,
                                        Mt5DrawRecord* draws, std::size_t draw_capacity,
                                        Mt5GeometryVertex* vertices, std::size_t vertex_capacity) noexcept;

} // namespace cedar
