// SPDX-License-Identifier: GPL-3.0-only
// Source-informed byte/LSB-bitfield behavior; docs/GAME-STATE-API.md.
#pragma once
#include "cedar/dialogue_expression.hpp"
#include "cedar/dialogue_body.hpp"

namespace cedar {
enum class GameStateStorage : std::uint8_t { unspecified, bytes, lsb_bitfield };
struct GameStateBank {
    std::uint8_t id{};
    GameStateStorage kind{GameStateStorage::unspecified};
    std::size_t capacity{};
    std::uint8_t* bytes{};
    std::size_t byte_size{};
    // One LSB-first knownness bit per logical entry, including byte banks.
    std::uint8_t* known{};
    std::size_t known_size{};
    bool writable{};
};
enum class GameStateStatus : std::uint8_t {
    ok, invalid_bank, duplicate_bank, invalid_descriptor, invalid_storage,
    address_overflow, overlapping_storage, missing_bank, index_out_of_range,
    unknown, read_only, value_out_of_range, invalid_request
};
struct GameStateRead {
    GameStateStatus status{GameStateStatus::missing_bank};
    std::uint8_t value{}; // Meaningful only when status==ok.
    explicit operator bool() const noexcept { return status == GameStateStatus::ok; }
};
class GameState {
public:
    GameState() noexcept = default;
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) = delete;
    GameState& operator=(GameState&&) = delete;
    // IDs2/3/4 only, no duplicates, up to three explicit borrowed descriptors.
    // Capacity1..4096 is this adapter's unsigned12-index limit, not native size.
    // Full declared data/knownness spans must be pairwise disjoint and disjoint
    // from this object and descriptor array. Failure preserves prior binding.
    GameStateStatus bind(const GameStateBank* banks, std::size_t count) noexcept;
    void unbind() noexcept;
    std::size_t bank_count() const noexcept { return count_; }
    // Composition guard: mutable borrowed storage must not overwrite another
    // live engine component. Invalid/wrapping spans are never disjoint.
    bool storage_disjoint(const void* object, std::size_t bytes) const noexcept;
    GameStateRead read(std::uint8_t bank, std::size_t index) const noexcept;
    GameStateStatus write(std::uint8_t bank, std::size_t index, unsigned value) noexcept;
    // Applies only a decoded actual pending bank2 set/clear request. Does NOT
    // acknowledge a machine token; host does that only after status==ok.
    GameStateStatus apply_bank2_write(const DialogueBodyInstruction&) noexcept;
    // Optional adapter for an enclosing host provider; state-bank requests only.
    static DialogueExpressionValue expression_provider(void*, const DialogueOperandRequest&) noexcept;
private:
    const GameStateBank* find(std::uint8_t bank) const noexcept;
    GameStateBank banks_[3]{};
    std::size_t count_{};
};
} // namespace cedar
