// Original exact MT5 resource-to-native draw assembly.
#pragma once
#include "cedar/mt5_geometry.hpp"
#include "cedar/model_textures.hpp"
#include "cedar/gpu_renderer.hpp"
namespace cedar {
enum class SceneAlpha : std::uint8_t { unspecified,opaque,straight,premultiplied };
struct SceneTextureBinding {
    std::uint32_t mesh_texture_index{};ModelTextureId id{};
    const GpuTexture* texture{};
    std::size_t package_index{SIZE_MAX};std::uint32_t package_texture_index{UINT32_MAX};
};
struct SceneDrawMaterial {GpuRendererConfig pipeline{};GpuLighting light{};SceneAlpha alpha{SceneAlpha::unspecified};};
struct SceneDrawBinding {
    Mt5DrawRecord source{};ModelTextureId id{};ModelTextureOrigin origin{};
    std::size_t texture_binding_index{},package_index{SIZE_MAX};
    std::uint32_t package_texture_index{UINT32_MAX};
};
enum class SceneDrawError : std::uint8_t { none,invalid_argument,invalid_buffer,insufficient_capacity,
    unavailable,invalid_range,missing_texture,texture_mismatch,source_failure,incompatible_material };
struct SceneDrawResult {
    SceneDrawError error{SceneDrawError::none};ModelTextureStatus source{};
    std::size_t draw_index{SIZE_MAX},written{};
    explicit operator bool() const noexcept {return error==SceneDrawError::none;}
};
// Source records must be the actual complete successful expanded MT5 extraction
// corresponding to this ready mesh. No packing, GPU upload or source guessing.
SceneDrawResult assemble_mt5_scene_draws(const Mt5TextureTable&,
    const Mt5DrawRecord*,std::size_t,const GpuMesh&,
    const SceneTextureBinding*,std::size_t,const SceneDrawMaterial*,
    const float (&camera)[16],const GpuRendererConfig& selected_pipeline,
    GpuDraw*,std::size_t,SceneDrawBinding*,std::size_t) noexcept;
}
