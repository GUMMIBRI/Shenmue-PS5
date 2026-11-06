// SPDX-License-Identifier: GPL-3.0-only
// Composition of authored-reference services; see docs/GAME-CONTEXT.md.
#pragma once
#include "cedar/game_state.hpp"
#include "cedar/world_clock.hpp"
#include "cedar/dialogue_machine.hpp"
#include "cedar/game_events.hpp"
namespace cedar {
enum class RuntimeClockField : std::uint8_t { year,month,day,day_number,hour,minute,second,second_of_day,nanosecond,phase,paused };
struct RuntimeClockBinding {std::uint16_t runtime_mode{};RuntimeClockField field{};};
struct GameHostResponse {
    GameServiceResult result{GameServiceResult::unavailable};
    bool has_offset{};std::size_t offset{};
};
using GameHostService=GameHostResponse (*)(void*,const DialogueMachinePending&) noexcept;
struct GameContextServices {
    GameEventFacing* facing{};
    GameHostService host{};void* host_context{};
    DialogueExpressionProvider fallback{};void* fallback_context{};
};
struct ResolvedGameFacing {std::uint64_t token{};GameFacingRequest request{};};
enum class GameContextStatus : std::uint8_t { ok,unavailable,host_failed,invalid_argument,
    invalid_state,reentrant,provider_conflict,acknowledgement_failed,faulted,dialogue_blocked,dialogue_failure };
struct GameContextStep {
    GameContextStatus status{GameContextStatus::ok};
    DialogueMachineStatus machine{DialogueMachineStatus::invalid_state};
    GameStateStatus state{GameStateStatus::ok};
    GameEventResult facing{GameEventResult::unavailable};
    std::uint64_t token{};
    bool effect_applied{},acknowledged{};
};
class NativeGameContext final {
public:
    NativeGameContext() noexcept=default;
    NativeGameContext(const NativeGameContext&)=delete;
    NativeGameContext& operator=(const NativeGameContext&)=delete;
    // Explicit unbind required before lifetime ends. No destructor call through
    // borrowed component pointers whose lifetime may already have ended.
    GameContextStatus bind(WorldClock&,GameState&,DialogueMachine&,
        const RuntimeClockBinding*,std::size_t,const GameContextServices&) noexcept;
    GameContextStatus unbind() noexcept;
    GameContextStep step_dialogue(std::size_t budget,const ResolvedGameFacing* =nullptr) noexcept;
    GameContextStatus advance_world(std::uint64_t elapsed_ns,WorldClockError&) noexcept;
    bool faulted() const noexcept {return faulted_;}
    bool bound() const noexcept {return machine_!=nullptr;}
private:
    static DialogueExpressionValue provider(void*,const DialogueOperandRequest&) noexcept;
    bool owns_provider() const noexcept;
    bool storage_safe() const noexcept;
    void detach() noexcept;
    WorldClock* clock_{};GameState* state_{};DialogueMachine* machine_{};
    GameContextServices services_{};
    RuntimeClockBinding bindings_[32]{};std::size_t count_{};
    bool busy_{},provider_busy_{},faulted_{};
};
}
