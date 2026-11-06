// SPDX-License-Identifier: GPL-3.0-only
// Authored MAPINFO CHRS recognition profile; docs/SCENE-RESOURCE.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
struct SceneResourceString { const std::uint8_t* bytes{}; std::size_t size{}; };
struct ScenePlacementRecord {
    char object_tag[4]{};
    SceneResourceString model{}, image{};
    std::size_t record_offset{}, record_end{}, model_string_offset{};
    std::uint32_t property_count{};
    float position[3]{};
    bool has_angle{}, yaw_only{}, has_image{};
    // Source identifies degrees. Yaw-only preserves its single Y value;
    // X/Z zeros are absent components, not an inferred complete orientation.
    float angle_degrees[3]{};
    std::size_t position_property_offset{}, angle_property_offset{}, image_property_offset{};
};
enum class SceneResourceError : std::uint8_t {
    none, invalid_buffer, address_overflow, overlapping_storage, resource_limit,
    missing_chrs, missing_strg, malformed_chunk, insufficient_capacity,
    malformed_record, missing_position, unsupported_property, duplicate_property,
    non_finite
};
struct SceneResourceResult {
    SceneResourceError error{SceneResourceError::none};
    std::size_t offset{}, required_records{};
    explicit operator bool() const noexcept { return error == SceneResourceError::none; }
};
class SceneResourceView {
public:
    static constexpr std::size_t max_bytes=64u*1024u*1024u, max_records=16384, max_string=1024;
    // First validated CHRS marker and first following STRG marker, following
    // pinned authored recognition rules; not a universal container decoder.
    // After safe span validation, failure clears view; workspace may change and
    // is invalid on failure. Invalid/aliased buffers leave prior view untouched.
    // All input/workspace/view spans disjoint. Input/workspace remain immutable
    // while borrowed; no implicit placement/rotation/scale/entrypoint defaults.
    SceneResourceResult parse(const std::uint8_t*,std::size_t,
        ScenePlacementRecord*,std::size_t capacity) noexcept;
    bool parsed() const noexcept { return parsed_; }
    std::size_t record_count() const noexcept { return count_; }
    const ScenePlacementRecord* record(std::size_t index) const noexcept {
        return parsed_ && index<count_ ? records_+index : nullptr;
    }
private:
    ScenePlacementRecord* records_{};
    std::size_t count_{};
    bool parsed_{};
};
} // namespace cedar
