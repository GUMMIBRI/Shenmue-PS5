// SPDX-License-Identifier: GPL-3.0-only
// Adapted New Yokosuka authored command decoder; docs/DIALOGUE-DISPATCH.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include "cedar/game_events.hpp"
namespace cedar {
enum class DialogueReceiverPath : std::uint8_t { inline10,inline_fc,queued };
enum DialogueHandlerBits : std::uint16_t { handler_161140=1,handler_16175c=2,
    table_2243a0=4,handler_16188c=8,handler_161e54=16,handler_161f22=32 };
struct DialogueCommandReceiver {
    DialogueReceiverPath path{};
    std::uint16_t handlers{};
    std::uint8_t state_code{};
    bool has_facing_index{},has_participant_index{};
    std::uint8_t facing_index{},participant_index{};
};
enum class DialogueDispatchError : std::uint8_t { none,invalid_command,invalid_buffer,
    missing_facing_target,missing_participant,nonfinite_target,unsupported_action,converter_unavailable,converter_failed };
enum class DialogueDispatchKind : std::uint8_t { facing_target,participant_control,selected_participant };
struct DialogueFacingVector {float xyz[3]{};};
struct DialogueParticipantCode {char bytes[4]{};};
enum class DialogueVectorBasis : std::uint8_t { unspecified,confirmed_native_world_identity,converter };
using DialogueVectorConverter=bool (*)(void*,const float native_vector[3],float world_vector[3]) noexcept;
struct DialogueDispatchInputs {
    // Actual WORLD-space targets, explicitly converted by the caller producer.
    const DialogueFacingVector* facing_targets{};std::size_t facing_count{};
    const DialogueParticipantCode* participants{};std::size_t participant_count{};
    DialogueVectorBasis native_vector_basis{DialogueVectorBasis::unspecified};
    DialogueVectorConverter converter{};void* converter_context{};
};
struct DialogueCommandDispatch {
    std::uint16_t command_word{};
    DialogueCommandReceiver receiver{};
    DialogueDispatchKind kind{};
    std::uint8_t index{};
    bool activate{};
    DialogueParticipantCode participant{};
    // WORLD coordinates for target; SOURCE native vector for control.
    float source_vector[3]{},source_axis[3]{};
    bool has_facing_request{};
    GameFacingRequest facing_request{};
};
DialogueDispatchError decode_dialogue_receiver(std::uint32_t command_word,DialogueCommandReceiver&) noexcept;
// Does not apply/ack commands. Unsupported actions stay unsupported, output
// unchanged on failure. Complete supplied spans must be valid and disjoint.
DialogueDispatchError resolve_dialogue_dispatch(std::uint32_t command_word,
    const DialogueDispatchInputs&,DialogueCommandDispatch&) noexcept;
}
