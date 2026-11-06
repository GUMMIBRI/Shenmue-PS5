// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by brynnb/new-yokosuka; see docs/DIALOGUE-BODY.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class DialogueBodyMode : std::uint8_t { outer_body, message_group };
enum class DialogueBodyError : std::uint8_t {
    none, invalid_mode, invalid_buffer, offset_out_of_range, address_overflow,
    overlapping_storage, truncated, unsupported_opcode, target_out_of_range
};
enum class DialogueBodyCommand : std::uint8_t {
    none, noop, progress_redirect, message_group_entry, invoke_and_yield,
    branch_if_zero, branch, state_bank_write, call_with_continuation,
    progress_marker, runtime_write, native_event, state5_event,
    save_continuation, continuation_request, state6_event, complete,
    message, native_command, message_flags
};
struct DialogueBodyInstruction {
    DialogueBodyCommand command{DialogueBodyCommand::none};
    std::uint8_t opcode{};
    std::size_t offset{}, byte_length{}, following_offset{};
    // Only branch/call/save-continuation commands have an encoded target.
    bool has_target{};
    std::int32_t displacement{};
    std::size_t target_offset{};
    std::uint32_t operand{};
    // Meaningful for state_bank_write only; bank is exactly 2.
    std::uint8_t state_bank{}, state_value{};
    // Meaningful for runtime_write: raw byte and source-established field map.
    std::uint8_t selector{}, raw_value{}, field_offset{};
    bool manager_field{};
    std::int16_t field_value{};
    // Meaningful for E1 native_event only. Other event bytes remain opaque.
    std::uint8_t event_byte{};
    std::int16_t event_arguments[4]{};
    // Meaningful for group native_command/message_flags only.
    std::uint8_t native_tick_advance{}, message_flag_bits{};
};
struct DialogueBodyResult {
    DialogueBodyError error{DialogueBodyError::none};
    explicit operator bool() const noexcept { return error == DialogueBodyError::none; }
};

// Decode exactly one supported command, never execute or advance caller state.
// Input/output are valid caller storage, disjoint and immutable during the call.
// All offsets are relative to this supplied slice; target must name a byte in it.
// On any failure output remains unchanged. No expression/native-event execution,
// generated resources, allocation, IO, message lookup or guessed opcode lengths.
DialogueBodyResult decode_dialogue_body(const std::uint8_t* bytes, std::size_t size,
    std::size_t offset, DialogueBodyMode mode, DialogueBodyInstruction& output) noexcept;
} // namespace cedar
