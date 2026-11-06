#include "cedar/gpu_pack.hpp"
#include <cmath>
#include <limits>

namespace cedar {
namespace {
struct Span { std::uintptr_t begin{}, end{}; };
bool span(const void* pointer, std::size_t bytes, Span& result) noexcept {
    result.begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - result.begin) return false;
    result.end = result.begin + bytes;
    return true;
}
bool overlaps(const Span& a, const Span& b) noexcept {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
bool finite(float value) noexcept {
    constexpr auto largest = std::numeric_limits<float>::max();
    return value >= -largest && value <= largest;
}
GpuPackResult fail(GpuPackError error, std::size_t index = GpuPackResult::no_vertex) noexcept {
    return {error, index, 0, 0};
}
GpuPackError normal(const Mt5View& model, const Mt5GeometryVertex& vertex, GpuNormal& result) noexcept {
    const auto* node = model.node(vertex.source_node);
    if (!node) return GpuPackError::missing_node;
    if (!node->mesh_offset || vertex.source_vertex_index >= node->vertex_count ||
        std::uint64_t{node->global_vertex_base} + vertex.source_vertex_index != vertex.global_vertex_index)
        return GpuPackError::invalid_source_reference;
    const auto n = vertex.raw_local_normal;
    if (!finite(n.x) || !finite(n.y) || !finite(n.z)) return GpuPackError::non_finite;
    if (!node->inverse_world_available) return GpuPackError::noninvertible_transform;
    const auto* inverse = node->inverse_world_transform.values;
    for (unsigned i = 0; i < 16; ++i) if (!finite(inverse[i])) return GpuPackError::non_finite;
    const double a = inverse[0], b = inverse[1], c = inverse[2];
    const double d = inverse[4], e = inverse[5], f = inverse[6];
    const double g = inverse[8], h = inverse[9], i = inverse[10];
    const double determinant = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    if (!std::isfinite(determinant)) return GpuPackError::non_finite;
    if (determinant == 0) return GpuPackError::noninvertible_transform;
    // For row-vector normals times inverse-transpose, each output component is
    // the dot product of nLocal with a ROW of the inverse. Translation absent.
    const double x = a*n.x + b*n.y + c*n.z;
    const double y = d*n.x + e*n.y + f*n.z;
    const double z = g*n.x + h*n.y + i*n.z;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return GpuPackError::non_finite;
    double largest = std::fabs(x);
    if (std::fabs(y) > largest) largest = std::fabs(y);
    if (std::fabs(z) > largest) largest = std::fabs(z);
    if (largest == 0) return GpuPackError::zero_normal;
    const double sx = x / largest, sy = y / largest, sz = z / largest;
    const double length = std::sqrt(sx*sx + sy*sy + sz*sz);
    if (!std::isfinite(length)) return GpuPackError::non_finite;
    if (length == 0) return GpuPackError::zero_normal;
    const GpuNormal packed{static_cast<float>(sx / length), static_cast<float>(sy / length), static_cast<float>(sz / length)};
    if (!finite(packed.x) || !finite(packed.y) || !finite(packed.z)) return GpuPackError::non_finite;
    result = packed;
    return GpuPackError::none;
}
} // namespace

GpuPackResult pack_mt5_gpu_vertices(const Mt5GeometryVertex* source, std::size_t count,
    GpuVertex* output, std::size_t capacity, GpuColorPolicy policy,
    const std::uint8_t* fallback) noexcept {
    if (!count || count % 3) return fail(GpuPackError::invalid_count);
    if (policy != GpuColorPolicy::require_source &&
        policy != GpuColorPolicy::source_else_explicit_rgba)
        return fail(GpuPackError::invalid_policy);
    const bool use_fallback = policy == GpuColorPolicy::source_else_explicit_rgba;
    if (!source || reinterpret_cast<std::uintptr_t>(source) % alignof(Mt5GeometryVertex) ||
        (capacity && (!output || reinterpret_cast<std::uintptr_t>(output) % alignof(GpuVertex))) ||
        (use_fallback && !fallback))
        return fail(GpuPackError::invalid_buffer);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Mt5GeometryVertex) ||
        capacity > std::numeric_limits<std::size_t>::max() / sizeof(GpuVertex))
        return fail(GpuPackError::size_overflow);
    Span input{}, destination{}, color{};
    if (!span(source, count * sizeof(Mt5GeometryVertex), input) ||
        !span(output, capacity * sizeof(GpuVertex), destination) ||
        (use_fallback && !span(fallback, 4, color)))
        return fail(GpuPackError::size_overflow);
    if (overlaps(input, destination) || (use_fallback && overlaps(color, destination)))
        return fail(GpuPackError::overlapping_storage);

    // Complete preflight before touching a single destination vertex.
    for (std::size_t i = 0; i < count; ++i) {
        const auto& vertex = source[i];
        if (!vertex.has_uv) return fail(GpuPackError::unsupported_uv, i);
        if (!finite(vertex.world_position.x) || !finite(vertex.world_position.y) ||
            !finite(vertex.world_position.z) || !finite(vertex.u) || !finite(vertex.v))
            return fail(GpuPackError::non_finite, i);
        if (!vertex.has_color && !use_fallback) return fail(GpuPackError::missing_color, i);
    }
    GpuPackResult result{};
    result.required_vertices = count;
    if (capacity < count) {
        result.error = GpuPackError::insufficient_capacity;
        return result;
    }
    std::uint8_t explicit_color[4]{};
    if (use_fallback)
        for (std::size_t channel = 0; channel < 4; ++channel) explicit_color[channel] = fallback[channel];
    for (std::size_t i = 0; i < count; ++i) {
        const auto& vertex = source[i];
        GpuVertex packed{};
        packed.x = vertex.world_position.x;
        packed.y = vertex.world_position.y;
        packed.z = vertex.world_position.z;
        packed.u = vertex.u;
        packed.v = vertex.v;
        if (vertex.has_color) {
            packed.rgba[0] = vertex.bgra[2];
            packed.rgba[1] = vertex.bgra[1];
            packed.rgba[2] = vertex.bgra[0];
            packed.rgba[3] = vertex.bgra[3];
        } else {
            for (std::size_t channel = 0; channel < 4; ++channel)
                packed.rgba[channel] = explicit_color[channel];
        }
        output[i] = packed;
    }
    result.written_vertices = count;
    return result;
}

