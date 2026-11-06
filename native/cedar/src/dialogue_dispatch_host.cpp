// SPDX-License-Identifier: GPL-3.0-only
// Composition of authored New Yokosuka dispatch adapters, pinned provenance in
// DIALOGUE-DISPATCH.md; complete GPL in new-yokosuka-GPL3.txt.
#include "cedar/dialogue_dispatch_host.hpp"
#include <cmath>
#include <limits>
namespace cedar {
namespace {
struct Span {std::uintptr_t a,b;};
bool span(const void*p,std::size_t n,std::size_t size,std::size_t align,Span& s) {
    s.a=reinterpret_cast<std::uintptr_t>(p);const auto max=std::numeric_limits<std::uintptr_t>::max();
    if((n && (!p || s.a%align)) || n>max/size || n*size>max-s.a)return false;
    s.b=s.a+n*size;return true;
}
bool overlap(Span x,Span y) {return x.a<x.b && y.a<y.b && x.a<y.b && y.a<x.b;}
GameHostResponse response(GameServiceResult r) {GameHostResponse out;out.result=r;return out;}
}
bool DialogueDispatchHost::bind(const DialogueDispatchInputs& input,const DialogueDispatchHostServices& services) noexcept {
    if(busy_ || bound_ || input.facing_count>10 || input.participant_count>11)return false;
    Span ranges[6];
    if(!span(input.facing_targets,input.facing_count,sizeof(DialogueFacingVector),alignof(DialogueFacingVector),ranges[0]) ||
       !span(input.participants,input.participant_count,sizeof(DialogueParticipantCode),alignof(DialogueParticipantCode),ranges[1]) ||
       !span(&input,1,sizeof(input),alignof(DialogueDispatchInputs),ranges[2]) ||
       !span(&services,1,sizeof(services),alignof(DialogueDispatchHostServices),ranges[3]) ||
       !span(this,1,sizeof(*this),alignof(DialogueDispatchHost),ranges[4]) ||
       !span(services.facing,services.facing?1:0,sizeof(GameEventFacing),alignof(GameEventFacing),ranges[5]))return false;
    for(unsigned i=0;i<6;++i)for(unsigned j=i+1;j<6;++j)if(overlap(ranges[i],ranges[j]))return false;
    const void* contexts[3]={input.converter_context,services.selection_context,services.fallback_context};
    for(const void* context:contexts)if(context) {
        Span c;if(!span(context,1,1,1,c))return false;
        for(auto r:ranges)if(overlap(c,r))return false;
    }
    if(input.native_vector_basis!=DialogueVectorBasis::unspecified &&
       input.native_vector_basis!=DialogueVectorBasis::confirmed_native_world_identity &&
       input.native_vector_basis!=DialogueVectorBasis::converter)return false;
    for(std::size_t i=0;i<input.facing_count;++i)for(float f:input.facing_targets[i].xyz)if(!std::isfinite(f))return false;
    // The one-based source table may leave slot zero or other unavailable
    // participants empty. The resolver rejects a missing code only when used.
    for(std::size_t i=0;i<input.facing_count;++i)targets_[i]=input.facing_targets[i];
    for(std::size_t i=0;i<input.participant_count;++i)participants_[i]=input.participants[i];
    input_=input;input_.facing_targets=targets_;input_.participants=participants_;
    services_=services;bound_=true;diagnostic_={};return true;
}
bool DialogueDispatchHost::unbind() noexcept {
    if(busy_)return false;
    bound_=false;services_={};input_={};diagnostic_={};return true;
}
GameHostResponse DialogueDispatchHost::handle(const DialogueMachinePending& pending) noexcept {
    if(busy_) {diagnostic_.reentrant=true;return response(GameServiceResult::unavailable);}
    if(!bound_)return response(GameServiceResult::unavailable);
    Span source,owner;
    if(!span(&pending,1,sizeof(pending),alignof(DialogueMachinePending),source) ||
       !span(this,1,sizeof(*this),alignof(DialogueDispatchHost),owner) || overlap(source,owner))
        return response(GameServiceResult::unavailable);
    const auto request=pending;
    diagnostic_={};diagnostic_.token=request.token;
    if(!request.token)return response(GameServiceResult::unavailable);
    if(request.instruction.command!=DialogueBodyCommand::native_command &&
       request.instruction.command!=DialogueBodyCommand::invoke_and_yield) {
        if(!services_.fallback)return response(GameServiceResult::unavailable);
        busy_=true;diagnostic_.fallback_called=true;
        const auto result=services_.fallback(services_.fallback_context,request);
        busy_=false;return result;
    }
    if(request.requires_offset)return response(GameServiceResult::unavailable);
    busy_=true;
    DialogueCommandDispatch dispatch;
    diagnostic_.decoder_called=true;
    diagnostic_.decoder=resolve_dialogue_dispatch(request.instruction.operand,input_,dispatch);
    GameHostResponse result;
    if(diagnostic_.decoder==DialogueDispatchError::none) {
        if(dispatch.has_facing_request) {
            if(services_.facing) {
                diagnostic_.facing_called=true;diagnostic_.facing=services_.facing->apply(dispatch.facing_request);
                result.result=diagnostic_.facing==GameEventResult::applied?GameServiceResult::applied:
                    diagnostic_.facing==GameEventResult::failed?GameServiceResult::failed:GameServiceResult::unavailable;
            }
        } else if(dispatch.kind==DialogueDispatchKind::selected_participant && services_.select) {
            diagnostic_.selection_called=true;
            diagnostic_.selection=services_.select(services_.selection_context,request,dispatch);
            result=response(diagnostic_.selection);
        }
    } else if(diagnostic_.decoder==DialogueDispatchError::unsupported_action && services_.fallback) {
        diagnostic_.fallback_called=true;result=services_.fallback(services_.fallback_context,request);
    }
    // Missing required data/converter never falls through to a success fallback.
    busy_=false;return result;
}
GameHostResponse DialogueDispatchHost::service(void* context,const DialogueMachinePending& pending) noexcept {
    if(!context)return response(GameServiceResult::unavailable);
    return static_cast<DialogueDispatchHost*>(context)->handle(pending);
}
GameContextServices DialogueDispatchHost::context_services() noexcept {
    GameContextServices out;out.facing=services_.facing;out.host=service;out.host_context=this;return out;
}
}
