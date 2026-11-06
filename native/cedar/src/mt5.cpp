#include "cedar/mt5.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace cedar {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "MT5 requires IEEE754 binary32 floats");
std::uint16_t u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8u));
}
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8u) |
           (std::uint32_t{p[2]} << 16u) | (std::uint32_t{p[3]} << 24u);
}
std::int32_t signed16(std::uint16_t n) noexcept {
    return n & 0x8000u ? static_cast<std::int32_t>(n) - 65536 : n;
}
float f32(const std::uint8_t* p) noexcept {
    const auto word = u32(p);
    float result;
    std::memcpy(&result, &word, sizeof(result));
    return result;
}
Mt5Vec3 vec3(const std::uint8_t* p) noexcept { return {f32(p), f32(p + 4), f32(p + 8)}; }
bool finite(Mt5Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
Mt5Matrix identity() noexcept {
    Mt5Matrix matrix;
    for (unsigned i = 0; i < 4; ++i) matrix.values[i * 4 + i] = 1;
    return matrix;
}
bool finite(const Mt5Matrix& matrix) noexcept {
    for (const auto value : matrix.values) if (!std::isfinite(value)) return false;
    return true;
}
Mt5Matrix multiply(const Mt5Matrix& left, const Mt5Matrix& right) noexcept {
    Mt5Matrix result;
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned col = 0; col < 4; ++col) {
            const auto* a = left.values + row * 4;
            const auto* b = right.values + col;
            result.values[row * 4 + col] = ((a[0] * b[0] + a[1] * b[4]) + a[2] * b[8]) + a[3] * b[12];
        }
    }
    return result;
}
Mt5Matrix axis_rotation(unsigned axis, float degrees) noexcept {
    constexpr float radians_per_degree = 3.14159265358979323846f / 180.0f;
    const float angle = degrees * radians_per_degree;
    const float c = std::cos(-angle), s = std::sin(-angle), t = 1.0f - c;
    const float x = axis == 0 ? 1.0f : 0.0f;
    const float y = axis == 1 ? 1.0f : 0.0f;
    const float z = axis == 2 ? 1.0f : 0.0f;
    Mt5Matrix matrix = identity();
    matrix.values[0] = t * x * x + c;
    matrix.values[1] = t * x * y - s * z;
    matrix.values[2] = t * x * z + s * y;
    matrix.values[4] = t * x * y + s * z;
    matrix.values[5] = t * y * y + c;
    matrix.values[6] = t * y * z - s * x;
    matrix.values[8] = t * x * z - s * y;
    matrix.values[9] = t * y * z + s * x;
    matrix.values[10] = t * z * z + c;
    return matrix;
}
Mt5Matrix local_transform(const Mt5Node& node) noexcept {
    auto scale = identity();
    scale.values[0] = node.scale.x;
    scale.values[5] = node.scale.y;
    scale.values[10] = node.scale.z;
    auto translate = identity();
    translate.values[12] = node.translation.x;
    translate.values[13] = node.translation.y;
    translate.values[14] = node.translation.z;
    auto matrix = multiply(scale, axis_rotation(0, node.rotation_degrees.x));
    matrix = multiply(matrix, axis_rotation(1, node.rotation_degrees.y));
    matrix = multiply(matrix, axis_rotation(2, node.rotation_degrees.z));
    return multiply(matrix, translate);
}
Mt5Vec3 transform_position(Mt5Vec3 v, const Mt5Matrix& matrix) noexcept {
    const auto* m = matrix.values;
    return {((v.x * m[0] + v.y * m[4]) + v.z * m[8]) + m[12],
            ((v.x * m[1] + v.y * m[5]) + v.z * m[9]) + m[13],
            ((v.x * m[2] + v.y * m[6]) + v.z * m[10]) + m[14]};
}
bool inverse_affine(const Mt5Matrix& source, Mt5Matrix& output) noexcept {
    output = {};
    if (!finite(source)) return false;
    const auto* m = source.values;
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6],
                 g = m[8], h = m[9], i = m[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || determinant == 0.0) return false;
    const double linear[9] = {(e*i-f*h)/determinant, (c*h-b*i)/determinant, (b*f-c*e)/determinant,
                              (f*g-d*i)/determinant, (a*i-c*g)/determinant, (c*d-a*f)/determinant,
                              (d*h-e*g)/determinant, (b*g-a*h)/determinant, (a*e-b*d)/determinant};
    auto inverse = identity();
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned col = 0; col < 3; ++col) {
            const auto value = linear[row * 3 + col];
            if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max()) return false;
            inverse.values[row * 4 + col] = static_cast<float>(value);
        }
    }
    for (unsigned col = 0; col < 3; ++col) {
        const double value = -(static_cast<double>(m[12]) * linear[col] +
                               static_cast<double>(m[13]) * linear[3 + col] +
                               static_cast<double>(m[14]) * linear[6 + col]);
        if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max()) return false;
        inverse.values[12 + col] = static_cast<float>(value);
    }
    if (!finite(inverse)) return false;
    // Finite conversion alone must not label a rounded-to-singular inverse as
    // usable. This is an exact nonsingularity check, not a conditioning promise.
    const auto* v = inverse.values;
    const double converted_determinant = static_cast<double>(v[0]) *
        (static_cast<double>(v[5]) * v[10] - static_cast<double>(v[6]) * v[9]) -
        static_cast<double>(v[1]) * (static_cast<double>(v[4]) * v[10] - static_cast<double>(v[6]) * v[8]) +
        static_cast<double>(v[2]) * (static_cast<double>(v[4]) * v[9] - static_cast<double>(v[5]) * v[8]);
    if (!std::isfinite(converted_determinant) || converted_determinant == 0.0) return false;
    output = inverse;
    return true;
}
Mt5Status linked_index(std::int32_t raw, const Mt5Node* nodes, std::size_t count,
                        std::uint32_t parent, std::uint32_t& source_node,
                        std::uint32_t& local, std::uint32_t& global) noexcept {
    std::int64_t remaining = raw;
    for (std::uint32_t depth = 0; depth <= Mt5View::max_depth; ++depth) {
        if (!nodes || parent == UINT32_MAX || parent >= count || !nodes[parent].mesh_offset)
            return {Mt5Error::unresolved_parent_index};
        const auto& mesh = nodes[parent];
        // Exact mathematical form of the source's bounded 32-bit index mapping.
        // No root fallback to vertex zero is accepted.
        const auto mapped = static_cast<std::int64_t>(mesh.global_vertex_base) + mesh.vertex_count + remaining;
        if (mapped >= 0) {
            const auto candidate = static_cast<std::uint32_t>(mapped);
            std::size_t first = 0, last = count;
            while (first < last) {
                const auto middle = first + (last - first) / 2;
                if (nodes[middle].global_vertex_base <= candidate) first = middle + 1;
                else last = middle;
            }
            if (!first) return {Mt5Error::unresolved_parent_index};
            const auto owner = first - 1;
            const auto& record = nodes[owner];
            if (!record.mesh_offset || candidate < record.global_vertex_base ||
                candidate - record.global_vertex_base >= record.vertex_count)
                return {Mt5Error::unresolved_parent_index};
            source_node = static_cast<std::uint32_t>(owner);
            local = candidate - record.global_vertex_base;
            global = candidate;
            return {};
        }
        remaining = mapped;
        parent = mesh.parent_index;
    }
    return {Mt5Error::depth_limit};
}
bool range(std::size_t offset, std::size_t bytes, std::size_t end) noexcept {
    return offset <= end && bytes <= end - offset;
}
bool overlaps(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!an || !bn) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a);
    const auto y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < an : x - y < bn;
}
bool strip_command(std::uint16_t c) noexcept {
    return (c >= 0x10 && c <= 0x14) || (c >= 0x18 && c <= 0x1c);
}
bool has_uv(std::uint16_t c) noexcept { return c == 0x11 || c == 0x19 || c == 0x14 || c == 0x1c; }
bool has_color(std::uint16_t c) noexcept { return c == 0x12 || c == 0x1a || c == 0x14 || c == 0x1c; }
float normalized_uv(std::uint16_t raw, bool high, std::uint16_t size) noexcept {
    if (high) return static_cast<float>(raw) / 65536.0f;
    // Follow the compatible primary reader, including its exceptional high-word
    // path. Raw UVs remain available so this behavior is not hidden from callers.
    return raw < 61440u ? static_cast<float>(raw) / size : static_cast<float>(raw) / 4294967296.0f;
}
}

