// SPDX-License-Identifier: GPL-3.0-only
// Composes Cedar authored New Yokosuka adaptations. GPL text retained in
// third_party/licenses/new-yokosuka-GPL3.txt; no generated/story/native-ID data.
#include "cedar/game_context.hpp"
#include <limits>
#include <initializer_list>
namespace cedar {
namespace {
using S=GameContextStatus;
bool disjoint(const void* p,std::size_t n,const void* q,std::size_t m) {
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(q);
    const auto max=std::numeric_limits<std::uintptr_t>::max();
    return (!n || p) && (!m || q) && n<=max-a && m<=max-b && (!n || !m || a+n<=b || b+m<=a);
}
bool field_valid(RuntimeClockField f) {return f>=RuntimeClockField::year && f<=RuntimeClockField::paused;}
}
bool NativeGameContext::owns_provider() const noexcept {
    return machine_ && machine_->expression_provider()==provider && machine_->expression_provider_context()==this;
}
bool NativeGameContext::storage_safe() const noexcept {
    return state_ && state_->storage_disjoint(this,sizeof(*this)) &&
        state_->storage_disjoint(state_,sizeof(*state_)) &&
        state_->storage_disjoint(machine_,sizeof(*machine_)) &&
        state_->storage_disjoint(clock_,sizeof(*clock_)) &&
        (!services_.facing || state_->storage_disjoint(services_.facing,sizeof(GameEventFacing)));
}
void NativeGameContext::detach() noexcept {
    machine_=nullptr;clock_=nullptr;state_=nullptr;services_={};count_=0;faulted_=false;
}
S NativeGameContext::bind(WorldClock& clock,GameState& state,DialogueMachine& machine,
    const RuntimeClockBinding* bindings,std::size_t count,const GameContextServices& services) noexcept {
    if(busy_ || provider_busy_) return S::reentrant;
    if(machine_) return S::invalid_state;
    if(!clock.configured() || count>32 || (count && !bindings) ||
       machine.expression_provider() || machine.expression_provider_context()) return S::invalid_argument;
    const void* objects[5]={this,&clock,&state,&machine,services.facing};
    const std::size_t sizes[5]={sizeof(*this),sizeof(clock),sizeof(state),sizeof(machine),services.facing?sizeof(GameEventFacing):0};
    for(unsigned i=0;i<5;++i) {
        if(sizes[i] && !state.storage_disjoint(objects[i],sizes[i])) return S::invalid_argument;
        for(unsigned j=i+1;j<5;++j) if(!disjoint(objects[i],sizes[i],objects[j],sizes[j])) return S::invalid_argument;
    }
    if((count && reinterpret_cast<std::uintptr_t>(bindings)%alignof(RuntimeClockBinding)) ||
       !disjoint(bindings,count*sizeof(*bindings),this,sizeof(*this)) ||
       !disjoint(&services,sizeof(services),this,sizeof(*this))) return S::invalid_argument;
    for(const void* context:{services.host_context,services.fallback_context}) if(context) {
        for(unsigned i=0;i<5;++i) if(!disjoint(context,1,objects[i],sizes[i])) return S::invalid_argument;
        if(!state.storage_disjoint(context,1)) return S::invalid_argument;
    }
    for(std::size_t i=0;i<count;++i) {
        if(bindings[i].runtime_mode>255 || !field_valid(bindings[i].field)) return S::invalid_argument;
        for(std::size_t j=0;j<i;++j) if(bindings[i].runtime_mode==bindings[j].runtime_mode) return S::invalid_argument;
    }
    const auto bound=machine.bind_expression_provider(provider,this);
    if(bound!=DialogueMachineStatus::ready) return S::invalid_state;
    clock_=&clock;state_=&state;machine_=&machine;services_=services;count_=count;
    for(std::size_t i=0;i<count;++i) bindings_[i]=bindings[i];
    faulted_=false;return S::ok;
}
S NativeGameContext::unbind() noexcept {
    if(busy_ || provider_busy_) return S::reentrant;
    if(!machine_) return S::invalid_state;
    if(!owns_provider()) {
        if(machine_->expression_provider_context()!=this) detach();
        return S::provider_conflict;
    }
    if(machine_->bind_expression_provider(nullptr,nullptr)!=DialogueMachineStatus::ready) return S::invalid_state;
    detach();return S::ok;
}
DialogueExpressionValue NativeGameContext::provider(void* context,const DialogueOperandRequest& request) noexcept {
    auto& self=*static_cast<NativeGameContext*>(context);
    if(self.provider_busy_ || self.faulted_ || !self.owns_provider() || !self.storage_safe()) return {};
    self.provider_busy_=true;
    DialogueExpressionValue result{};
    if(request.kind==DialogueOperandKind::state_bank) result=GameState::expression_provider(self.state_,request);
    else {
        bool mapped=false;
        if(request.kind==DialogueOperandKind::runtime_selector) for(std::size_t i=0;i<self.count_;++i) {
            if(self.bindings_[i].runtime_mode!=request.index) continue;
            mapped=true;WorldClockState s{};
            if(self.clock_->state(s)!=WorldClockError::ok) break;
            double value=0;bool valid=true;
            switch(self.bindings_[i].field) {
            case RuntimeClockField::year:value=s.date.year;break;
            case RuntimeClockField::month:value=s.date.month;break;
            case RuntimeClockField::day:value=s.date.day;break;
            case RuntimeClockField::day_number:if(s.day_number>9007199254740992ULL) valid=false;else value=static_cast<double>(s.day_number);break;
            case RuntimeClockField::hour:value=s.second_of_day/3600;break;
            case RuntimeClockField::minute:value=(s.second_of_day/60)%60;break;
            case RuntimeClockField::second:value=s.second_of_day%60;break;
            case RuntimeClockField::second_of_day:value=s.second_of_day;break;
            case RuntimeClockField::nanosecond:value=s.nanosecond;break;
            case RuntimeClockField::phase:value=static_cast<unsigned>(s.phase);break;
            case RuntimeClockField::paused:value=s.paused?1:0;break;
            }
            if(valid) result={true,value};break;
        }
        if(!mapped && self.services_.fallback) result=self.services_.fallback(self.services_.fallback_context,request);
    }
    self.provider_busy_=false;return result;
}
S NativeGameContext::advance_world(std::uint64_t elapsed,WorldClockError& out) noexcept {
    if(busy_ || provider_busy_) return S::reentrant;
    if(!machine_) return S::invalid_state;
    if(faulted_) return S::faulted;
    if(!owns_provider()) return S::provider_conflict;
    if(!storage_safe()) return S::invalid_argument;
    if(!disjoint(&out,sizeof(out),this,sizeof(*this)) || !disjoint(&out,sizeof(out),clock_,sizeof(*clock_)) ||
       !disjoint(&out,sizeof(out),state_,sizeof(*state_)) || !disjoint(&out,sizeof(out),machine_,sizeof(*machine_)) ||
       !state_->storage_disjoint(&out,sizeof(out)) ||
       (services_.facing && !disjoint(&out,sizeof(out),services_.facing,sizeof(GameEventFacing)))) return S::invalid_argument;
    busy_=true;const auto error=clock_->advance_elapsed_ns(elapsed);busy_=false;out=error;
    return error==WorldClockError::ok?S::ok:S::invalid_state;
}
GameContextStep NativeGameContext::step_dialogue(std::size_t budget,const ResolvedGameFacing* facing) noexcept {
    GameContextStep r;
    if(busy_ || provider_busy_) {r.status=S::reentrant;return r;}
    if(!budget || budget>4096) {r.status=S::invalid_argument;return r;}
    if(!machine_) {r.status=S::invalid_state;return r;}
    if(faulted_) {r.status=S::faulted;return r;}
    if(!owns_provider()) {r.status=S::provider_conflict;return r;}
    if(!storage_safe()) {r.status=S::invalid_argument;return r;}
    const ResolvedGameFacing resolved=facing?*facing:ResolvedGameFacing{};
    busy_=true;
    r.machine=machine_->advance(budget);
    const auto* pending=machine_->pending();
    if(!pending) {
        switch(r.machine) {
        case DialogueMachineStatus::ready:case DialogueMachineStatus::complete:
        case DialogueMachineStatus::budget_exhausted:break;
        case DialogueMachineStatus::predicate_required:r.status=S::dialogue_blocked;break;
        case DialogueMachineStatus::invalid_argument:r.status=S::invalid_argument;break;
        case DialogueMachineStatus::invalid_state:r.status=S::invalid_state;break;
        default:r.status=S::dialogue_failure;break;
        }
        busy_=false;return r;
    }
    const auto request=*pending;r.token=request.token;
    if(facing && (request.instruction.command!=DialogueBodyCommand::native_command || resolved.token!=request.token ||
        resolved.request.command_word!=request.instruction.operand)) {
        busy_=false;r.status=S::invalid_argument;return r;
    }
    GameHostResponse response;
    if(request.instruction.command==DialogueBodyCommand::state_bank_write) {
        r.state=state_->apply_bank2_write(request.instruction);
        response.result=r.state==GameStateStatus::ok?GameServiceResult::applied:GameServiceResult::unavailable;
    } else if(facing) {
        if(services_.facing) r.facing=services_.facing->apply(resolved.request);
        response.result=r.facing==GameEventResult::applied?GameServiceResult::applied:
            r.facing==GameEventResult::failed?GameServiceResult::failed:GameServiceResult::unavailable;
    } else if(services_.host) response=services_.host(services_.host_context,request);
    r.effect_applied=response.result==GameServiceResult::applied;
    const auto* current=machine_->pending();
    if(!owns_provider() || !storage_safe() || !current || current->token!=request.token) {
        faulted_=true;r.status=S::acknowledgement_failed;busy_=false;return r;
    }
    if(!r.effect_applied) {
        r.status=response.result==GameServiceResult::failed?S::host_failed:S::unavailable;busy_=false;return r;
    }
    if(response.has_offset!=request.requires_offset || (response.has_offset && response.offset>=machine_->route_size())) {
        faulted_=true;r.status=S::acknowledgement_failed;busy_=false;return r;
    }
    const DialogueMachineAck ack{request.token,true,response.has_offset,response.offset};
    r.machine=machine_->acknowledge(ack);
    r.acknowledged=r.machine==DialogueMachineStatus::ready || r.machine==DialogueMachineStatus::complete;
    if(!r.acknowledged) {faulted_=true;r.status=S::acknowledgement_failed;}
    busy_=false;return r;
}
}
