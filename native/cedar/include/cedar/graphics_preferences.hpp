// Original app-owned preferences bridge; never an original Shenmue save codec.
#pragma once
#include "cedar/graphics_settings.hpp"
#include "cedar/save_store.hpp"

namespace cedar {
enum class GraphicsPreferencesError : std::uint8_t { none, storage, record };
struct GraphicsPreferencesResult {
    GraphicsPreferencesError error{GraphicsPreferencesError::none};
    SaveStatus storage{};
    GraphicsSettingsStatus record{};
    explicit operator bool() const noexcept { return error == GraphicsPreferencesError::none; }
};
// Fixed Cedar-specific name in an explicitly mounted private app directory.
// The caller provisions/mounts that directory; no game-save root is selected.
inline constexpr char graphics_preferences_slot[] = "cedar-graphics-v1.cgfx";
// Failure always leaves preserve-engine defaults. Missing file is a storage
// result (including native errno), never silently rewritten or called loaded.
GraphicsPreferencesResult load_graphics_preferences(const SaveStore& store,
    GraphicsSettings& output) noexcept;
// Validates before I/O. Default refuses an existing file; callers must explicitly
// request replacement. Committed/durability-uncertain results pass through intact.
// Saving preferences does not apply effects or persist capability assertions.
GraphicsPreferencesResult store_graphics_preferences(SaveStore& store,
    const GraphicsSettings& settings, bool allow_replace = false) noexcept;
} // namespace cedar