Mt5Status Mt5Strip::corner(std::uint32_t index, Mt5Corner& result) const noexcept {
    result = {};
    if (!data_) return {Mt5Error::not_parsed};
    if (index >= count_) return {Mt5Error::invalid_corner_index};
    const auto* encoded = data_ + static_cast<std::size_t>(index) * stride_;
    const auto vertex = signed16(u16(encoded));
    Mt5Corner candidate;
    candidate.raw_vertex_index = vertex;
    if (vertex < 0) {
        const auto status = linked_index(vertex, nodes_, node_count_, parent_node_, candidate.source_node,
                                         candidate.vertex_index, candidate.global_vertex_index);
        if (!status) return status;
    } else {
        if (static_cast<std::uint32_t>(vertex) >= vertex_count_)
            return {Mt5Error::invalid_vertex_index};
        candidate.source_node = source_node_;
        candidate.vertex_index = static_cast<std::uint32_t>(vertex);
        candidate.global_vertex_index = global_base_ + candidate.vertex_index;
    }
    candidate.has_uv = uv_;
    candidate.has_color = color_;
    std::size_t offset = 2;
    if (uv_) {
        candidate.raw_u = u16(encoded + offset);
        candidate.raw_v = u16(encoded + offset + 2);
        if (!uvh_ && !uv_size_ && (candidate.raw_u < 61440u || candidate.raw_v < 61440u))
            return {Mt5Error::unsupported_uv_state};
        candidate.u = normalized_uv(candidate.raw_u, uvh_, uv_size_);
        candidate.v = normalized_uv(candidate.raw_v, uvh_, uv_size_);
        offset += 4;
    }
    if (color_) std::memcpy(candidate.bgra, encoded + offset, sizeof(candidate.bgra));
    result = candidate;
    return {};
}

