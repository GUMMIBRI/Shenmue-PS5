// SPDX-License-Identifier: GPL-3.0-only
// Adapted from brynnb/new-yokosuka NativeDialogueBody.js at
// b0df7f2720a7eecabce2e610fdedec35bf1243e3. GPL text retained in
// third_party/licenses/new-yokosuka-GPL3.txt. No generated/story data.
#include "cedar/dialogue_machine.hpp"
#include <limits>
namespace cedar {
namespace {
using S=DialogueMachineStatus; using C=DialogueBodyCommand;
bool disjoint(const void* a,std::size_t n,const void* b,std::size_t m) {
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    const auto max=std::numeric_limits<std::uintptr_t>::max();
    return a && b && n<=max-x && m<=max-y && (x+n<=y || y+m<=x);
}
bool group_boundary(std::uint8_t op) {
    const auto f=op&0xf0;
    if(f==0x80 || f==0xa0 || f==0xb0 || (f==0xe0 && op!=0xe0)) return true;
    // These are the source's outer-control boundaries, not group noops.
    return op==0xf2 || op==0xf3 || op==0xf4 || op==0xf5 || op==0xf9 ||
        op==0xfa || op==0xfb || op==0xfe || op==0xff;
}
}
void DialogueMachine::reset() noexcept {
    if(busy_) return;
    bytes_=nullptr;size_=0;identifier_=0;state_={};request_={};pending_=false;done_=false;
    decode_error_=DialogueBodyError::none;
    provider_=nullptr;provider_context_=nullptr;expression_parse_={};expression_eval_={};
    // Keep token serial across restarts so an old acknowledgement cannot match.
}
S DialogueMachine::start(const std::uint8_t* bytes,std::size_t size,
    std::uint64_t id,const DialogueMachineState& initial) noexcept {
    if(busy_ || bytes_ || pending_) return S::invalid_state;
    if(!size || size>16u*1024u*1024u || !disjoint(bytes,size,this,sizeof(*this)) ||
       !disjoint(&initial,sizeof(initial),this,sizeof(*this)) || initial.cursor>=size ||
       (provider_context_ && !disjoint(provider_context_,1,bytes,size)) ||
       (initial.has_continuation60 && initial.continuation60>=size) ||
       (initial.has_continuation64 && initial.continuation64>=size) ||
       (initial.mode!=DialogueBodyMode::outer_body && initial.mode!=DialogueBodyMode::message_group) ||
       (initial.message_flags&~6u)) return S::invalid_argument;
    state_=initial;bytes_=bytes;size_=size;identifier_=id;return S::ready;
}
S DialogueMachine::supply_predicate(bool zero) noexcept {
    if(busy_ || !bytes_ || done_ || pending_) return S::invalid_state;
    state_.predicate_valid=true;state_.predicate_zero=zero;return S::ready;
}
S DialogueMachine::bind_expression_provider(DialogueExpressionProvider provider,void* context) noexcept {
    if(busy_ || pending_) return S::invalid_state;
    if(context && (!disjoint(context,1,this,sizeof(*this)) ||
       (bytes_ && !disjoint(context,1,bytes_,size_)))) return S::invalid_argument;
    provider_=provider;provider_context_=context;return S::ready;
}
S DialogueMachine::advance(std::size_t budget) noexcept {
    if(busy_ || !bytes_) return S::invalid_state;
    if(done_) return S::complete;
    if(pending_) return S::pending_host;
    if(!budget || budget>4096) return S::invalid_argument;
    for(std::size_t step=0;step<budget;++step) {
        if(state_.cursor>=size_) {decode_error_=DialogueBodyError::offset_out_of_range;return S::decode_failure;}
        if(state_.mode==DialogueBodyMode::message_group && group_boundary(bytes_[state_.cursor])) {
            state_.mode=DialogueBodyMode::outer_body;state_.message_flags=0;continue;
        }
        if(bytes_[state_.cursor]==0xf0) {
            DialogueExpressionView view;
            expression_parse_=parse_dialogue_expression(bytes_,size_,state_.cursor+1,
                expression_nodes_,expression_capacity,expression_stack_,expression_capacity,view);
            expression_eval_={};
            if(!expression_parse_) return S::expression_failure;
            busy_=true;
            expression_eval_=evaluate_dialogue_expression(view,provider_,provider_context_,
                expression_values_,expression_capacity);
            busy_=false;
            if(!expression_eval_) return S::expression_failure;
            if(!expression_eval_.resolved) return S::predicate_required;
            state_.predicate_valid=true;state_.predicate_zero=expression_eval_.value==0;
            state_.cursor=view.end_offset();continue;
        }
        DialogueBodyInstruction i;
        const auto decoded=decode_dialogue_body(bytes_,size_,state_.cursor,state_.mode,i);
        if(!decoded) {decode_error_=decoded.error;return S::decode_failure;}
        const auto c=i.command;
        if(c==C::branch_if_zero && !state_.predicate_valid) return S::predicate_required;
        if(c==C::noop) {state_.cursor=i.following_offset;continue;}
        if(c==C::branch || c==C::branch_if_zero) {
            state_.cursor=(c==C::branch || state_.predicate_zero)?i.target_offset:i.following_offset;continue;
        }
        if(c==C::message_group_entry) {state_.mode=DialogueBodyMode::message_group;state_.message_flags=0;continue;}
        if(c==C::call_with_continuation) {
            if(i.following_offset>=size_) return S::invalid_argument;
            state_.continuation60=i.following_offset;state_.has_continuation60=true;state_.cursor=i.target_offset;continue;
        }
        if(c==C::save_continuation) {state_.continuation64=i.target_offset;state_.has_continuation64=true;state_.cursor=i.following_offset;continue;}
        if(c==C::continuation_request && state_.has_continuation60) {
            state_.cursor=state_.continuation60;state_.has_continuation60=false;continue;
        }
        if(c==C::message_flags) {state_.message_flags|=i.message_flag_bits;state_.cursor=i.following_offset;continue;}
        if(c==C::invoke_and_yield && i.operand==0x10) {state_.cursor=i.following_offset;continue;}
        if(serial_==std::numeric_limits<std::uint64_t>::max()) return S::invalid_state;
        request_={++serial_,identifier_,i,state_.message_flags,c==C::progress_redirect || c==C::continuation_request};
        pending_=true;return S::pending_host;
    }
    return S::budget_exhausted;
}
S DialogueMachine::acknowledge(const DialogueMachineAck& ack) noexcept {
    if(busy_ || !pending_ || !disjoint(&ack,sizeof(ack),this,sizeof(*this))) return S::invalid_state;
    if(ack.token!=request_.token) return S::invalid_argument;
    if(!ack.succeeded) return S::host_failed;
    if(ack.has_offset!=request_.requires_offset || (ack.has_offset && ack.offset>=size_)) return S::invalid_argument;
    auto next=state_;
    const auto& i=request_.instruction;
    next.cursor=ack.has_offset?ack.offset:i.following_offset;
    if(i.command==C::progress_marker && state_.mode==DialogueBodyMode::outer_body) next.has_continuation64=false;
    if(i.command==C::message) next.message_flags=0;
    if(i.command==C::native_command && i.native_tick_advance==1 &&
       ((i.operand>=0x14 && i.operand<=0x1d)||(i.operand>=0x78 && i.operand<=0x81))) next.message_flags|=2;
    if(i.command==C::complete) {next.cursor=i.offset;done_=true;}
    state_=next;pending_=false;request_={};return done_?S::complete:S::ready;
}
}
