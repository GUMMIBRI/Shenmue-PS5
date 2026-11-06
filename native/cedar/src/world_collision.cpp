// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by new-yokosuka src/ThirdPersonControllerRules.js b0df7f...
// Modified original C++ implementation 2026-11-05; docs/WORLD-COLLISION.md.
#include "cedar/world_collision.hpp"
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace cedar { namespace {
using E=WorldCollisionError;
constexpr double bound=1e100;
struct Point {double x,z;};
struct Span {std::uintptr_t begin{},end{};};
bool span(const void* p,std::size_t n,Span& s) noexcept {
    s.begin=reinterpret_cast<std::uintptr_t>(p);
    if(n>std::numeric_limits<std::uintptr_t>::max()-s.begin)return false;
    s.end=s.begin+n;return true;
}
bool overlap(Span a,Span b) noexcept {return a.begin<a.end&&b.begin<b.end&&a.begin<b.end&&b.begin<a.end;}
E number(double v) noexcept {return !std::isfinite(v)?E::non_finite:std::abs(v)>bound?E::coordinate_limit:E::none;}
double minimum(double a,double b) noexcept {return a<b?a:b;}
double maximum(double a,double b) noexcept {return a>b?a:b;}
double point_distance(Point p,Point a,Point b,double epsilon_squared) noexcept {
    const double dx=b.x-a.x,dz=b.z-a.z,length=dx*dx+dz*dz;
    if(length<=epsilon_squared){const double x=p.x-a.x,z=p.z-a.z;return x*x+z*z;}
    const double t=maximum(0,minimum(1,((p.x-a.x)*dx+(p.z-a.z)*dz)/length));
    const double x=p.x-(a.x+dx*t),z=p.z-(a.z+dz*t);return x*x+z*z;
}
double orientation(Point a,Point b,Point c) noexcept {return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);}
double segment_distance(Point a,Point b,Point c,Point d,double epsilon_squared) noexcept {
    const double abc=orientation(a,b,c),abd=orientation(a,b,d),cda=orientation(c,d,a),cdb=orientation(c,d,b);
    // Correct the reference's collinear-disjoint false intersection. Inclusive
    // bounding intervals also retain actual collinear/touching intersections.
    const bool boxes=maximum(minimum(a.x,b.x),minimum(c.x,d.x))<=minimum(maximum(a.x,b.x),maximum(c.x,d.x)) &&
        maximum(minimum(a.z,b.z),minimum(c.z,d.z))<=minimum(maximum(a.z,b.z),maximum(c.z,d.z));
    if(boxes && ((abc<=0&&abd>=0)||(abd<=0&&abc>=0)) &&
        ((cda<=0&&cdb>=0)||(cdb<=0&&cda>=0)))return 0;
    return minimum(minimum(point_distance(a,c,d,epsilon_squared),point_distance(b,c,d,epsilon_squared)),
        minimum(point_distance(c,a,b,epsilon_squared),point_distance(d,a,b,epsilon_squared)));
}
bool active(const WorldCollisionSegment& s,const WorldCollisionMove& m) noexcept {
    return s.enabled&&s.blocking&&(!s.has_y_range ||
        !(s.upper_y<m.y-m.vertical_tolerance || s.lower_y>m.y+m.height+m.vertical_tolerance));
}
} // namespace

