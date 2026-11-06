// Independent bounded subset derived from MIT ShenmueDK at
// 1dae443814b3755281d207a13f25571c0e51a5d1, Copyright(c)2019 Phil.
// Full attribution/limitations: docs/MT7-SCOPE.md; retained MIT notice in third_party.
#include "cedar/mt7.hpp"
#include <cstring>
#include <limits>

namespace cedar {
namespace {
Mt7Status fail(Mt7Error error, std::size_t offset = 0, std::size_t written = 0) noexcept {
    return {error, offset, written};
}
std::uint16_t u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
float f32(const std::uint8_t* p) noexcept {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "MT7 requires IEEE754 binary32");
    const auto word = u32(p);
    float result;
    std::memcpy(&result, &word, sizeof(result));
    return result;
}
bool finite(const std::uint8_t* p) noexcept { return (u32(p) & 0x7f800000u) != 0x7f800000u; }
bool contains(std::size_t offset, std::size_t length, std::size_t total) noexcept {
    return offset <= total && length <= total - offset;
}
bool range(const void* pointer, std::size_t length, std::uintptr_t& begin, std::uintptr_t& end) noexcept {
    begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    end = begin + length;
    return true;
}
bool is_signature(std::uint32_t value) noexcept {
    return value == 1480803405u || value == 927155277u || value == 1481655373u || value == 928007245u ||
        value == 1481589837u || value == 927941709u || value == 1481393229u || value == 927745101u;
}
Mt7Status read_node(const std::uint8_t* data, std::size_t size, std::size_t prefix,
                    std::uint32_t offset, std::uint32_t parent, Mt7NodeRecord& result) noexcept {
    if (offset < prefix || !contains(offset, 56, size)) return fail(Mt7Error::invalid_offset, offset);
    Mt7NodeRecord next;
    next.offset = offset;
    next.parent_index = parent;
    const auto* p = data + offset;
    next.id = u32(p);
    for (unsigned i = 0; i < 3; ++i) {
        if (!finite(p + 4 + i * 4)) return fail(Mt7Error::nonfinite_float, offset + 4 + i * 4);
        if (!finite(p + 28 + i * 4)) return fail(Mt7Error::nonfinite_float, offset + 28 + i * 4);
        next.position[i] = f32(p + 4 + i * 4);
        next.rotation_words[i] = u32(p + 16 + i * 4);
        next.scale[i] = f32(p + 28 + i * 4);
    }
    next.mesh_offset = u32(p + 40);
    next.child_offset = u32(p + 44);
    next.sibling_offset = u32(p + 48);
    next.parent_offset = u32(p + 52);
    // Only traversed links are interpreted. ParentOffset is source metadata;
    // the compatible implementation constructs parent links from traversal.
    if (next.mesh_offset && (next.mesh_offset < prefix || !contains(next.mesh_offset, 32, size)))
        return fail(Mt7Error::invalid_offset, next.mesh_offset);
    result = next;
    return {};
}
} // namespace

