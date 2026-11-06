// SPDX-License-Identifier: GPL-3.0-only
// Adapted from brynnb/new-yokosuka at b0df7f2720a7eecabce2e610fdedec35bf1243e3.
// GPL v3 text: third_party/licenses/new-yokosuka-GPL3.txt. No generated game data.
#include "cedar/dialogue_progress.hpp"
#include <cstring>
#include <limits>

namespace cedar {
namespace {
using S = DialogueProgressStatus;
bool range(const void* p, std::size_t n, std::uintptr_t& a, std::uintptr_t& b) noexcept {
    a = reinterpret_cast<std::uintptr_t>(p);
    if (!p || n > std::numeric_limits<std::uintptr_t>::max() - a) return false;
    b = a + n; return true;
}
bool separate(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    std::uintptr_t x,y,z,w;
    return range(a,an,x,y) && range(b,bn,z,w) && (y <= z || w <= x);
}
std::uint16_t u16(const std::uint8_t* p) noexcept { return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8)); }
void put16(std::uint8_t* p, std::uint32_t v) noexcept { p[0]=static_cast<std::uint8_t>(v); p[1]=static_cast<std::uint8_t>(v>>8); }
bool field_offset(DialogueProgressField f, std::size_t& o) noexcept {
    switch(f) {
    case DialogueProgressField::selected:o=0;return true;
    case DialogueProgressField::continuation:o=2;return true;
    case DialogueProgressField::boundary:o=4;return true;
    case DialogueProgressField::latest:o=6;return true;
    } return false;
}
}
void DialogueProgress::initialize_reference_defaults() noexcept {
    bytes_.fill(0);
    // Source's float32 threshold 9.0, explicitly encoded little endian.
    for(std::size_t i=0;i<record_count;++i) { bytes_[i*12+10]=0x10; bytes_[i*12+11]=0x41; }
    initialized_=true;
}
S DialogueProgress::load_snapshot(const void* p,std::size_t n) noexcept {
    if(n!=snapshot_size) return S::invalid_size;
    if(!separate(p,n,this,sizeof(*this))) return S::invalid_buffer;
    std::memcpy(bytes_.data(),p,n); initialized_=true; return S::ok;
}
S DialogueProgress::store_snapshot(void* p,std::size_t n) const noexcept {
    if(!initialized_) return S::uninitialized;
    if(n<snapshot_size) return S::invalid_size;
    if(!separate(p,snapshot_size,this,sizeof(*this))) return S::invalid_buffer;
    std::memcpy(p,bytes_.data(),snapshot_size); return S::ok;
}
S DialogueProgress::record(std::size_t i,DialogueProgressRecord& out) const noexcept {
    if(!initialized_) return S::uninitialized;
    if(i>=record_count) return S::invalid_index;
    if(!separate(&out,sizeof(out),this,sizeof(*this))) return S::invalid_buffer;
    const auto* p=bytes_.data()+i*12;
    DialogueProgressRecord r{u16(p),u16(p+2),u16(p+4),u16(p+6),
        std::uint32_t(p[8])|(std::uint32_t(p[9])<<8)|(std::uint32_t(p[10])<<16)|(std::uint32_t(p[11])<<24)};
    out=r; return S::ok;
}
S DialogueProgress::write_offset(std::size_t i,DialogueProgressField f,std::uint32_t v) noexcept {
    if(!initialized_) return S::uninitialized;
    if(i>=record_count) return S::invalid_index;
    std::size_t o;
    if(v>65535 || !field_offset(f,o)) return S::invalid_offset;
    put16(bytes_.data()+i*12+o,v); return S::ok;
}
S DialogueProgress::begin_selection(std::size_t i,std::uint32_t v,bool& changed) noexcept {
    if(!separate(&changed,sizeof(changed),this,sizeof(*this))) return S::invalid_buffer;
    DialogueProgressRecord r; const auto s=record(i,r); if(s!=S::ok) return s;
    if(v>65535) return S::invalid_offset;
    const bool different=r.selected!=v;
    if(different) { auto* p=bytes_.data()+i*12; put16(p,v); std::memset(p+2,0,6); }
    changed=different; return S::ok;
}
S DialogueProgress::resolve_redirect(std::size_t i,std::uint32_t following,bool saved,DialogueProgressRedirect& out) noexcept {
    if(!separate(&out,sizeof(out),this,sizeof(*this))) return S::invalid_buffer;
    DialogueProgressRecord r; const auto s=record(i,r); if(s!=S::ok) return s;
    if(following>65535) return S::invalid_offset;
    std::int64_t resolved=following; bool yield=false, update=false;
    if(!saved) {
        if(following==r.selected) {
            if(r.latest!=0) resolved=r.latest;
            if(resolved<r.boundary) resolved=std::int64_t(r.boundary)-2;
            if(resolved<r.continuation) { resolved=r.continuation; yield=true; }
        } else update=true;
    }
    if(resolved<0 || resolved>65535) return S::invalid_offset;
    auto* p=bytes_.data()+i*12;
    if(update) { put16(p+6,following); put16(p+4,0); }
    put16(p+2,0);
    out={static_cast<std::uint16_t>(resolved),yield}; return S::ok;
}
S DialogueProgress::complete_state5(std::size_t i,const void* route,std::size_t n,
    const DialogueProgressRuntime& input,DialogueProgressCompletion& out) const noexcept {
    if(!separate(&out,sizeof(out),this,sizeof(*this)) ||
       !separate(&out,sizeof(out),&input,sizeof(input))) return S::invalid_buffer;
    DialogueProgressRecord r; const auto s=record(i,r); if(s!=S::ok) return s;
    std::uintptr_t a,b;
    if(n>65536 || !range(route,n,a,b) || !separate(&out,sizeof(out),route,n)) return S::invalid_route;
    const auto rt=input;
    if(rt.current_offset>65535 || rt.current_offset>n ||
       (rt.has_continuation5c && (rt.continuation5c_offset>65535 || rt.continuation5c_offset>n))) return S::invalid_offset;
    DialogueProgressCompletion result{};
    result.flags1e=rt.flags1e;
    if((rt.flags1e&1)==0 || (rt.flags1e&0x40)!=0) {
        const auto next=std::uint64_t(rt.current_offset)+3;
        if(next>65535 || next>n) return S::invalid_route;
        result.current_offset=static_cast<std::uint16_t>(next);
        result.has_continuation5c=rt.has_continuation5c;
        result.continuation5c_offset=rt.has_continuation5c?static_cast<std::uint16_t>(rt.continuation5c_offset):0;
    } else {
        const auto end=std::uint32_t(r.continuation)+3;
        if(end>n || end>65535) return S::invalid_route;
        const auto* p=static_cast<const std::uint8_t*>(route)+r.continuation;
        const std::uint32_t bits=(std::uint32_t(p[0])<<16)|(std::uint32_t(p[1])<<8)|p[2];
        const std::int32_t displacement=bits&0x800000?static_cast<std::int32_t>(bits)-0x1000000:static_cast<std::int32_t>(bits);
        const auto target=std::int64_t(end)+displacement;
        if(target<0 || target>65535 || static_cast<std::uint64_t>(target)>n) return S::invalid_route;
        result.current_offset=static_cast<std::uint16_t>(target);
        result.flags1e=static_cast<std::uint16_t>(rt.flags1e&~1u);
        result.used_progress_continuation=true; result.displacement=displacement;
        if(displacement!=0) { result.flags1e|=0x80; result.has_continuation5c=true; result.continuation5c_offset=static_cast<std::uint16_t>(end); }
    }
    out=result; return S::ok;
}
}
