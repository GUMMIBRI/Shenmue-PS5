// Original bounded subset derived from independently MIT ShenmueDK.
// Pin1dae443814b3755281d207a13f25571c0e51a5d1, Copyright(c)2019 Phil.
// Preserve third_party/licenses/shenmuedk-MIT.txt; see docs/MT7-SCOPE.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class Mt7Error : std::uint8_t {
    none, invalid_buffer, overlapping_buffers, truncated, invalid_signature,
    invalid_offset, resource_limit, workspace_too_small, cycle_or_shared_node,
    nonfinite_float, unsupported_vertex_stride, invalid_vertex_block,
    invalid_index_record, index_out_of_range, not_parsed, invalid_node,
    invalid_vertex, invalid_texture, invalid_table_index, no_mesh
};
struct Mt7Status {
    Mt7Error error{Mt7Error::none};
    std::size_t offset{};
    std::size_t nodes_written{}; // Failed parse may alter this many workspace records.
    explicit operator bool() const noexcept { return error == Mt7Error::none; }
};
struct Mt7Bytes { const std::uint8_t* data{}; std::size_t size{}; };
struct Mt7Header {
    std::uint32_t signature{}, declared_size{}, first_node_offset{}, texture_count{};
    std::size_t texture_fields_offset{}, texture_ids_offset{};
    std::size_t node_table_offset{}, node_table_count{}, structural_prefix_bytes{};
};
struct Mt7TextureRecord {
    std::uint8_t raw_fields[20]{}; // Source's unsafe width/height interpretation is NOT copied.
    std::uint8_t texture_id[8]{};
};
struct Mt7NodeRecord {
    std::uint32_t offset{}, id{}, parent_index{UINT32_MAX};
    float position[3]{}, scale[3]{};
    std::uint32_t rotation_words[3]{}; // Source header/node code disagree on numeric interpretation.
    std::uint32_t mesh_offset{}, child_offset{}, sibling_offset{}, parent_offset{};
};
struct Mt7Vertex {
    float position[3]{}, normal[3]{}, uv[2]{};
    bool has_normal{}, has_uv{}; // Absent fields have no decoded semantic value.
};
struct Mt7GeometryInfo {
    std::size_t mesh_offset{}, vertex_header_offset{}, vertex_data_offset{};
    std::size_t command_offset{}, command_bytes{};
    std::uint32_t unknown{}, first_entry_words{}, command_size_words{};
    float center[3]{}, radius{};
    std::uint32_t vertex_unknown1{}, vertex_unknown2{}, vertex_bytes{}, vertex_count{};
    std::uint16_t vertex_stride{}, vertex_unknown3{};
    std::uint8_t first_group_id{};
    std::uint16_t first_group_declared_size{}, first_group_values[8]{};
};
struct Mt7IndexRecordInfo {
    std::size_t offset{}, record_bytes{}, index_bytes{}, trailing_record_bytes{};
    std::uint16_t record_words{};
    std::uint32_t opaque_primitive{}, count{}; // No triangle/strip topology is established.
};
class Mt7RawIndexView final {
public:
    bool valid() const noexcept { return indices_ != nullptr; }
    const Mt7IndexRecordInfo& metadata() const noexcept { return info_; }
    Mt7Status index(std::uint32_t at, std::uint16_t& value) const noexcept;
private:
    friend class Mt7GeometryView;
    const std::uint8_t* indices_{};
    Mt7IndexRecordInfo info_{};
};
class Mt7GeometryView final {
public:
    bool valid() const noexcept { return data_ != nullptr; }
    const Mt7GeometryInfo& metadata() const noexcept { return info_; }
    Mt7Status vertex(std::uint32_t index, Mt7Vertex& result) const noexcept;
    Mt7Bytes raw_commands() const noexcept;
    // Caller supplies an exact file offset; no command scan or unknown skip.
    // Verifies type0x10, word-count extent, explicit count and every vertex index.
    Mt7Status raw_indices_at(std::size_t file_offset, Mt7RawIndexView& result) const noexcept;
private:
    friend class Mt7View;
    const std::uint8_t* data_{};
    Mt7GeometryInfo info_{};
};

// Non-owning standalone-base-zero MT7 subset. Input and workspace stay alive and
// immutable after successful parse. Workspace holds the validated child/sibling
// hierarchy; no heap/recursion. Parent pointers and rotations remain raw metadata.
// No animation/skinning, subnode evaluation, material resolution, unknown-command
// walking or triangle generation is performed. Supported vertices: strides12/24/32.
// Geometry creation validates actual streams; parse alone validates hierarchy only.
class Mt7View final {
public:
    static constexpr std::size_t max_file_bytes = 256u * 1024u * 1024u;
    static constexpr std::size_t max_nodes = 4096;
    static constexpr std::size_t max_textures = 65536;
    static constexpr std::size_t max_table_entries = 65536;
    static constexpr std::size_t max_vertices = 1024u * 1024u;
    static constexpr std::size_t max_indices = 1024u * 1024u;
    Mt7Status parse(const void* bytes, std::size_t size,
                    Mt7NodeRecord* workspace, std::size_t node_capacity) noexcept;
    bool valid() const noexcept { return data_ != nullptr; }
    const Mt7Header& header() const noexcept { return header_; }
    std::size_t node_count() const noexcept { return node_count_; }
    Mt7Status node(std::size_t index, Mt7NodeRecord& result) const noexcept;
    Mt7Status texture(std::uint32_t index, Mt7TextureRecord& result) const noexcept;
    Mt7Status node_table_entry(std::size_t index, std::uint32_t& raw_offset) const noexcept;
    Mt7Status geometry(std::size_t node_index, Mt7GeometryView& result) const noexcept;
private:
    const std::uint8_t* data_{};
    std::size_t size_{}, node_count_{};
    const Mt7NodeRecord* nodes_{};
    Mt7Header header_{};
};
} // namespace cedar