Mt5Status Mt5Strip::triangle(std::uint32_t index, Mt5Corner (&result)[3]) const noexcept {
    for (auto& corner : result) corner = {};
    if (!data_) return {Mt5Error::not_parsed};
    if (count_ < 3 || index >= count_ - 2) return {Mt5Error::invalid_triangle_index};
    const std::uint32_t indices[] = {index, index + ((index & 1u) ? 1u : 2u),
                                   index + ((index & 1u) ? 2u : 1u)};
    Mt5Corner decoded[3];
    for (unsigned i = 0; i < 3; ++i) {
        const auto status = corner(indices[i], decoded[i]);
        if (!status) return status;
    }
    for (unsigned i = 0; i < 3; ++i) result[i] = decoded[i];
    return {};
}

Mt5Status Mt5StripCursor::next(Mt5Strip& result) noexcept {
    result = {};
    if (!data_) return {Mt5Error::not_parsed};
    if (ended_) return {Mt5Error::end_of_strips};
    while (!remaining_strips_) {
        if (++commands_seen_ > Mt5View::max_geometry_items)
            return {Mt5Error::geometry_limit, static_cast<std::uint32_t>(offset_)};
        if (!range(offset_, 2, end_)) return {Mt5Error::truncated, static_cast<std::uint32_t>(offset_)};
        const auto at = static_cast<std::uint32_t>(offset_);
        command_ = u16(data_ + offset_);
        offset_ += 2;
        if (command_ == 0x8000u) { ended_ = true; return {Mt5Error::end_of_strips}; }
        if (command_ == 0 || command_ == 0xffffu) continue;
        if (command_ >= 2 && command_ <= 7) {
            if (!range(offset_, 2, end_)) return {Mt5Error::truncated, at, command_};
            attribute_size_ = u16(data_ + offset_);
            offset_ += 2;
            if (!range(offset_, attribute_size_, end_)) return {Mt5Error::truncated, at, command_};
            attributes_ = data_ + offset_;
            offset_ += attribute_size_;
            continue;
        }
        if (command_ == 9 || command_ == 0x0b) {
            if (!range(offset_, 2, end_)) return {Mt5Error::truncated, at, command_};
            const auto value = u16(data_ + offset_);
            offset_ += 2;
            if (command_ == 9) texture_ = value; else uv_size_ = value;
            continue;
        }
        if (!strip_command(command_)) return {Mt5Error::unsupported_command, at, command_};
        if (!range(offset_, 4, end_)) return {Mt5Error::truncated, at, command_};
        opaque_ = u16(data_ + offset_);
        remaining_strips_ = u16(data_ + offset_ + 2);
        offset_ += 4;
    }
    const auto at = static_cast<std::uint32_t>(offset_);
    if (!range(offset_, 2, end_)) return {Mt5Error::truncated, at, command_};
    const auto signed_length = signed16(u16(data_ + offset_));
    offset_ += 2;
    const auto count = static_cast<std::uint32_t>(signed_length < 0 ? -signed_length : signed_length);
    Mt5Strip candidate;
    candidate.uv_ = has_uv(command_);
    candidate.color_ = has_color(command_);
    candidate.stride_ = static_cast<std::uint8_t>(2 + (candidate.uv_ ? 4 : 0) + (candidate.color_ ? 4 : 0));
    if (count > (end_ - offset_) / candidate.stride_) return {Mt5Error::truncated, at, command_};
    candidate.data_ = data_ + offset_;
    candidate.attributes_ = attributes_;
    candidate.attribute_size_ = attribute_size_;
    candidate.count_ = count;
    candidate.vertex_count_ = vertex_count_;
    candidate.nodes_ = nodes_;
    candidate.node_count_ = node_count_;
    candidate.source_node_ = source_node_;
    candidate.global_base_ = global_base_;
    candidate.parent_node_ = parent_node_;
    candidate.texture_ = texture_;
    candidate.command_ = command_;
    candidate.opaque_ = opaque_;
    candidate.uv_size_ = uv_size_;
    candidate.uvh_ = attribute_size_ >= 1 && (attributes_[0] & 1u);
    candidate.mirror_u_ = attribute_size_ >= 11 && (attributes_[10] & 4u);
    candidate.mirror_v_ = attribute_size_ >= 11 && (attributes_[10] & 2u);
    candidate.negative_ = signed_length < 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        Mt5Corner corner;
        const auto status = candidate.corner(i, corner);
        if (!status) return {status.error, static_cast<std::uint32_t>(offset_ + i * candidate.stride_), command_};
    }
    offset_ += static_cast<std::size_t>(count) * candidate.stride_;
    --remaining_strips_;
    result = candidate;
    return {};
}

