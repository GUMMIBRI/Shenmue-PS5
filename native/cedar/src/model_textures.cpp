#include "cedar/model_textures.hpp"

#include <cstring>

namespace cedar {
namespace {
constexpr std::uint32_t texd = 0x44584554u, texl = 0x4c584554u, name = 0x454d414eu;
constexpr std::uint32_t ptrl = 0x4c525450u, texn = 0x4e584554u;
std::uint32_t word32(const std::uint8_t* bytes) noexcept {
    return std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8u) |
           (std::uint32_t{bytes[2]} << 16u) | (std::uint32_t{bytes[3]} << 24u);
}
ModelTextureStatus fail(ModelTextureError error, std::size_t offset = 0) noexcept {
    ModelTextureStatus result;
    result.error = error;
    result.appended_offset = offset;
    return result;
}
ModelTextureStatus package_failure(ModelTextureError error, std::size_t package,
                                    PkfStatus status = {}) noexcept {
    auto result = fail(error);
    result.package_index = package;
    result.package_status = status;
    return result;
}
}

ModelTextureStatus Mt5TextureTable::parse(const Mt5View& model) noexcept {
    *this = {};
    if (!model.parsed()) return fail(ModelTextureError::invalid_model);
    const auto* data = model.appended_data();
    const auto length = model.appended_bytes();
    if (!data) return fail(ModelTextureError::invalid_model);
    Mt5TextureTable candidate;
    candidate.data_ = data;
    candidate.length_ = length;
    bool has_name = false, has_texl = false, has_ptrl = false;
    std::size_t offset = 0;
    while (offset < length) {
        if (length - offset < 8) return fail(ModelTextureError::truncated, offset);
        const auto* encoded = data + offset;
        const auto signature = word32(encoded);
        const auto size = word32(encoded + 4);
        if (size < 8) return fail(ModelTextureError::malformed_node, offset);
        if (size > length - offset) return fail(ModelTextureError::truncated, offset);
        const auto body = size - 8u;
        switch (signature) {
        case texd:
            if (candidate.has_texd_) return fail(ModelTextureError::duplicate_node, offset);
            if (body != 4) return fail(ModelTextureError::unsupported_layout, offset);
            candidate.has_texd_ = true;
            candidate.declared_count_ = word32(encoded + 8);
            if (candidate.declared_count_ > max_textures) return fail(ModelTextureError::size_limit, offset);
            break;
        case name:
            if (has_name) return fail(ModelTextureError::duplicate_node, offset);
            if (body % 8u) return fail(ModelTextureError::malformed_node, offset);
            has_name = true;
            candidate.names_ = encoded + 8;
            candidate.name_count_ = body / 8u;
            if (candidate.name_count_ > max_textures) return fail(ModelTextureError::size_limit, offset);
            break;
        case texl: {
            if (has_texl) return fail(ModelTextureError::duplicate_node, offset);
            if (body < 8) return fail(ModelTextureError::truncated, offset);
            has_texl = true;
            candidate.texl_unknown_ = word32(encoded + 8);
            candidate.texl_count_ = word32(encoded + 12);
            if (candidate.texl_count_ > max_textures) return fail(ModelTextureError::size_limit, offset);
            if (candidate.texl_count_ != (body - 8u) / 12u || (body - 8u) % 12u)
                return fail(ModelTextureError::malformed_node, offset);
            candidate.texl_ = encoded + 16;
            break;
        }
        case ptrl:
            if (has_ptrl) return fail(ModelTextureError::duplicate_node, offset);
            has_ptrl = true;
            candidate.ptrl_ = {encoded + 8, body};
            break;
        case texn:
            if (body < 8) return fail(ModelTextureError::malformed_node, offset);
            if (candidate.embedded_count_ == max_textures) return fail(ModelTextureError::size_limit, offset);
            ++candidate.embedded_count_;
            break;
        default:
            return fail(ModelTextureError::unsupported_node, offset);
        }
        offset += size;
    }
    if (candidate.name_count_ > max_textures - candidate.embedded_count_)
        return fail(ModelTextureError::size_limit);
    if (candidate.has_texd_ && candidate.declared_count_ != candidate.texture_count())
        return fail(ModelTextureError::count_mismatch);
    *this = candidate;
    return {};
}

