// SPDX-License-Identifier: GPL-3.0-only
// Adapted from brynnb/new-yokosuka NativeDialoguePresentation.js at
// b0df7f2720a7eecabce2e610fdedec35bf1243e3. Complete GPL retained in
// third_party/licenses/new-yokosuka-GPL3.txt. No generated actor/story tables.
#include "cedar/dialogue_dispatch.hpp"
#include <cmath>
#include <limits>
namespace cedar {
namespace {
using E=DialogueDispatchError;
struct Span {std::uintptr_t begin,end;};
bool span(const void*p,std::size_t count,std::size_t size,std::size_t alignment,Span& s) {
    const auto max=std::numeric_limits<std::uintptr_t>::max();s.begin=reinterpret_cast<std::uintptr_t>(p);
    if((count && (!p || s.begin%alignment)) || count>max/size || count*size>max-s.begin)return false;
    s.end=s.begin+count*size;return true;
}
bool overlaps(Span a,Span b) {return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end;}
}
E decode_dialogue_receiver(std::uint32_t word,DialogueCommandReceiver& out) noexcept {
    DialogueCommandReceiver r;
    if(word==0xfc00)r.path=DialogueReceiverPath::inline_fc;
    else if(word==0x10)r.path=DialogueReceiverPath::inline10;
    else {
        if(word>0xfff)return E::invalid_command;
        r.path=DialogueReceiverPath::queued;r.state_code=word==0x14?0x6c:0x6d;
        if(word>=0x28 && word<=0x31)r.handlers|=handler_161140;
        if(word>=0x32 && word<=0x3b) {r.handlers|=handler_16175c;r.has_participant_index=true;r.participant_index=static_cast<std::uint8_t>(word-0x31);}
        if(word>=0x8c && word<=0x95) {r.handlers|=table_2243a0;r.has_participant_index=true;r.participant_index=static_cast<std::uint8_t>(word-0x8b);}
        if((word>=0x14 && word<=0x1d)||(word>=0x78 && word<=0x81)) {
            r.handlers|=handler_16188c;r.has_facing_index=true;r.facing_index=static_cast<std::uint8_t>(word-(word<0x78?0x14:0x78));
        }
        if(word==0x0d)r.handlers|=handler_161e54;
        if(word==0x0a || word==0x0f)r.handlers|=handler_161f22;
    }
    out=r;return E::none;
}
E resolve_dialogue_dispatch(std::uint32_t word,const DialogueDispatchInputs& input,DialogueCommandDispatch& output) noexcept {
    Span spans[4];
    if(input.facing_count>10 || input.participant_count>11 ||
       !span(input.facing_targets,input.facing_count,sizeof(DialogueFacingVector),alignof(DialogueFacingVector),spans[0]) ||
       !span(input.participants,input.participant_count,sizeof(DialogueParticipantCode),alignof(DialogueParticipantCode),spans[1]) ||
       !span(&input,1,sizeof(input),alignof(DialogueDispatchInputs),spans[2]) ||
       !span(&output,1,sizeof(output),alignof(DialogueCommandDispatch),spans[3]))return E::invalid_buffer;
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)if(overlaps(spans[i],spans[j]))return E::invalid_buffer;
    if(input.converter_context) {
        Span context;
        if(!span(input.converter_context,1,1,1,context))return E::invalid_buffer;
        for(auto s:spans)if(overlaps(context,s))return E::invalid_buffer;
    }
    DialogueCommandDispatch r;const auto decoded=decode_dialogue_receiver(word,r.receiver);if(decoded!=E::none)return decoded;
    r.command_word=static_cast<std::uint16_t>(word);
    if(r.receiver.has_facing_index) {
        r.kind=DialogueDispatchKind::facing_target;r.index=r.receiver.facing_index;r.activate=word<0x78;
        if(r.index>=input.facing_count)return E::missing_facing_target;
        for(unsigned j=0;j<3;++j) {r.source_vector[j]=input.facing_targets[r.index].xyz[j];if(!std::isfinite(r.source_vector[j]))return E::nonfinite_target;}
        r.has_facing_request=true;r.facing_request.kind=GameFacingKind::target;r.facing_request.activate=r.activate;
        for(unsigned j=0;j<3;++j)r.facing_request.world_vector[j]=r.source_vector[j];
    } else if(r.receiver.has_participant_index) {
        r.index=r.receiver.participant_index;if(r.index>=input.participant_count)return E::missing_participant;
        r.participant=input.participants[r.index];for(char c:r.participant.bytes)if(!c)return E::missing_participant;
        if(word<0x8c) {
            r.kind=DialogueDispatchKind::participant_control;r.activate=true;r.source_vector[0]=-0.001f;r.source_axis[1]=1;
            float world[3]{};
            if(input.native_vector_basis==DialogueVectorBasis::confirmed_native_world_identity) {
                for(unsigned j=0;j<3;++j)world[j]=r.source_vector[j];
            } else if(input.native_vector_basis==DialogueVectorBasis::converter && input.converter) {
                if(!input.converter(input.converter_context,r.source_vector,world))return E::converter_failed;
            } else return E::converter_unavailable;
            for(float value:world)if(!std::isfinite(value))return E::nonfinite_target;
            r.has_facing_request=true;r.facing_request.kind=GameFacingKind::participant_offset;r.facing_request.activate=true;
            for(unsigned j=0;j<3;++j)r.facing_request.world_vector[j]=world[j];
            for(unsigned j=0;j<4;++j)r.facing_request.participant_code[j]=r.participant.bytes[j];
        }
        else r.kind=DialogueDispatchKind::selected_participant;
    } else return E::unsupported_action;
    if(r.has_facing_request) {r.facing_request.command_word=r.command_word;r.facing_request.source_index=r.index;}
    output=r;return E::none;
}
}
