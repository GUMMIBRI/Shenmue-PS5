// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by new-yokosuka tools/lib/mapinfo_character_placements.js
// pin b0df7f2720a7eecabce2e610fdedec35bf1243e3; docs/SCENE-RESOURCE.md.
#include "cedar/scene_resource.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace cedar { namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
    "Scene placement profile requires IEEE754 binary32");
using E=SceneResourceError;
struct Span {std::uintptr_t begin{},end{};};
bool span(const void* p,std::size_t size,Span& out) noexcept {
    out.begin=reinterpret_cast<std::uintptr_t>(p);
    if(size>std::numeric_limits<std::uintptr_t>::max()-out.begin)return false;
    out.end=out.begin+size;return true;
}
bool overlap(Span a,Span b) noexcept {return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end;}
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t{p[0]}|(std::uint32_t{p[1]}<<8)|(std::uint32_t{p[2]}<<16)|(std::uint32_t{p[3]}<<24);
}
bool equal(SceneResourceString value,const char* text,std::size_t size) noexcept {
    if(value.size!=size)return false;
    for(std::size_t i=0;i<size;++i)if(value.bytes[i]!=static_cast<std::uint8_t>(text[i]))return false;
    return true;
}
bool relative(const std::uint8_t* bytes,std::size_t word,std::size_t strings_begin,
    std::size_t strings_end,SceneResourceString& value,std::size_t& target) noexcept {
    const auto delta=u32(bytes+word);
    if(word>strings_end || delta>strings_end-word)return false;
    target=word+delta;
    if(target<strings_begin || target>=strings_end)return false;
    std::size_t end=target;
    while(end<strings_end && end-target<=SceneResourceView::max_string && bytes[end]) {
        if(bytes[end]<0x20 || bytes[end]>0x7e)return false;
        ++end;
    }
    if(end==target || end==strings_end || end-target>SceneResourceView::max_string)return false;
    value={bytes+target,end-target};return true;
}
bool model_name(SceneResourceString value) noexcept {
    if(value.size<4)return false;
    const auto* s=value.bytes+value.size-4;
    return s[0]=='.' && (s[1]=='M'||s[1]=='m') && (s[2]=='T'||s[2]=='t') && s[3]=='5';
}
bool tag(const std::uint8_t* bytes) noexcept {
    for(unsigned i=0;i<4;++i)if(!((bytes[i]>='A'&&bytes[i]<='Z')||
        (bytes[i]>='0'&&bytes[i]<='9')||bytes[i]=='_'))return false;
    return true;
}
struct Chunk {std::size_t begin{},end{};};
SceneResourceResult chunk(const std::uint8_t* bytes,std::size_t size,const char* token,
    std::size_t start,Chunk& out,E missing) noexcept {
    for(std::size_t at=start;at<size && size-at>=4;++at) {
        bool match=true;for(unsigned i=0;i<4;++i)if(bytes[at+i]!=static_cast<std::uint8_t>(token[i]))match=false;
        if(!match)continue;
        if(size-at<8)return {E::malformed_chunk,at,0};
        const auto length=u32(bytes+at+4);
        if(length<8 || length>size-at)return {E::malformed_chunk,at,0};
        out={at,at+length};return {};
    }
    return {missing,start,0};
}
bool start_record(const std::uint8_t* bytes,std::size_t at,Chunk chrs,Chunk strings,
    ScenePlacementRecord& out) noexcept {
    // Narrow safety profile: complete serialized introducer inside CHRS payload.
    if(at<chrs.begin+20 || chrs.end-at<8 || !tag(bytes+at) || u32(bytes+at-4)!=0x22)return false;
    SceneResourceString model{},character{};std::size_t model_offset{},unused{};
    if(!relative(bytes,at-12,strings.begin+8,strings.end,model,model_offset) || !model_name(model) ||
        !relative(bytes,at-8,strings.begin+8,strings.end,character,unused) || !equal(character,"Character",9))return false;
    const auto count=u32(bytes+at+4);
    if(!count || count>0x100)return false;
    for(unsigned i=0;i<4;++i)out.object_tag[i]=static_cast<char>(bytes[at+i]);
    if(model.bytes[0]=='$'||model.bytes[0]=='@'){++model.bytes;--model.size;}
    out.model=model;out.model_string_offset=model_offset;out.record_offset=at;out.property_count=count;
    return true;
}
SceneResourceResult properties(const std::uint8_t* bytes,Chunk strings,ScenePlacementRecord& record) noexcept {
    bool position=false;
    for(std::size_t at=record.record_offset+8;at<=record.record_end && record.record_end-at>=8;at+=4) {
        SceneResourceString name{};std::size_t unused{};
        if(!relative(bytes,at,strings.begin+8,strings.end,name,unused))continue;
        const bool is_position=equal(name,"Position",8),is_angle=equal(name,"Angle",5),is_image=equal(name,"Image",5);
        if(!is_position && !is_angle && !is_image)continue;
        if((is_position&&position)||(is_angle&&record.has_angle)||(is_image&&record.has_image))return {E::duplicate_property,at,0};
        const auto type=u32(bytes+at+4);
        const auto count=(is_position || (is_angle && type==0x49))?3u:1u;
        if((is_position&&type!=0x49)||(is_angle&&type!=0x49&&type!=1)||(is_image&&type!=3))return {E::unsupported_property,at,0};
        if(record.record_end-at<8+4*count)return {E::malformed_record,at,0};
        if(is_image) {
            if(!relative(bytes,at+8,strings.begin+8,strings.end,record.image,unused))return {E::malformed_record,at,0};
            record.has_image=true;record.image_property_offset=at;
        } else {
            float values[3]{};
            for(unsigned i=0;i<count;++i){const auto raw=u32(bytes+at+8+4*i);std::memcpy(values+i,&raw,4);
                if(!std::isfinite(values[i]))return {E::non_finite,at+8+4*i,0};}
            if(is_position){for(unsigned i=0;i<3;++i)record.position[i]=values[i];position=true;record.position_property_offset=at;}
            else {record.has_angle=true;record.yaw_only=count==1;record.angle_property_offset=at;
                if(count==1)record.angle_degrees[1]=values[0];else for(unsigned i=0;i<3;++i)record.angle_degrees[i]=values[i];}
        }
    }
    return position?SceneResourceResult{}:SceneResourceResult{E::missing_position,record.record_offset,0};
}
} // namespace
SceneResourceResult SceneResourceView::parse(const std::uint8_t* bytes,std::size_t size,
    ScenePlacementRecord* workspace,std::size_t capacity) noexcept {
    // Avoid changing a view object through a source/workspace alias on failure.
    Span input{},output{},view{};
    if(!bytes || (capacity && (!workspace || reinterpret_cast<std::uintptr_t>(workspace)%alignof(ScenePlacementRecord))))return {E::invalid_buffer,0,0};
    if(capacity>std::numeric_limits<std::size_t>::max()/sizeof(ScenePlacementRecord) ||
        !span(bytes,size,input)||!span(workspace,capacity*sizeof(ScenePlacementRecord),output)||!span(this,sizeof(*this),view))return {E::address_overflow,0,0};
    if(overlap(input,output)||overlap(input,view)||overlap(output,view))return {E::overlapping_storage,0,0};
    records_=nullptr;count_=0;parsed_=false;
    if(size>max_bytes)return {E::resource_limit,0,0};
    Chunk chrs{},strings{};
    auto status=chunk(bytes,size,"CHRS",0,chrs,E::missing_chrs);if(!status)return status;
    status=chunk(bytes,size,"STRG",chrs.end,strings,E::missing_strg);if(!status)return status;
    std::size_t count=0;
    for(std::size_t at=chrs.begin+8;at<=chrs.end && chrs.end-at>=8;at+=4) {
        ScenePlacementRecord record{};if(!start_record(bytes,at,chrs,strings,record))continue;
        if(count==max_records)return {E::resource_limit,at,0};
        // Count all starts even on capacity query; only publish complete views.
        if(count && count<=capacity)workspace[count-1].record_end=at;
        if(count<capacity)workspace[count]=record;
        ++count;
    }
    if(count>capacity)return {E::insufficient_capacity,chrs.begin,count};
    if(count)workspace[count-1].record_end=chrs.end;
    for(std::size_t i=0;i<count;++i){status=properties(bytes,strings,workspace[i]);if(!status)return status;}
    records_=workspace;count_=count;parsed_=true;return {E::none,0,count};
}
} // namespace cedar