Mt5Status Mt5View::parse(const void* bytes, std::size_t length, Mt5Workspace storage) noexcept {
    data_ = nullptr;
    nodes_ = nullptr;
    length_ = geometry_end_ = node_count_ = 0;
    if (!bytes || (!storage.nodes && storage.node_capacity) || (!storage.tasks && storage.task_capacity))
        return {Mt5Error::invalid_buffer};
    if (length > max_file_bytes) return {Mt5Error::size_limit};
    if (length < 12) return {Mt5Error::truncated};
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    if (u32(data) != 1296257608u) return {Mt5Error::unknown_signature};
    const auto end = u32(data + 4);
    const auto root = u32(data + 8);
    if (end < 12 || root < 12) return {Mt5Error::invalid_offset};
    if (end > length) return {Mt5Error::truncated};
    const auto node_capacity = storage.node_capacity < max_nodes ? storage.node_capacity : max_nodes;
    const auto task_capacity = storage.task_capacity < max_nodes ? storage.task_capacity : max_nodes;
    if (!node_capacity || !task_capacity) return {Mt5Error::workspace_too_small};
    if (overlaps(bytes, length, storage.nodes, node_capacity * sizeof(Mt5Node)) ||
        overlaps(bytes, length, storage.tasks, task_capacity * sizeof(Mt5Task)) ||
        overlaps(storage.nodes, node_capacity * sizeof(Mt5Node), storage.tasks, task_capacity * sizeof(Mt5Task)))
        return {Mt5Error::overlapping_storage};

    std::size_t tasks = 1, count = 0;
    std::uint32_t total_vertices = 0, total_corners = 0, total_strips = 0, total_commands = 0;
    storage.tasks[0] = {root, UINT32_MAX, 0};
    while (tasks) {
        const auto task = storage.tasks[--tasks];
        if (task.depth > max_depth) return {Mt5Error::depth_limit, task.offset};
        if (task.offset < 12 || !range(task.offset, 64, end))
            return {Mt5Error::invalid_offset, task.offset};
        for (std::size_t i = 0; i < count; ++i) {
            const auto previous = storage.nodes[i].file_offset;
            if (task.offset == previous) return {Mt5Error::hierarchy_cycle_or_alias, task.offset};
            const auto distance = task.offset > previous ? task.offset - previous : previous - task.offset;
            if (distance < 64) return {Mt5Error::invalid_offset, task.offset};
        }
        if (count == max_nodes) return {Mt5Error::node_limit, task.offset};
        if (count == node_capacity) return {Mt5Error::workspace_too_small, task.offset};
        const auto* encoded = data + task.offset;
        Mt5Node node;
        node.file_offset = task.offset;
        node.parent_index = task.parent_index;
        node.id = u32(encoded);
        node.mesh_offset = u32(encoded + 4);
        for (unsigned i = 0; i < 3; ++i) node.rotation_units[i] = u32(encoded + 8 + 4 * i);
        node.rotation_degrees = {static_cast<float>(node.rotation_units[0] & 0xffffu) / 65535.0f * 360.0f,
                                 static_cast<float>(node.rotation_units[1] & 0xffffu) / 65535.0f * 360.0f,
                                 static_cast<float>(node.rotation_units[2] & 0xffffu) / 65535.0f * 360.0f};
        node.scale = vec3(encoded + 20);
        node.translation = vec3(encoded + 32);
        node.child_offset = u32(encoded + 44);
        node.sibling_offset = u32(encoded + 48);
        node.declared_parent_offset = u32(encoded + 52);
        std::memcpy(node.name, encoded + 56, sizeof(node.name));
        node.unknown = u32(encoded + 60);
        const auto expected_parent = task.parent_index == UINT32_MAX ? 0u : storage.nodes[task.parent_index].file_offset;
        if (node.declared_parent_offset != expected_parent)
            return {Mt5Error::hierarchy_mismatch, task.offset};
        if (!finite(node.scale) || !finite(node.translation)) return {Mt5Error::nonfinite_value, task.offset};
        node.local_transform = local_transform(node);
        node.world_transform = task.parent_index == UINT32_MAX ? node.local_transform :
            multiply(node.local_transform, storage.nodes[task.parent_index].world_transform);
        if (!finite(node.local_transform) || !finite(node.world_transform))
            return {Mt5Error::nonfinite_value, task.offset};
        node.inverse_world_available = inverse_affine(node.world_transform, node.inverse_world_transform);
        node.global_vertex_base = total_vertices;
        if (node.mesh_offset) {
            if (node.mesh_offset < 12 || !range(node.mesh_offset, 32, end))
                return {Mt5Error::invalid_offset, node.mesh_offset};
            const auto* mesh = data + node.mesh_offset;
            node.polygon_type = u32(mesh);
            node.vertices_offset = u32(mesh + 4);
            const auto vertex_word = u32(mesh + 8);
            if (vertex_word & 0x80000000u) return {Mt5Error::unsupported_vertex_count, node.mesh_offset};
            node.vertex_count = vertex_word;
            node.faces_offset = u32(mesh + 12);
            node.center = vec3(mesh + 16);
            node.radius = f32(mesh + 28);
            if (!finite(node.center) || !std::isfinite(node.radius))
                return {Mt5Error::nonfinite_value, node.mesh_offset};
            if (node.vertex_count > max_geometry_items - total_vertices)
                return {Mt5Error::geometry_limit, node.vertices_offset};
            if (node.vertex_count && (node.vertices_offset < 12 ||
                !range(node.vertices_offset, static_cast<std::size_t>(node.vertex_count) * 24, end)))
                return {Mt5Error::invalid_offset, node.vertices_offset};
            if (!node.vertex_count && node.vertices_offset &&
                (node.vertices_offset < 12 || !range(node.vertices_offset, 0, end)))
                return {Mt5Error::invalid_offset, node.vertices_offset};
            for (std::uint32_t i = 0; i < node.vertex_count; ++i) {
                const auto* vertex = data + node.vertices_offset + static_cast<std::size_t>(i) * 24;
                if (!finite(vec3(vertex)) || !finite(vec3(vertex + 12)))
                    return {Mt5Error::nonfinite_value, node.vertices_offset + i * 24};
            }
            total_vertices += node.vertex_count;
            if (node.faces_offset < 12 || !range(node.faces_offset, 2, end))
                return {Mt5Error::invalid_offset, node.faces_offset};
            Mt5StripCursor cursor;
            cursor.data_ = data;
            cursor.offset_ = node.faces_offset;
            cursor.end_ = end;
            cursor.vertex_count_ = node.vertex_count;
            cursor.nodes_ = storage.nodes;
            cursor.node_count_ = count;
            cursor.source_node_ = static_cast<std::uint32_t>(count);
            cursor.global_base_ = node.global_vertex_base;
            cursor.parent_node_ = node.parent_index;
            for (;;) {
                const auto old_commands = cursor.commands_seen_;
                Mt5Strip strip;
                const auto status = cursor.next(strip);
                const auto added_commands = cursor.commands_seen_ - old_commands;
                if (added_commands > max_geometry_items - total_commands)
                    return {Mt5Error::geometry_limit, node.faces_offset};
                total_commands += added_commands;
                if (status.error == Mt5Error::end_of_strips) break;
                if (!status) return status;
                if (total_strips == max_geometry_items || strip.corner_count() > max_geometry_items - total_corners)
                    return {Mt5Error::geometry_limit, node.faces_offset};
                ++total_strips;
                total_corners += strip.corner_count();
            }
        }
        const auto this_index = static_cast<std::uint32_t>(count);
        storage.nodes[count++] = node;
        // Push sibling first so child is visited first, matching primary DFS.
        if (node.sibling_offset) {
            if (tasks == task_capacity) return {Mt5Error::workspace_too_small, node.sibling_offset};
            storage.tasks[tasks++] = {node.sibling_offset, task.parent_index, task.depth};
        }
        if (node.child_offset) {
            if (tasks == task_capacity) return {Mt5Error::workspace_too_small, node.child_offset};
            storage.tasks[tasks++] = {node.child_offset, this_index, task.depth + 1};
        }
    }
    data_ = data;
    length_ = length;
    geometry_end_ = end;
    nodes_ = storage.nodes;
    node_count_ = count;
    return {};
}

