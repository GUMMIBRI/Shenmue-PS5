// SPDX-License-Identifier: GPL-3.0-only
// Adapted authored scene-staging behavior; see docs/GAME-EVENTS.md.
#pragma once
#include <cstdint>
namespace cedar {
struct GameActorKey { std::uint64_t instance{}; char code[4]{}; };
enum class GameFacingKind : std::uint8_t { target, participant_offset };
enum class GameServiceResult : std::uint8_t { applied, unavailable, failed };
enum class GameEventResult : std::uint8_t { applied, unavailable, failed,
    invalid_argument, no_active_actor, actor_mismatch, unsupported_command, reentrant };
struct GameFacingRequest {
    GameFacingKind kind{GameFacingKind::target};
    float world_vector[3]{};
    bool activate{};
    bool has_actor_code{};
    char actor_code[4]{};
    char participant_code[4]{}; // Required match for participant_offset.
    std::uint16_t command_word{}; // Opaque provenance, not a decoded command ID.
    std::uint32_t source_index{};
    std::uint64_t handler_address{}, request_address{}; // Never dereferenced.
};
struct GameFacingServices {
    void* context{};
    // Real actor registry/service decides whether the handle exists and applies
    // its world-space target or offset. No callback means unavailable.
    GameServiceResult (*apply)(void*,const GameActorKey&,const GameFacingRequest&) noexcept{};
    GameServiceResult (*clear)(void*,const GameActorKey&) noexcept{};
};
class GameEventFacing final {
public:
    explicit GameEventFacing(GameFacingServices services) noexcept:services_(services){}
    GameEventFacing(const GameEventFacing&)=delete;
    GameEventFacing& operator=(const GameEventFacing&)=delete;
    GameEventResult begin(const GameActorKey&) noexcept;
    GameEventResult apply(const GameFacingRequest&) noexcept;
    GameEventResult end() noexcept;
    GameEventResult dispatch_numeric(std::uint16_t) const noexcept {return GameEventResult::unsupported_command;}
    bool active() const noexcept {return active_;}
private:
    GameFacingServices services_{};
    GameActorKey actor_{};
    bool active_{},busy_{};
};
}
