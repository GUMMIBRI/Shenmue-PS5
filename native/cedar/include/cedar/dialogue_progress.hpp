// SPDX-License-Identifier: GPL-3.0-only
// Adapted from brynnb/new-yokosuka NativeDialogueProgressState.js; see docs/DIALOGUE-PROGRESS.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class DialogueProgressStatus { ok, uninitialized, invalid_index, invalid_offset, invalid_buffer, invalid_size, invalid_route };
enum class DialogueProgressField { selected, continuation, boundary, latest };
struct DialogueProgressRecord {
    std::uint16_t selected{}, continuation{}, boundary{}, latest{};
    std::uint32_t auxiliary_bits{};
};
struct DialogueProgressRedirect { std::uint16_t offset{}; bool yield_state5{}; };
struct DialogueProgressRuntime {
    std::uint32_t current_offset{};
    std::uint16_t flags1e{};
    bool has_continuation5c{};
    std::uint32_t continuation5c_offset{};
};
struct DialogueProgressCompletion {
    std::uint16_t current_offset{}, flags1e{}, continuation5c_offset{};
    bool has_continuation5c{}, used_progress_continuation{};
    std::int32_t displacement{};
};
// Compatibility behavior of the authored reference, not a verified SEGA save/VM.
// Caller provides all routing bytes and identifiers. No actor/story mapping exists.
class DialogueProgress {
public:
    static constexpr std::size_t record_count = 325, record_size = 12;
    static constexpr std::size_t snapshot_size = record_count * record_size;
    void initialize_reference_defaults() noexcept;
    DialogueProgressStatus load_snapshot(const void*, std::size_t) noexcept;
    DialogueProgressStatus store_snapshot(void*, std::size_t) const noexcept;
    DialogueProgressStatus record(std::size_t, DialogueProgressRecord&) const noexcept;
    DialogueProgressStatus write_offset(std::size_t, DialogueProgressField, std::uint32_t) noexcept;
    DialogueProgressStatus begin_selection(std::size_t, std::uint32_t, bool& changed) noexcept;
    DialogueProgressStatus resolve_redirect(std::size_t, std::uint32_t following,
        bool has_saved_continuation, DialogueProgressRedirect&) noexcept;
    DialogueProgressStatus complete_state5(std::size_t, const void* route, std::size_t route_size,
        const DialogueProgressRuntime&, DialogueProgressCompletion&) const noexcept;
private:
    std::array<std::uint8_t, snapshot_size> bytes_{};
    bool initialized_{};
};
}