const Mt5Node* Mt5View::node(std::size_t index) const noexcept {
    return parsed() && index < node_count_ ? nodes_ + index : nullptr;
}

Mt5Status Mt5View::vertex(std::size_t node_index, std::uint32_t index, Mt5Vertex& result) const noexcept {
    result = {};
    if (!parsed()) return {Mt5Error::not_parsed};
    const auto* record = node(node_index);
    if (!record) return {Mt5Error::invalid_node_index};
    if (!record->mesh_offset) return {Mt5Error::no_mesh};
    if (index >= record->vertex_count) return {Mt5Error::invalid_vertex_index};
    const auto* bytes = data_ + record->vertices_offset + static_cast<std::size_t>(index) * 24;
    result = {vec3(bytes), vec3(bytes + 12)};
    return {};
}

Mt5Status Mt5View::strips(std::size_t node_index, Mt5StripCursor& result) const noexcept {
    result = {};
    if (!parsed()) return {Mt5Error::not_parsed};
    const auto* record = node(node_index);
    if (!record) return {Mt5Error::invalid_node_index};
    if (!record->mesh_offset) return {Mt5Error::no_mesh};
    result.data_ = data_;
    result.offset_ = record->faces_offset;
    result.end_ = geometry_end_;
    result.vertex_count_ = record->vertex_count;
    result.nodes_ = nodes_;
    result.node_count_ = node_count_;
    result.source_node_ = static_cast<std::uint32_t>(node_index);
    result.global_base_ = record->global_vertex_base;
    result.parent_node_ = record->parent_index;
    return {};
}