Mt7Status Mt7View::parse(const void* bytes, std::size_t size, Mt7NodeRecord* workspace,
                         std::size_t capacity) noexcept {
    *this = Mt7View{};
    if (!bytes || !workspace) return fail(Mt7Error::invalid_buffer);
    if (size > max_file_bytes) return fail(Mt7Error::resource_limit);
    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(*workspace)) return fail(Mt7Error::invalid_buffer);
    std::uintptr_t input_start, input_end, work_start, work_end, view_start, view_end;
    if (!range(bytes, size, input_start, input_end) ||
        !range(workspace, capacity * sizeof(*workspace), work_start, work_end) ||
        !range(this, sizeof(*this), view_start, view_end)) return fail(Mt7Error::invalid_buffer);
    if ((work_start < input_end && input_start < work_end) ||
        (work_start < view_end && view_start < work_end)) return fail(Mt7Error::overlapping_buffers);
    if (size < 16) return fail(Mt7Error::truncated, size);
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    Mt7Header info;
    info.signature = u32(data);
    if (!is_signature(info.signature)) return fail(Mt7Error::invalid_signature);
    info.declared_size = u32(data + 4); // Opaque: not established as a safe outer bound.
    info.first_node_offset = u32(data + 8);
    info.texture_count = u32(data + 12);
    if (info.texture_count > max_textures) return fail(Mt7Error::resource_limit, 12);
    info.texture_fields_offset = 16;
    const auto field_bytes = static_cast<std::size_t>(info.texture_count) * 20;
    const auto id_bytes = static_cast<std::size_t>(info.texture_count) * 8;
    if (!contains(16, field_bytes + id_bytes, size)) return fail(Mt7Error::truncated, size);
    info.texture_ids_offset = 16 + field_bytes;
    std::size_t prefix = info.texture_ids_offset + id_bytes;
    if (info.first_node_offset < prefix || !contains(info.first_node_offset, 56, size))
        return fail(Mt7Error::invalid_offset, 8);
    if (prefix != info.first_node_offset) {
        if (!contains(prefix, 4, info.first_node_offset)) return fail(Mt7Error::invalid_offset, prefix);
        info.node_table_count = u32(data + prefix);
        if (info.node_table_count > max_table_entries) return fail(Mt7Error::resource_limit, prefix);
        info.node_table_offset = prefix + 4;
        const auto table_bytes = info.node_table_count * 4;
        if (!contains(info.node_table_offset, table_bytes, info.first_node_offset))
            return fail(Mt7Error::invalid_offset, info.node_table_offset);
        prefix = info.node_table_offset + table_bytes;
    }
    info.structural_prefix_bytes = prefix;
    if (!capacity) return fail(Mt7Error::workspace_too_small);
    auto status = read_node(data, size, prefix, info.first_node_offset, UINT32_MAX, workspace[0]);
    if (!status) return status;
    std::size_t count = 1;
    for (std::size_t at = 0; at < count; ++at) {
        const auto current = workspace[at];
        const std::uint32_t links[2]{current.child_offset, current.sibling_offset};
        const std::uint32_t parents[2]{static_cast<std::uint32_t>(at), current.parent_index};
        for (unsigned link = 0; link < 2; ++link) {
            if (!links[link]) continue;
            for (std::size_t seen = 0; seen < count; ++seen)
                if (workspace[seen].offset == links[link])
                    return fail(Mt7Error::cycle_or_shared_node, links[link], count);
            if (count == max_nodes) return fail(Mt7Error::resource_limit, links[link], count);
            if (count == capacity) return fail(Mt7Error::workspace_too_small, links[link], count);
            status = read_node(data, size, prefix, links[link], parents[link], workspace[count]);
            if (!status) { status.nodes_written = count; return status; }
            ++count;
        }
    }
    data_ = data;
    size_ = size;
    nodes_ = workspace;
    node_count_ = count;
    header_ = info;
    return {Mt7Error::none, 0, count};
}
Mt7Status Mt7View::node(std::size_t index, Mt7NodeRecord& result) const noexcept {
    result = {};
    if (!valid()) return fail(Mt7Error::not_parsed);
    if (index >= node_count_) return fail(Mt7Error::invalid_node);
    result = nodes_[index];
    return {};
}
Mt7Status Mt7View::texture(std::uint32_t index, Mt7TextureRecord& result) const noexcept {
    result = {};
    if (!valid()) return fail(Mt7Error::not_parsed);
    if (index >= header_.texture_count) return fail(Mt7Error::invalid_texture);
    const auto fields = header_.texture_fields_offset + static_cast<std::size_t>(index) * 20;
    const auto id = header_.texture_ids_offset + static_cast<std::size_t>(index) * 8;
    std::memcpy(result.raw_fields, data_ + fields, 20);
    std::memcpy(result.texture_id, data_ + id, 8);
    return {};
}
Mt7Status Mt7View::node_table_entry(std::size_t index, std::uint32_t& raw_offset) const noexcept {
    raw_offset = 0;
    if (!valid()) return fail(Mt7Error::not_parsed);
    if (index >= header_.node_table_count) return fail(Mt7Error::invalid_table_index);
    raw_offset = u32(data_ + header_.node_table_offset + index * 4);
    return {};
}
Mt7Status Mt7View::geometry(std::size_t index, Mt7GeometryView& result) const noexcept {
    result = Mt7GeometryView{};
    if (!valid()) return fail(Mt7Error::not_parsed);
    if (index >= node_count_) return fail(Mt7Error::invalid_node);
    const auto mesh = static_cast<std::size_t>(nodes_[index].mesh_offset);
    if (!mesh) return fail(Mt7Error::no_mesh);
    if (!contains(mesh, 32, size_)) return fail(Mt7Error::invalid_offset, mesh);
    const auto* p = data_ + mesh;
    if (u32(p) != 825246296u) return fail(Mt7Error::invalid_signature, mesh); // XB01
    Mt7GeometryInfo info;
    info.mesh_offset = mesh;
    info.unknown = u32(p + 4);
    for (unsigned i = 0; i < 3; ++i) {
        if (!finite(p + 8 + i * 4)) return fail(Mt7Error::nonfinite_float, mesh + 8 + i * 4);
        info.center[i] = f32(p + 8 + i * 4);
    }
    if (!finite(p + 20)) return fail(Mt7Error::nonfinite_float, mesh + 20);
    info.radius = f32(p + 20);
    info.first_entry_words = u32(p + 24);
    info.command_size_words = u32(p + 28);
    // Exact seek equations in compatible XB01::Read, expressed without wrap.
    const std::uint64_t vertex_header = static_cast<std::uint64_t>(mesh) + 28u +
        static_cast<std::uint64_t>(info.command_size_words) * 4u;
    const std::uint64_t command_start = static_cast<std::uint64_t>(mesh) + 24u +
        static_cast<std::uint64_t>(info.first_entry_words) * 4u;
    if (vertex_header < mesh + 32 || vertex_header > size_ || size_ - vertex_header < 16)
        return fail(Mt7Error::invalid_offset, mesh + 28);
    if (command_start < mesh + 32 || command_start > vertex_header - 8 ||
        vertex_header - 8 - command_start < 20) return fail(Mt7Error::invalid_offset, mesh + 24);
    info.vertex_header_offset = static_cast<std::size_t>(vertex_header);
    info.command_offset = static_cast<std::size_t>(command_start);
    info.command_bytes = static_cast<std::size_t>(vertex_header - 8 - command_start);
    const auto* vertex_info = data_ + info.vertex_header_offset;
    info.vertex_unknown1 = u32(vertex_info);
    info.vertex_unknown2 = u32(vertex_info + 4);
    info.vertex_stride = u16(vertex_info + 8);
    info.vertex_unknown3 = u16(vertex_info + 10);
    info.vertex_bytes = u32(vertex_info + 12);
    if (info.vertex_stride != 12 && info.vertex_stride != 24 && info.vertex_stride != 32)
        return fail(Mt7Error::unsupported_vertex_stride, info.vertex_header_offset + 8);
    if (info.vertex_bytes % info.vertex_stride) return fail(Mt7Error::invalid_vertex_block, info.vertex_header_offset + 12);
    info.vertex_count = info.vertex_bytes / info.vertex_stride;
    if (info.vertex_count > max_vertices) return fail(Mt7Error::resource_limit, info.vertex_header_offset + 12);
    info.vertex_data_offset = info.vertex_header_offset + 16;
    if (!contains(info.vertex_data_offset, info.vertex_bytes, size_)) return fail(Mt7Error::truncated, size_);
    for (std::size_t offset = 0; offset < info.vertex_bytes; offset += 4)
        if (!finite(data_ + info.vertex_data_offset + offset))
            return fail(Mt7Error::nonfinite_float, info.vertex_data_offset + offset);
    const auto* group = data_ + info.command_offset;
    info.first_group_id = group[0];
    info.first_group_declared_size = u16(group + 1);
    for (unsigned i = 0; i < 8; ++i) info.first_group_values[i] = u16(group + 4 + i * 2);
    result.data_ = data_;
    result.info_ = info;
    return {};
}
Mt7Status Mt7GeometryView::vertex(std::uint32_t index, Mt7Vertex& result) const noexcept {
    result = {};
    if (!valid()) return fail(Mt7Error::not_parsed);
    if (index >= info_.vertex_count) return fail(Mt7Error::invalid_vertex);
    const auto* p = data_ + info_.vertex_data_offset + static_cast<std::size_t>(index) * info_.vertex_stride;
    for (unsigned i = 0; i < 3; ++i) result.position[i] = f32(p + i * 4);
    result.has_normal = info_.vertex_stride >= 24;
    result.has_uv = info_.vertex_stride == 32;
    if (result.has_normal) for (unsigned i = 0; i < 3; ++i) result.normal[i] = f32(p + 12 + i * 4);
    if (result.has_uv) for (unsigned i = 0; i < 2; ++i) result.uv[i] = f32(p + 24 + i * 4);
    return {};
}
Mt7Bytes Mt7GeometryView::raw_commands() const noexcept {
    return valid() ? Mt7Bytes{data_ + info_.command_offset, info_.command_bytes} : Mt7Bytes{};
}
Mt7Status Mt7GeometryView::raw_indices_at(std::size_t offset, Mt7RawIndexView& result) const noexcept {
    result = Mt7RawIndexView{};
    if (!valid()) return fail(Mt7Error::not_parsed);
    const auto end = info_.command_offset + info_.command_bytes;
    if (offset < info_.command_offset + 20 || !contains(offset, 12, end))
        return fail(Mt7Error::invalid_offset, offset);
    const auto* p = data_ + offset;
    if (p[0] != 0x10) return fail(Mt7Error::invalid_index_record, offset);
    Mt7IndexRecordInfo info;
    info.offset = offset;
    info.record_words = u16(p + 1);
    // Compatible reader loads this field through signed16-bit storage. Do not
    // claim a larger unsigned record profile that its seek does not establish.
    if (info.record_words > 32767u) return fail(Mt7Error::invalid_index_record, offset + 1);
    info.record_bytes = static_cast<std::size_t>(info.record_words) * 4;
    if (info.record_bytes < 12 || !contains(offset, info.record_bytes, end))
        return fail(Mt7Error::invalid_index_record, offset + 1);
    info.opaque_primitive = u32(p + 4);
    info.count = u32(p + 8);
    if (info.count > Mt7View::max_indices) return fail(Mt7Error::resource_limit, offset + 8);
    info.index_bytes = static_cast<std::size_t>(info.count) * 2;
    if (info.index_bytes > info.record_bytes - 12) return fail(Mt7Error::invalid_index_record, offset + 8);
    info.trailing_record_bytes = info.record_bytes - 12 - info.index_bytes;
    for (std::uint32_t i = 0; i < info.count; ++i)
        if (u16(p + 12 + static_cast<std::size_t>(i) * 2) >= info_.vertex_count)
            return fail(Mt7Error::index_out_of_range, offset + 12 + static_cast<std::size_t>(i) * 2);
    result.indices_ = p + 12;
    result.info_ = info;
    return {};
}
Mt7Status Mt7RawIndexView::index(std::uint32_t at, std::uint16_t& value) const noexcept {
    value = 0;
    if (!valid()) return fail(Mt7Error::not_parsed);
    if (at >= info_.count) return fail(Mt7Error::index_out_of_range);
    value = u16(indices_ + static_cast<std::size_t>(at) * 2);
    return {};
}
} // namespace cedar