WorldCollisionResult step_world_collision(const WorldCollisionSegment* segments,std::size_t count,
    const WorldCollisionMove& move,WorldCollisionStep& output) noexcept {
    if(count>65536)return {E::resource_limit};
    if(count && (!segments || reinterpret_cast<std::uintptr_t>(segments)%alignof(WorldCollisionSegment)))return {E::invalid_buffer};
    if(count>std::numeric_limits<std::size_t>::max()/sizeof(*segments))return {E::address_overflow};
    Span source{},input{},destination{};
    if(!span(segments,count*sizeof(*segments),source)||!span(&move,sizeof(move),input)||!span(&output,sizeof(output),destination))return {E::address_overflow};
    if(overlap(source,destination)||overlap(input,destination))return {E::overlapping_storage};
    const double fields[]={move.x,move.y,move.z,move.displacement_x,move.displacement_z,
        move.radius,move.height,move.epsilon,move.vertical_tolerance,move.segment_epsilon_squared};
    for(double v:fields){const auto e=number(v);if(e!=E::none)return {e};}
    if(move.radius<=0 || move.height<=0 || move.epsilon<0 || move.epsilon>=move.radius ||
        move.vertical_tolerance<0 || move.segment_epsilon_squared<0)return {E::invalid_policy};
    const Point start{move.x,move.z},end{move.x+move.displacement_x,move.z+move.displacement_z};
    for(double v:{end.x,end.z}){const auto e=number(v);if(e!=E::none)return {e};}
    for(std::size_t i=0;i<count;++i){const auto& s=segments[i];
        const double coordinates[]={s.start_x,s.start_z,s.end_x,s.end_z};
        for(double v:coordinates){const auto e=number(v);if(e!=E::none)return {e,i};}
        if(s.has_y_range){for(double v:{s.lower_y,s.upper_y}){const auto e=number(v);if(e!=E::none)return {e,i};}
            if(s.lower_y>s.upper_y)return {E::invalid_segment,i};}}
    const double clearance=move.radius-move.epsilon,clearance_squared=clearance*clearance;
    bool penetrating=false;
    for(std::size_t i=0;i<count;++i)if(active(segments[i],move)) {
        const auto& s=segments[i];
        if(point_distance(start,{s.start_x,s.start_z},{s.end_x,s.end_z},move.segment_epsilon_squared)<clearance_squared){penetrating=true;break;}
    }
    WorldCollisionStep result{};result.x=move.x;result.y=move.y;result.z=move.z;result.escaping=penetrating;
    for(std::size_t i=0;i<count;++i)if(active(segments[i],move)) {
        const auto& s=segments[i];const Point a{s.start_x,s.start_z},b{s.end_x,s.end_z};
        const double start_distance=std::sqrt(point_distance(start,a,b,move.segment_epsilon_squared));
        const double swept=segment_distance(start,end,a,b,move.segment_epsilon_squared);
        WorldCollisionBlock block=WorldCollisionBlock::none;
        if(penetrating && start_distance<clearance) {
            const double end_distance=std::sqrt(point_distance(end,a,b,move.segment_epsilon_squared));
            if(end_distance<=start_distance+move.epsilon)block=WorldCollisionBlock::contact_not_improving;
            else if(swept<=move.epsilon*move.epsilon)block=WorldCollisionBlock::contact_center_crossing;
        } else if(swept<clearance_squared)block=WorldCollisionBlock::swept_boundary;
        if(block!=WorldCollisionBlock::none){result.block=block;result.segment_index=i;output=result;return {};}
    }
    result.allowed=true;result.x=end.x;result.z=end.z;output=result;return {};
}
WorldTerrainResult world_terrain_transition(const WorldTerrainTransition& t) noexcept {
    const double values[]={t.current_y,t.height,t.normal_y,t.max_step_up,t.max_drop,t.max_slope_degrees};
    for(double v:values){const auto e=number(v);if(e!=E::none)return {e,false};}
    if(t.max_step_up<0||t.max_drop<0||t.max_slope_degrees<0||t.max_slope_degrees>180)return {E::invalid_policy,false};
    const double delta=t.height-t.current_y;
    const double normal=maximum(-1,minimum(1,t.normal_y));
    constexpr double radians_to_degrees=57.2957795130823208768;
    return {E::none,delta<=t.max_step_up && delta>=-t.max_drop && std::acos(normal)*radians_to_degrees<=t.max_slope_degrees};
}
} // namespace cedar
