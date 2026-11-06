// SPDX-License-Identifier: GPL-3.0-only
// Source facts: new-yokosuka nativePredicateValues.generated.js, pin b0df7f...
// Exact notice/hash and deliberately narrow scope: docs/REFERENCE-GAME-STATE.md.
#include "cedar/reference_game_state.hpp"
#include <limits>

namespace cedar {
namespace {
struct Span { std::uintptr_t begin{}, end{}; };
bool span(const void* pointer, std::size_t size, Span& result) noexcept {
    result.begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (size > std::numeric_limits<std::uintptr_t>::max() - result.begin) return false;
    result.end = result.begin + size;
    return true;
}
bool overlap(Span a, Span b) noexcept { return a.begin < b.end && b.begin < a.end; }
template<std::size_t N>
void copy_bytes(std::uint8_t (&destination)[N], const std::uint8_t* source) noexcept {
    for (std::size_t i = 0; i < N; ++i) destination[i] = source[i];
}
template<std::size_t N>
void known(std::uint8_t (&destination)[N], std::uint8_t value) noexcept {
    for (auto& byte : destination) byte = value;
}
}
GameStateStatus bind_reference_predicate_state(GameState& state,
    ReferencePredicateStorage& storage, bool writable) noexcept {
    const GameStateBank banks[] = {
        {2,GameStateStorage::lsb_bitfield,1024,storage.bank2_bytes,sizeof(storage.bank2_bytes),
            storage.bank2_known,sizeof(storage.bank2_known),writable},
        {3,GameStateStorage::lsb_bitfield,64,storage.bank3_bytes,sizeof(storage.bank3_bytes),
            storage.bank3_known,sizeof(storage.bank3_known),writable},
        {4,GameStateStorage::bytes,32,storage.bank4_bytes,sizeof(storage.bank4_bytes),
            storage.bank4_known,sizeof(storage.bank4_known),writable}
    };
    return state.bind(banks,3);
}
ReferencePredicateStatus load_bank_payloads(ReferencePredicateStorage& storage,
    ReferenceBankPayload bank2, ReferenceBankPayload bank3, ReferenceBankPayload bank4) noexcept {
    using S = ReferencePredicateStatus;
    const ReferenceBankPayload inputs[] = {bank2,bank3,bank4};
    const std::size_t expected[] = {sizeof(storage.bank2_bytes),sizeof(storage.bank3_bytes),sizeof(storage.bank4_bytes)};
    Span destination{};
    if (!span(&storage,sizeof(storage),destination)) return S::address_overflow;
    for (std::size_t i = 0; i < 3; ++i) {
        if (inputs[i].size != expected[i]) return S::invalid_size;
        if (!inputs[i].bytes) return S::invalid_buffer;
        Span source{};
        if (!span(inputs[i].bytes,inputs[i].size,source)) return S::address_overflow;
        if (overlap(source,destination)) return S::overlapping_storage;
    }
    copy_bytes(storage.bank2_bytes,bank2.bytes);
    copy_bytes(storage.bank3_bytes,bank3.bytes);
    copy_bytes(storage.bank4_bytes,bank4.bytes);
    // Publish knownness only after all actual payload copies are complete.
    known(storage.bank2_known,0xff);
    known(storage.bank3_known,0xff);
    known(storage.bank4_known,0xff);
    return S::ok;
}
void forget_reference_predicate_state(ReferencePredicateStorage& storage) noexcept {
    known(storage.bank2_known,0);
    known(storage.bank3_known,0);
    known(storage.bank4_known,0);
}
} // namespace cedar