GpuPackResult pack_mt5_gpu_normals(const Mt5View& model,
    const Mt5GeometryVertex* source, std::size_t count,
    GpuNormal* output, std::size_t capacity) noexcept {
    if (!model.parsed()) return fail(GpuPackError::invalid_model);
    if (!count || count % 3) return fail(GpuPackError::invalid_count);
    if (!source || reinterpret_cast<std::uintptr_t>(source) % alignof(Mt5GeometryVertex) ||
        (capacity && (!output || reinterpret_cast<std::uintptr_t>(output) % alignof(GpuNormal))))
        return fail(GpuPackError::invalid_buffer);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Mt5GeometryVertex) ||
        capacity > std::numeric_limits<std::size_t>::max() / sizeof(GpuNormal) ||
        model.node_count() > std::numeric_limits<std::size_t>::max() / sizeof(Mt5Node))
        return fail(GpuPackError::size_overflow);
    const auto* nodes = model.node_count() ? model.node(0) : nullptr;
    if (!nodes || reinterpret_cast<std::uintptr_t>(nodes) % alignof(Mt5Node))
        return fail(GpuPackError::invalid_model);
    Span input{}, destination{}, view{}, node_storage{};
    if (!span(source, count * sizeof(Mt5GeometryVertex), input) ||
        !span(output, capacity * sizeof(GpuNormal), destination) ||
        !span(&model, sizeof(model), view) || !span(nodes, model.node_count() * sizeof(Mt5Node), node_storage))
        return fail(GpuPackError::size_overflow);
    if (overlaps(input, destination) || overlaps(view, destination) || overlaps(node_storage, destination))
        return fail(GpuPackError::overlapping_storage);
    // Validate every actual source-node mapping, inverse and normalized result
    // before any caller output changes. Singular nodes still work in unlit paths.
    for (std::size_t i = 0; i < count; ++i) {
        GpuNormal packed{};
        const auto error = normal(model, source[i], packed);
        if (error != GpuPackError::none) return fail(error, i);
    }
    GpuPackResult result{};
    result.required_vertices = count;
    if (capacity < count) {
        result.error = GpuPackError::insufficient_capacity;
        return result;
    }
    for (std::size_t i = 0; i < count; ++i) {
        GpuNormal packed{};
        // No failing operation remains under the required immutable source/model
        // contract; all references and arithmetic were preflighted identically.
        (void)normal(model, source[i], packed);
        output[i] = packed;
    }
    result.written_vertices = count;
    return result;
}
} // namespace cedar
