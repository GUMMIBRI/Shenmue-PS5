// Original CPU-side conversion of completed MT5 geometry for explicit GPU input.
#pragma once
#include "cedar/mt5_geometry.hpp"
#include "cedar/gpu_types.hpp"

namespace cedar {
enum class GpuColorPolicy : std::uint8_t { require_source, source_else_explicit_rgba };
enum class GpuPackError : std::uint8_t {
    none, invalid_count, invalid_policy, invalid_buffer, size_overflow,
    overlapping_storage, unsupported_uv, missing_color, non_finite,
    insufficient_capacity, invalid_model, missing_node, invalid_source_reference,
    noninvertible_transform, zero_normal
};
struct GpuPackResult {
    static constexpr std::size_t no_vertex = static_cast<std::size_t>(-1);
    GpuPackError error{GpuPackError::none};
    std::size_t vertex_index{no_vertex};
    // Required is valid only after complete successful source/range preflight;
    // insufficient_capacity can expose it. Written is valid only on full success.
    std::size_t required_vertices{}, written_vertices{};
    explicit operator bool() const noexcept { return error == GpuPackError::none; }
};

// No default color policy. Fallback mode requires a real caller-supplied RGBA
// array of four bytes, even when this particular input has all source colors.
// require_source ignores fallback entirely and permits nullptr there.
// Source stays immutable/disjoint from the entire declared output-capacity span.
// All validation precedes writes; no allocation, SDL call, GPU work or guessing.
GpuPackResult pack_mt5_gpu_vertices(const Mt5GeometryVertex* source, std::size_t count,
    GpuVertex* output, std::size_t capacity, GpuColorPolicy policy,
    const std::uint8_t* explicit_fallback_rgba) noexcept;

// Separate 12-byte normal stream; GpuVertex's 24-byte layout is unchanged. Uses
// raw_local_normal and its actual source_node's inverse world linear transform,
// never source_legacy_transformed_normal or the strip/draw node. Row vectors:
// nWorld=nLocal*(W^-1)^T, then robust double-precision normalization.
// Missing references, singular transforms, zero/nonfinite normals are errors,
// never a guessed lighting direction. Full preflight before any output writes.
// Source/model node workspace stay immutable; declared output-capacity span
// must be disjoint from source, model/view and node storage. Caller additionally
// keeps underlying model bytes disjoint (not exposed by Mt5View's public API).
GpuPackResult pack_mt5_gpu_normals(const Mt5View& model,
    const Mt5GeometryVertex* source, std::size_t count,
    GpuNormal* output, std::size_t capacity) noexcept;
} // namespace cedar
