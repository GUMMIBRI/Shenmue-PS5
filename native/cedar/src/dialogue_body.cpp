// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by brynnb/new-yokosuka NativeDialogueBody.js at
// b0df7f2720a7eecabce2e610fdedec35bf1243e3. See docs/DIALOGUE-BODY.md.
#include "cedar/dialogue_body.hpp"
#include <limits>

namespace cedar {
namespace {
std::int32_t signed_operand(std::uint32_t value, unsigned bits) noexcept {
    const auto sign = std::uint32_t{1} << (bits - 1);
    return value & sign ? static_cast<std::int32_t>(value) -
        static_cast<std::int32_t>(std::uint32_t{1} << bits) : static_cast<std::int32_t>(value);
}
bool target(std::size_t following, std::int32_t displacement,
    std::size_t size, std::size_t& result) noexcept {
    if (displacement < 0) {
        const auto distance = static_cast<std::size_t>(-displacement);
        if (distance > following) return false;
        result = following - distance;
    } else {
        const auto distance = static_cast<std::size_t>(displacement);
        if (following > size || distance >= size - following) return false;
        result = following + distance;
    }
    return result < size;
}
} // namespace

DialogueBodyResult decode_dialogue_body(const std::uint8_t* bytes, std::size_t size,
    std::size_t offset, DialogueBodyMode mode, DialogueBodyInstruction& output) noexcept {
    using E = DialogueBodyError;
    using C = DialogueBodyCommand;
    if (mode != DialogueBodyMode::outer_body && mode != DialogueBodyMode::message_group)
        return {E::invalid_mode};
    if (!bytes) return {E::invalid_buffer};
    const auto begin = reinterpret_cast<std::uintptr_t>(bytes);
    const auto out = reinterpret_cast<std::uintptr_t>(&output);
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    if (size > maximum - begin || sizeof(output) > maximum - out) return {E::address_overflow};
    if (begin < out + sizeof(output) && out < begin + size) return {E::overlapping_storage};
    if (offset >= size) return {E::offset_out_of_range};

    DialogueBodyInstruction value{};
    value.offset = offset;
    value.opcode = bytes[offset];
    const auto op = value.opcode;
    const auto family = static_cast<std::uint8_t>(op & 0xf0);
    const bool group = mode == DialogueBodyMode::message_group;
    std::size_t length = 0;
    unsigned branch_bits = 0;
    if (family == 0x40 || family == 0x50 || family == 0xd0) {
        value.command = family == 0x40 ? C::branch_if_zero : C::branch;
        length = family == 0xd0 ? 3 : 2;
        branch_bits = family == 0xd0 ? 20 : 12;
    } else if (family == 0xc0 && (op & 15) >= 1 && (op & 15) <= 7) {
        value.command = C::runtime_write;
        length = 2;
    } else if (!group && op < 0xa0) {
        length = 2;
        switch (family) {
        case 0x00: value.command = C::noop; break;
        case 0x10: value.command = C::progress_redirect; break;
        case 0x20: value.command = C::message_group_entry; break;
        case 0x30: value.command = C::invoke_and_yield; break;
        case 0x60: case 0x70: value.command = C::state_bank_write; break;
        case 0x80: value.command = C::call_with_continuation; branch_bits = 12; break;
        case 0x90: value.command = C::progress_marker; break;
        default: return {E::unsupported_opcode};
        }
    } else if (group && (family == 0x20 || family == 0x30 || family == 0x60 || family == 0x90)) {
        length = 2;
        value.command = family == 0x20 ? C::message : family == 0x90 ? C::progress_marker : C::native_command;
        value.native_tick_advance = family == 0x30 ? 1 : 0;
    } else if (!group && (op == 0xe1 || op == 0xe2)) {
        value.command = C::native_event;
        length = op == 0xe1 ? 11 : 2;
    } else if (!group && op == 0xf2) {
        value.command = C::state5_event; length = 4;
    } else if (!group && op == 0xf5) {
        value.command = C::save_continuation; length = 4; branch_bits = 24;
    } else if (!group && op == 0xf9) {
        value.command = C::continuation_request; length = 1;
    } else if (op == 0xfc) {
        value.command = group ? C::native_command : C::state6_event;
        value.operand = group ? 0xfc00 : 0;
        length = 1;
    } else if (group && (op == 0xf6 || op == 0xf7 || op == 0xf8)) {
        value.command = C::message_flags; value.message_flag_bits = 4; length = 1;
    } else if (!group && (op == 0xf3 || op == 0xf6 || op == 0xf7 || op == 0xf8 || op == 0xff)) {
        value.command = C::complete; length = 1;
    } else if (op == 0xf1 || op == 0xfd ||
        (!group && (op == 0xf4 || op == 0xfa || op == 0xfb || op == 0xfe))) {
        value.command = C::noop; length = 1;
    } else return {E::unsupported_opcode};

    if (length > size - offset) return {E::truncated};
    value.byte_length = length;
    value.following_offset = offset + length;
    if (length == 2 && op < 0xe0)
        value.operand = (std::uint32_t{op & 15u} << 8) | bytes[offset + 1];
    if (branch_bits) {
        if (branch_bits == 20)
            value.operand = (std::uint32_t{op & 15u} << 16) |
                (std::uint32_t{bytes[offset + 1]} << 8) | bytes[offset + 2];
        else if (branch_bits == 24)
            value.operand = (std::uint32_t{bytes[offset + 1]} << 16) |
                (std::uint32_t{bytes[offset + 2]} << 8) | bytes[offset + 3];
        value.displacement = signed_operand(value.operand, branch_bits);
        if (!target(value.following_offset, value.displacement, size, value.target_offset))
            return {E::target_out_of_range};
        value.has_target = true;
    }
    if (value.command == C::state_bank_write) {
        value.state_bank = 2; value.state_value = family == 0x60 ? 1 : 0;
    }
    if (value.command == C::runtime_write) {
        value.selector = op & 15; value.raw_value = bytes[offset + 1];
        value.manager_field = value.selector <= 2;
        value.field_offset = value.manager_field ? static_cast<std::uint8_t>(0x0f + value.selector) :
            value.selector == 7 ? 0x10 : static_cast<std::uint8_t>(0x08 + value.selector);
        value.field_value = value.selector == 7 ?
            static_cast<std::int16_t>(signed_operand(value.raw_value, 8)) : value.raw_value;
    }
    if (op == 0xe1) {
        value.event_byte = bytes[offset + 2];
        for (unsigned i = 0; i < 4; ++i) {
            const auto at = offset + 3 + 2*i;
            value.event_arguments[i] = static_cast<std::int16_t>(signed_operand(
                (std::uint32_t{bytes[at]} << 8) | bytes[at + 1], 16));
        }
    }
    // Source's outer 0x3010 is a dispatch no-op, not an external invocation.
    if (!group && family == 0x30 && value.operand == 0x10) value.command = C::noop;
    output = value;
    return {};
}
} // namespace cedar
