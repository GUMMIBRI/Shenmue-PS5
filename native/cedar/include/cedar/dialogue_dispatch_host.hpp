// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "cedar/dialogue_dispatch.hpp"
#include "cedar/game_context.hpp"
namespace cedar {
using DialogueSelectionService=GameServiceResult (*)(void*,const DialogueMachinePending&,
    const DialogueCommandDispatch&) noexcept;
struct DialogueDispatchHostServices {
    GameEventFacing* facing{};
    DialogueSelectionService select{};void* selection_context{};
    GameHostService fallback{};void* fallback_context{};
};
struct DialogueDispatchHostDiagnostic {
    std::uint64_t token{};
    DialogueDispatchError decoder{DialogueDispatchError::none};
    GameEventResult facing{GameEventResult::unavailable};
    GameServiceResult selection{GameServiceResult::unavailable};
    bool decoder_called{},facing_called{},selection_called{},fallback_called{},reentrant{};
};
class DialogueDispatchHost final {
public:
    DialogueDispatchHost() noexcept=default;
    DialogueDispatchHost(const DialogueDispatchHost&)=delete;
    DialogueDispatchHost& operator=(const DialogueDispatchHost&)=delete;
    // Copies explicit world targets/codes. Does not create/activate an actor.
    bool bind(const DialogueDispatchInputs&,const DialogueDispatchHostServices&) noexcept;
    bool unbind() noexcept;
    GameHostResponse handle(const DialogueMachinePending&) noexcept;
    static GameHostResponse service(void*,const DialogueMachinePending&) noexcept;
    GameContextServices context_services() noexcept;
    DialogueDispatchHostDiagnostic diagnostic() const noexcept {return diagnostic_;}
private:
    DialogueFacingVector targets_[10]{};DialogueParticipantCode participants_[11]{};
    DialogueDispatchInputs input_{};DialogueDispatchHostServices services_{};
    DialogueDispatchHostDiagnostic diagnostic_{};
    bool bound_{},busy_{};
};
}