Mt5Status Mt5View::static_vertex(std::size_t source_node, std::uint32_t index,
                                Mt5StaticVertex& result) const noexcept {
    result = {};
    Mt5StaticVertex candidate;
    const auto status = vertex(source_node, index, candidate.local);
    if (!status) return status;
    const auto& matrix = nodes_[source_node].world_transform;
    candidate.world_position = transform_position(candidate.local.position, matrix);
    candidate.source_legacy_transformed_normal = transform_position(candidate.local.normal, matrix);
    if (!finite(candidate.world_position) || !finite(candidate.source_legacy_transformed_normal))
        return {Mt5Error::nonfinite_value, nodes_[source_node].file_offset};
    result = candidate;
    return {};
}

Mt5Status Mt5View::world_to_local(std::size_t node_index, Mt5Vec3 position,
                                 Mt5Vec3& result) const noexcept {
    result = {};
    if (!parsed()) return {Mt5Error::not_parsed};
    const auto* record = node(node_index);
    if (!record) return {Mt5Error::invalid_node_index};
    if (!finite(position)) return {Mt5Error::nonfinite_value, record->file_offset};
    if (!record->inverse_world_available) return {Mt5Error::noninvertible_transform, record->file_offset};
    const auto candidate = transform_position(position, record->inverse_world_transform);
    if (!finite(candidate)) return {Mt5Error::nonfinite_value, record->file_offset};
    result = candidate;
    return {};
}

const std::uint8_t* Mt5View::appended_data() const noexcept {
    return parsed() ? data_ + geometry_end_ : nullptr;
}
std::size_t Mt5View::appended_bytes() const noexcept {
    return parsed() ? length_ - geometry_end_ : 0;
}

} // namespace cedar