ModelTextureStatus Mt5TextureTable::texture(std::uint32_t index,
                                           ModelTextureReference& result) const noexcept {
    result = {};
    if (!parsed()) return fail(ModelTextureError::not_parsed);
    if (index >= texture_count()) return fail(ModelTextureError::missing_id);
    ModelTextureReference reference;
    reference.texture_index = index;
    if (index >= embedded_count_) {
        reference.origin = ModelTextureOrigin::name_reference;
        std::memcpy(reference.id.bytes, names_ + static_cast<std::size_t>(index - embedded_count_) * 8, 8);
        result = reference;
        return {};
    }
    std::uint32_t embedded_index = 0;
    for (std::size_t offset = 0; offset < length_;) {
        const auto* encoded = data_ + offset;
        const auto size = word32(encoded + 4); // immutable, already fully checked
        if (word32(encoded) == texn && embedded_index++ == index) {
            reference.origin = ModelTextureOrigin::embedded_texn;
            std::memcpy(reference.id.bytes, encoded + 8, 8);
            reference.embedded_body = {encoded + 16, size - 16u};
            result = reference;
            return {};
        }
        offset += size;
    }
    return fail(ModelTextureError::missing_id); // immutable-source invariant failure
}

ModelTextureStatus Mt5TextureTable::texl_record(std::uint32_t index,
                                               ModelTexlRecord& result) const noexcept {
    result = {};
    if (!parsed()) return fail(ModelTextureError::not_parsed);
    if (index >= texl_count_) return fail(ModelTextureError::invalid_texl_index);
    const auto* encoded = texl_ + static_cast<std::size_t>(index) * 12;
    result = {word32(encoded), word32(encoded + 4), word32(encoded + 8)};
    return {};
}

ModelTextureStatus find_pkf_texture(const ModelTextureId& id,
    const PkfView* const* packages, std::size_t package_count,
    ModelTextureLookupPolicy policy, ModelTextureMatch& result) noexcept {
    const auto requested_id = id; // permit find_pkf_texture(result.id, ..., result)
    result = {};
    if ((!packages && package_count) ||
        (policy != ModelTextureLookupPolicy::first_matching_package &&
         policy != ModelTextureLookupPolicy::unique_across_packages))
        return fail(ModelTextureError::invalid_buffer);
    if (package_count > model_texture_package_limit) return fail(ModelTextureError::size_limit);
    ModelTextureMatch selected;
    bool found = false;
    std::size_t scanned = 0;
    for (std::size_t package = 0; package < package_count; ++package) {
        if (!packages[package] || !packages[package]->parsed())
            return package_failure(ModelTextureError::invalid_package, package, {PkfError::not_parsed});
        const auto& view = *packages[package];
        if (view.header().texture_count > model_texture_search_limit - scanned)
            return package_failure(ModelTextureError::search_limit, package);
        scanned += view.header().texture_count;
        bool found_in_package = false;
        auto cursor = view.textures();
        for (std::uint32_t index = 0; index < view.header().texture_count; ++index) {
            PkfTextureView texture;
            const auto status = cursor.next(texture);
            if (!status) return package_failure(ModelTextureError::package_failure, package, status);
            if (std::memcmp(texture.texture_id, requested_id.bytes, sizeof(requested_id.bytes)) != 0) continue;
            if (found_in_package || (found && policy == ModelTextureLookupPolicy::unique_across_packages))
                return package_failure(ModelTextureError::ambiguous, package);
            selected.id = requested_id;
            selected.package_index = package;
            selected.texture_index = texture.index;
            selected.texture = texture;
            found = found_in_package = true;
        }
        // Finish scanning the selected package before honoring its priority, so
        // an ambiguous duplicate inside it is never hidden by first-match lookup.
        if (found_in_package && policy == ModelTextureLookupPolicy::first_matching_package) {
            result = selected;
            return {};
        }
    }
    if (!found) return fail(ModelTextureError::not_found);
    result = selected;
    return {};
}

ModelTextureStatus resolve_mt5_texture(const Mt5TextureTable& table, std::uint32_t index,
    const PkfView* const* packages, std::size_t package_count,
    ModelTextureLookupPolicy policy, ModelTextureMatch& result) noexcept {
    result = {};
    ModelTextureReference reference;
    const auto status = table.texture(index, reference);
    if (!status) return status;
    return find_pkf_texture(reference.id, packages, package_count, policy, result);
}

} // namespace cedar
