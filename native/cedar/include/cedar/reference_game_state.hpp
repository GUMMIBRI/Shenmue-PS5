// SPDX-License-Identifier: GPL-3.0-only
// Pinned authored schema facts, not a universal native save profile.
#pragma once
#include "cedar/game_state.hpp"

namespace cedar {
enum class NewYokosukaPredicateLayout : std::uint8_t { predicate_data_v2_b0df7f };
struct ReferencePredicateStorage {
    static constexpr NewYokosukaPredicateLayout layout =
        NewYokosukaPredicateLayout::predicate_data_v2_b0df7f;
    std::uint8_t bank2_bytes[128]{}, bank2_known[128]{};
    std::uint8_t bank3_bytes[8]{}, bank3_known[8]{};
    std::uint8_t bank4_bytes[32]{}, bank4_known[4]{};
    ReferencePredicateStorage() noexcept = default;
    ReferencePredicateStorage(const ReferencePredicateStorage&) = delete;
    ReferencePredicateStorage& operator=(const ReferencePredicateStorage&) = delete;
    ReferencePredicateStorage(ReferencePredicateStorage&&) = delete;
    ReferencePredicateStorage& operator=(ReferencePredicateStorage&&) = delete;
};
struct ReferenceBankPayload { const std::uint8_t* bytes{}; std::size_t size{}; };
enum class ReferencePredicateStatus : std::uint8_t {
    ok, invalid_buffer, invalid_size, address_overflow, overlapping_storage
};
// Copies descriptors only; storage stays immovable/alive while state is bound.
// Default raw zeros remain UNKNOWN and are not an original NewGame assertion.
GameStateStatus bind_reference_predicate_state(GameState& state,
    ReferencePredicateStorage& storage, bool writable) noexcept;
// Exactly128/8/32 payload bytes, not save-file slices inferred by this helper.
// All input spans must be valid, immutable and disjoint from the whole storage.
// Full preflight before any copy; on success all copied entries become known.
ReferencePredicateStatus load_bank_payloads(ReferencePredicateStorage& storage,
    ReferenceBankPayload bank2, ReferenceBankPayload bank3, ReferenceBankPayload bank4) noexcept;
// Explicitly invalidate knownness; leave raw bytes intact. No automatic reset.
void forget_reference_predicate_state(ReferencePredicateStorage& storage) noexcept;
} // namespace cedar
