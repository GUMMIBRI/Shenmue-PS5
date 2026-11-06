#include "cedar/graphics_preferences.hpp"

namespace cedar {
GraphicsPreferencesResult load_graphics_preferences(const SaveStore& store,
    GraphicsSettings& output) noexcept {
    output = GraphicsSettings{};
    GraphicsPreferencesResult result{};
    std::uint8_t bytes[graphics_settings_wire_bytes]{};
    std::size_t length{};
    result.storage = store.load(graphics_preferences_slot, bytes, sizeof(bytes), length);
    if (!result.storage) {
        result.error = GraphicsPreferencesError::storage;
        return result;
    }
    // Decode requires exact length and checks magic, version, CRC and values.
    result.record = decode_graphics_settings(bytes, length, output);
    if (!result.record) result.error = GraphicsPreferencesError::record;
    return result;
}

GraphicsPreferencesResult store_graphics_preferences(SaveStore& store,
    const GraphicsSettings& settings, bool allow_replace) noexcept {
    GraphicsPreferencesResult result{};
    std::uint8_t bytes[graphics_settings_wire_bytes]{};
    result.record = encode_graphics_settings(settings, bytes, sizeof(bytes));
    if (!result.record) {
        result.error = GraphicsPreferencesError::record;
        return result;
    }
    result.storage = store.store(graphics_preferences_slot, bytes, sizeof(bytes), allow_replace);
    if (!result.storage) result.error = GraphicsPreferencesError::storage;
    return result;
}
} // namespace cedar
