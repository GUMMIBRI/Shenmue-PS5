#pragma once

// Original bounded resource linkage; compatible MIT evidence: MODEL-TEXTURES.md.
#include "cedar/mt5.hpp"
#include "cedar/pkf.hpp"

#include <cstddef>
#include <cstdint>

namespace cedar {

struct ModelTextureId { std::uint8_t bytes[8]{}; };
enum class ModelTextureError {
    none, invalid_model, not_parsed, truncated, malformed_node, unsupported_node,
    unsupported_layout, duplicate_node, count_mismatch, size_limit, missing_id,
    invalid_texl_index, invalid_buffer, invalid_package, package_failure,
    not_found, ambiguous, search_limit
};
struct ModelTextureStatus {
    ModelTextureError error = ModelTextureError::none;
    std::size_t appended_offset = 0; // relative to Mt5View::appended_data()
    std::size_t package_index = SIZE_MAX;
    PkfStatus package_status{};
    explicit operator bool() const noexcept { return error == ModelTextureError::none; }
};
enum class ModelTextureOrigin { embedded_texn, name_reference };
struct ModelTextureReference {
    std::uint32_t texture_index = 0;
    ModelTextureId id{};
    ModelTextureOrigin origin = ModelTextureOrigin::name_reference;
    PkfBytes embedded_body{}; // raw PVR-side bytes only for embedded TEXN
};
struct ModelTexlRecord {
    std::uint32_t texture_id_offset = 0, unknown1 = 0, unknown2 = 0;
};

// Mesh texture_index association follows the compatible primary: all embedded
// TEXN IDs in encounter order, then all NAME IDs in stored order. TEXD checks
// total count. TEXL offsets/PTRL are opaque metadata, not invented remapping.
// No allocation, file lookup, material/shader creation or default texture.
class Mt5TextureTable {
public:
    static constexpr std::uint32_t max_textures = 1024u * 1024u;
    ModelTextureStatus parse(const Mt5View& model) noexcept;
    bool parsed() const noexcept { return data_ != nullptr; }
    std::uint32_t texture_count() const noexcept { return embedded_count_ + name_count_; }
    std::uint32_t embedded_count() const noexcept { return embedded_count_; }
    std::uint32_t name_count() const noexcept { return name_count_; }
    bool has_texd() const noexcept { return has_texd_; }
    std::uint32_t declared_texture_count() const noexcept { return declared_count_; }
    std::uint32_t texl_count() const noexcept { return texl_count_; }
    std::uint32_t opaque_texl_word() const noexcept { return texl_unknown_; }
    ModelTextureStatus texture(std::uint32_t mesh_texture_index,
                                ModelTextureReference& result) const noexcept;
    ModelTextureStatus texl_record(std::uint32_t index, ModelTexlRecord& result) const noexcept;
    PkfBytes opaque_ptrl_body() const noexcept { return ptrl_; }
private:
    const std::uint8_t* data_ = nullptr;
    std::size_t length_ = 0;
    const std::uint8_t* names_ = nullptr;
    const std::uint8_t* texl_ = nullptr;
    std::uint32_t embedded_count_ = 0, name_count_ = 0;
    std::uint32_t declared_count_ = 0, texl_count_ = 0, texl_unknown_ = 0;
    bool has_texd_ = false;
    PkfBytes ptrl_{};
};

enum class ModelTextureLookupPolicy {
    // First package containing this ID wins; duplicates inside that package fail.
    first_matching_package,
    // All supplied packages must contain at most one total matching TEXN entry.
    unique_across_packages
};
struct ModelTextureMatch {
    ModelTextureId id{};
    std::size_t package_index = SIZE_MAX;
    std::uint32_t texture_index = UINT32_MAX;
    PkfTextureView texture{};
};
constexpr std::size_t model_texture_package_limit = 256;
constexpr std::size_t model_texture_search_limit = 1024u * 1024u;

// Exact opaque eight-byte equality; no filename hashing, case folding, endian
// conversion or automatic package discovery. Policy and package order are caller
// supplied. Missing/invalid packages and ambiguous IDs never produce a fallback.
ModelTextureStatus find_pkf_texture(const ModelTextureId& id,
                                     const PkfView* const* packages, std::size_t package_count,
                                     ModelTextureLookupPolicy policy, ModelTextureMatch& result) noexcept;
ModelTextureStatus resolve_mt5_texture(const Mt5TextureTable& table, std::uint32_t mesh_texture_index,
                                        const PkfView* const* packages, std::size_t package_count,
                                        ModelTextureLookupPolicy policy, ModelTextureMatch& result) noexcept;

// Lifetime: table/reference spans borrow immutable model bytes; matched texture
// spans borrow immutable selected PKF bytes. Retain those bytes throughout use.
// Result objects are valid only on success and are cleared on every failure.
// No image decode, upload, authenticity or completed draw binding is implied.
} // namespace cedar
