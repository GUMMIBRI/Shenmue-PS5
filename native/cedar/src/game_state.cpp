// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by authored new-yokosuka NativeDialogueState.js at
// b0df7f2720a7eecabce2e610fdedec35bf1243e3. No generated bank layouts.
#include "cedar/game_state.hpp"
#include <limits>

namespace cedar {
namespace {
using S = GameStateStatus;
struct Span { std::uintptr_t begin{}, end{}; };
bool span(const void* p, std::size_t size, Span& out) noexcept {
    out.begin = reinterpret_cast<std::uintptr_t>(p);
    if (size > std::numeric_limits<std::uintptr_t>::max() - out.begin) return false;
    out.end = out.begin + size;
    return true;
}
bool overlaps(Span a, Span b) noexcept {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
bool valid_id(std::uint8_t bank) noexcept { return bank >= 2 && bank <= 4; }
}

GameStateStatus GameState::bind(const GameStateBank* banks, std::size_t count) noexcept {
    if (count > 3 || (count && (!banks ||
        reinterpret_cast<std::uintptr_t>(banks) % alignof(GameStateBank)))) return S::invalid_descriptor;
    Span descriptors{}, object{}, storage[6]{};
    if (!span(banks,count*sizeof(GameStateBank),descriptors) || !span(this,sizeof(*this),object))
        return S::address_overflow;
    if (overlaps(descriptors,object)) return S::overlapping_storage;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& b = banks[i];
        if (!valid_id(b.id)) return S::invalid_bank;
        for (std::size_t j = 0; j < i; ++j) if (banks[j].id == b.id) return S::duplicate_bank;
        if (b.kind != GameStateStorage::bytes && b.kind != GameStateStorage::lsb_bitfield)
            return S::invalid_storage;
        if (!b.capacity || b.capacity > 4096 || !b.bytes || !b.known) return S::invalid_descriptor;
        const auto bits_size = (b.capacity + 7)/8; // Capacity cap makes addition safe.
        const auto data_size = b.kind == GameStateStorage::bytes ? b.capacity : bits_size;
        if (b.byte_size < data_size || b.known_size < bits_size) return S::invalid_descriptor;
        if (!span(b.bytes,b.byte_size,storage[2*i]) || !span(b.known,b.known_size,storage[2*i+1]))
            return S::address_overflow;
        for (std::size_t k = 2*i; k < 2*i+2; ++k) {
            if (overlaps(storage[k],object) || overlaps(storage[k],descriptors)) return S::overlapping_storage;
            for (std::size_t j = 0; j < k; ++j)
                if (overlaps(storage[k],storage[j])) return S::overlapping_storage;
        }
    }
    // No operation after preflight can fail. Copy descriptors, never bank bytes.
    for (std::size_t i = 0; i < count; ++i) banks_[i] = banks[i];
    for (std::size_t i = count; i < 3; ++i) banks_[i] = {};
    count_ = count;
    return S::ok;
}
void GameState::unbind() noexcept {
    for (auto& bank : banks_) bank = {};
    count_ = 0;
}
bool GameState::storage_disjoint(const void* pointer, std::size_t size) const noexcept {
    if (!pointer && size) return false;
    Span object{};
    if (!span(pointer,size,object)) return false;
    for (std::size_t i=0;i<count_;++i) {
        Span data{}, known{};
        if (!span(banks_[i].bytes,banks_[i].byte_size,data) ||
            !span(banks_[i].known,banks_[i].known_size,known) ||
            overlaps(object,data) || overlaps(object,known)) return false;
    }
    return true;
}
const GameStateBank* GameState::find(std::uint8_t bank) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) if (banks_[i].id == bank) return banks_ + i;
    return nullptr;
}
GameStateRead GameState::read(std::uint8_t bank, std::size_t index) const noexcept {
    if (!valid_id(bank)) return {S::invalid_bank,0};
    const auto* b = find(bank);
    if (!b) return {S::missing_bank,0};
    if (index >= b->capacity) return {S::index_out_of_range,0};
    const auto mask = static_cast<std::uint8_t>(1u << (index & 7));
    if (!(b->known[index >> 3] & mask)) return {S::unknown,0};
    const auto value = b->kind == GameStateStorage::bytes ? b->bytes[index] :
        static_cast<std::uint8_t>((b->bytes[index >> 3] >> (index & 7)) & 1u);
    return {S::ok,value};
}
GameStateStatus GameState::write(std::uint8_t bank, std::size_t index, unsigned value) noexcept {
    if (!valid_id(bank)) return S::invalid_bank;
    const auto* b = find(bank);
    if (!b) return S::missing_bank;
    if (index >= b->capacity) return S::index_out_of_range;
    if (!b->writable) return S::read_only;
    const bool bits = b->kind == GameStateStorage::lsb_bitfield;
    if (value > (bits ? 1u : 255u)) return S::value_out_of_range;
    const auto mask = static_cast<std::uint8_t>(1u << (index & 7));
    if (bits) {
        auto& byte = b->bytes[index >> 3];
        byte = value ? static_cast<std::uint8_t>(byte | mask) :
            static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
    } else b->bytes[index] = static_cast<std::uint8_t>(value);
    b->known[index >> 3] = static_cast<std::uint8_t>(b->known[index >> 3] | mask);
    return S::ok;
}
GameStateStatus GameState::apply_bank2_write(const DialogueBodyInstruction& instruction) noexcept {
    if (instruction.command != DialogueBodyCommand::state_bank_write ||
        instruction.state_bank != 2 || instruction.state_value > 1 || instruction.operand > 4095)
        return S::invalid_request;
    Span request{};
    if (!span(&instruction,sizeof(instruction),request)) return S::address_overflow;
    for (std::size_t i = 0; i < count_; ++i) {
        Span data{}, known{};
        if (!span(banks_[i].bytes,banks_[i].byte_size,data) ||
            !span(banks_[i].known,banks_[i].known_size,known)) return S::address_overflow;
        if (overlaps(request,data) || overlaps(request,known)) return S::overlapping_storage;
    }
    return write(2,instruction.operand,instruction.state_value);
}
DialogueExpressionValue GameState::expression_provider(void* context, const DialogueOperandRequest& request) noexcept {
    if (!context || reinterpret_cast<std::uintptr_t>(context) % alignof(GameState) ||
        request.kind != DialogueOperandKind::state_bank || !valid_id(request.native_value_type)) return {};
    const auto result = static_cast<const GameState*>(context)->read(request.native_value_type,request.index);
    return result ? DialogueExpressionValue{true,static_cast<double>(result.value)} : DialogueExpressionValue{};
}
} // namespace cedar
