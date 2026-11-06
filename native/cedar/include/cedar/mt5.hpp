#pragma once

// Original bounded implementation; MIT format attribution: docs/MT5-SCOPE.md.
#include <cstddef>
#include <cstdint>

namespace cedar {

enum class Mt5Error {
    none, invalid_buffer, overlapping_storage, size_limit, truncated,
    unknown_signature, invalid_offset, workspace_too_small, node_limit,
    hierarchy_cycle_or_alias, hierarchy_mismatch, depth_limit, nonfinite_value,
    unsupported_vertex_count, unsupported_command, unsupported_parent_index,
    unsupported_uv_state, invalid_vertex_index, invalid_node_index,
    invalid_corner_index, invalid_triangle_index, malformed_command,
    geometry_limit, not_parsed, no_mesh, end_of_strips, unresolved_parent_index,
    noninvertible_transform
};
struct Mt5Status {
    Mt5Error error = Mt5Error::none;
    std::uint32_t offset = 0;
    std::uint16_t command = 0;
    explicit operator bool() const noexcept { return error == Mt5Error::none; }
};
struct Mt5Vec3 { float x = 0, y = 0, z = 0; };
struct Mt5Vertex { Mt5Vec3 position{}, normal{}; };
// Row-major storage, row-vector multiplication. Translation is row 3.
struct Mt5Matrix { float values[16]{}; };
struct Mt5StaticVertex {
    Mt5Vertex local{};
    Mt5Vec3 world_position{};
    // Exact compatible-reader behavior: transformPosition(normal, world), with
    // translation. This is deliberately NOT presented as a lighting normal.
    Mt5Vec3 source_legacy_transformed_normal{};
};
struct Mt5Node {
    std::uint32_t file_offset = 0;
    std::uint32_t parent_index = UINT32_MAX;
    std::uint32_t id = 0;
    char name[4]{}; // fixed-width bytes, not a terminated string
    std::uint32_t unknown = 0;
    std::uint32_t rotation_units[3]{}; // raw 32-bit words; low 16 bits used below
    Mt5Vec3 rotation_degrees{};
    Mt5Vec3 scale{}, translation{}; // local SRT, not applied to vertex data
    Mt5Matrix local_transform{}, world_transform{}, inverse_world_transform{};
    bool inverse_world_available = false; // zero scale remains valid raw geometry
    std::uint32_t child_offset = 0, sibling_offset = 0, declared_parent_offset = 0;
    std::uint32_t mesh_offset = 0, polygon_type = 0;
    std::uint32_t vertices_offset = 0, vertex_count = 0, faces_offset = 0;
    std::uint32_t global_vertex_base = 0; // compatible reader's cumulative DFS base
    Mt5Vec3 center{};
    float radius = 0;
};
// Caller-owned traversal scratch. Contents have no meaning after parse returns.
struct Mt5Task { std::uint32_t offset = 0, parent_index = UINT32_MAX, depth = 0; };
struct Mt5Workspace {
    Mt5Node* nodes = nullptr;
    std::size_t node_capacity = 0;
    Mt5Task* tasks = nullptr;
    std::size_t task_capacity = 0;
};
struct Mt5Corner {
    std::int32_t raw_vertex_index = 0; // exact signed 16-bit on-disk index
    std::uint32_t source_node = UINT32_MAX;
    std::uint32_t vertex_index = 0; // local to source_node, including linked geometry
    std::uint32_t global_vertex_index = 0;
    bool has_uv = false, has_color = false;
    std::uint16_t raw_u = 0, raw_v = 0;
    float u = 0, v = 0;
    std::uint8_t bgra[4]{}; // exact optional on-disk bytes, no guessed swizzle
};

class Mt5StripCursor;
class Mt5Strip {
public:
    std::uint32_t corner_count() const noexcept { return count_; }
    std::uint16_t texture_index() const noexcept { return texture_; }
    std::uint16_t command() const noexcept { return command_; }
    std::uint16_t opaque_strip_word() const noexcept { return opaque_; }
    bool negative_length() const noexcept { return negative_; }
    bool mirror_u() const noexcept { return mirror_u_; }
    bool mirror_v() const noexcept { return mirror_v_; }
    bool uv_high_precision() const noexcept { return uvh_; }
    std::uint16_t uv_size() const noexcept { return uv_size_; }
    const std::uint8_t* raw_attributes() const noexcept { return attributes_; }
    std::size_t raw_attribute_bytes() const noexcept { return attribute_size_; }
    Mt5Status corner(std::uint32_t index, Mt5Corner& result) const noexcept;
    // Triangle-strip winding follows the compatible primary implementation:
    // even triangles [i,i+2,i+1], odd [i,i+1,i+2]. No degenerate removal.
    Mt5Status triangle(std::uint32_t index, Mt5Corner (&result)[3]) const noexcept;
private:
    friend class Mt5StripCursor;
    const std::uint8_t* data_ = nullptr;
    const std::uint8_t* attributes_ = nullptr;
    const Mt5Node* nodes_ = nullptr;
    std::size_t node_count_ = 0;
    std::uint32_t source_node_ = UINT32_MAX, global_base_ = 0, parent_node_ = UINT32_MAX;
    std::uint32_t count_ = 0, vertex_count_ = 0;
    std::uint16_t texture_ = 0, command_ = 0, opaque_ = 0, uv_size_ = 0;
    std::uint16_t attribute_size_ = 0;
    std::uint8_t stride_ = 0;
    bool uv_ = false, color_ = false, uvh_ = false;
    bool mirror_u_ = false, mirror_v_ = false, negative_ = false;
};

// Cursor/returned strips borrow immutable file bytes AND node workspace.
// No mutable output arrays or heap are used.
// Do not resume a cursor after an error other than end_of_strips.
class Mt5StripCursor {
public:
    Mt5Status next(Mt5Strip& result) noexcept;
private:
    friend class Mt5View;
    const std::uint8_t* data_ = nullptr;
    std::size_t offset_ = 0, end_ = 0;
    std::uint32_t vertex_count_ = 0, commands_seen_ = 0;
    const Mt5Node* nodes_ = nullptr;
    std::size_t node_count_ = 0;
    std::uint32_t source_node_ = UINT32_MAX, global_base_ = 0, parent_node_ = UINT32_MAX;
    const std::uint8_t* attributes_ = nullptr;
    std::uint16_t attribute_size_ = 0, texture_ = 0, uv_size_ = 0;
    std::uint16_t command_ = 0, opaque_ = 0, remaining_strips_ = 0;
    bool ended_ = false;
};

// Static local/linked-index MT5 geometry subset. Input begins at the MT5 file origin;
// caller retains immutable bytes AND the node workspace throughout view use.
// Do not modify workspace nodes after success. Parse failures invalidate the
// view; workspace may then contain incomplete/untrusted records. No game logic,
// runtime animation/skinning, texture decode, filesystem access or heap allocation.
class Mt5View {
public:
    static constexpr std::size_t max_file_bytes = 64u * 1024u * 1024u;
    static constexpr std::size_t max_nodes = 4096;
    static constexpr std::uint32_t max_depth = 256;
    static constexpr std::uint32_t max_geometry_items = 1024u * 1024u;
    Mt5Status parse(const void* bytes, std::size_t length, Mt5Workspace workspace) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    std::size_t node_count() const noexcept { return node_count_; }
    const Mt5Node* node(std::size_t index) const noexcept;
    Mt5Status vertex(std::size_t node_index, std::uint32_t vertex_index,
                      Mt5Vertex& result) const noexcept;
    Mt5Status static_vertex(std::size_t source_node, std::uint32_t vertex_index,
                             Mt5StaticVertex& result) const noexcept;
    // Only this inverse-dependent operation fails on singular/overflowing inverse.
    // Singular nodes retain raw geometry, SRT and forward-transform support.
    Mt5Status world_to_local(std::size_t node_index, Mt5Vec3 world_position,
                              Mt5Vec3& result) const noexcept;
    Mt5Status strips(std::size_t node_index, Mt5StripCursor& result) const noexcept;
    // Appended nodes/textures remain raw and are not interpreted by this view.
    const std::uint8_t* appended_data() const noexcept;
    std::size_t appended_bytes() const noexcept;
private:
    const std::uint8_t* data_ = nullptr;
    std::size_t length_ = 0, geometry_end_ = 0, node_count_ = 0;
    const Mt5Node* nodes_ = nullptr;
};

} // namespace cedar
