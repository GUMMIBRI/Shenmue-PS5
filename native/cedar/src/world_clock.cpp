// Adapted from internal/worldstate/clock.go, Brynn Bateman, copyright 2026.
// MIT notice: third_party/licenses/new-yokosuka-server-world-clock-MIT.txt.
#include "cedar/world_clock.hpp"
#include <limits>

namespace cedar { namespace {
constexpr std::uint64_t billion = 1000000000ULL;
bool leap(std::uint32_t y) noexcept { return y%4==0 && (y%100!=0 || y%400==0); }
std::uint32_t month_days(std::uint32_t y, std::uint32_t m) noexcept {
    constexpr std::uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m-1]+(m==2 && leap(y) ? 1U:0U);
}
std::uint64_t year_start(std::uint32_t y) noexcept {
    const std::uint64_t n=y-1; return n*365+n/4-n/100+n/400;
}
std::uint64_t ordinal(WorldDate d) noexcept {
    auto n=year_start(d.year);
    for(std::uint32_t m=1;m<d.month;++m) n+=month_days(d.year,m);
    return n+d.day-1;
}
WorldDate date_from(std::uint64_t n) noexcept {
    std::uint32_t lo=1,hi=10000;
    while(lo+1<hi) { const auto mid=(lo+hi)/2; if(year_start(mid)<=n) lo=mid; else hi=mid; }
    n-=year_start(lo); std::uint32_t m=1;
    while(n>=month_days(lo,m)) { n-=month_days(lo,m); ++m; }
    return {static_cast<std::uint16_t>(lo),static_cast<std::uint8_t>(m),static_cast<std::uint8_t>(n+1)};
}
bool policy_valid(const WorldClockPolicy& p) noexcept {
    const auto d=p.epoch_date;
    if(p.calendar!=WorldCalendar::gregorian || d.year<1 || d.year>9999 || d.month<1 || d.month>12 ||
       d.day<1 || d.day>month_days(d.year,d.month) ||
       p.active_begin_second>=p.active_end_second || p.active_end_second>86400 ||
       p.epoch_second<p.active_begin_second || p.epoch_second>=p.active_end_second ||
       p.rate_numerator<1 || p.rate_numerator>billion || p.rate_denominator<1 || p.rate_denominator>billion) return false;
    for(unsigned i=0;i<5;++i)
        if(p.phase_begin_second[i]>86400 || (i && p.phase_begin_second[i]<p.phase_begin_second[i-1])) return false;
    return true;
}
std::uint64_t span(const WorldClockPolicy& p) noexcept {
    return (p.active_end_second-p.active_begin_second)*billion;
}
bool snapshot_valid(const WorldClockSnapshot& s) noexcept {
    return policy_valid(s.policy) && s.revision!=0 && s.active_offset_ns<span(s.policy) &&
        s.rate_remainder<s.policy.rate_denominator &&
        s.day_number<year_start(10000)-ordinal(s.policy.epoch_date);
}
} // namespace
WorldClockError WorldClock::configure(const WorldClockPolicy& p) noexcept {
    if(!policy_valid(p)) return WorldClockError::invalid_policy;
    value_={p,0,(p.epoch_second-p.active_begin_second)*billion,0,1,false};
    configured_=true; return WorldClockError::ok;
}
WorldClockError WorldClock::advance_elapsed_ns(std::uint64_t elapsed) noexcept {
    if(!configured_) return WorldClockError::invalid_state;
    if(value_.paused || elapsed==0) return WorldClockError::ok;
    const auto& p=value_.policy;
    constexpr auto max=std::numeric_limits<std::uint64_t>::max();
    // Split before multiplication. Fraction product <1e18; no floating-point drift.
    const auto q=elapsed/p.rate_denominator;
    const auto fraction=(elapsed%p.rate_denominator)*p.rate_numerator+value_.rate_remainder;
    if(q>max/p.rate_numerator) return WorldClockError::overflow;
    const auto whole=q*p.rate_numerator, extra=fraction/p.rate_denominator;
    if(whole>max-extra) return WorldClockError::overflow;
    const auto increment=whole+extra, length=span(p);
    const auto offset=value_.active_offset_ns+increment%length; // <2*86400e9.
    const auto days=increment/length+offset/length;
    const auto available=year_start(10000)-1-ordinal(p.epoch_date)-value_.day_number;
    if(days>available || value_.revision==max) return WorldClockError::overflow;
    value_.day_number+=days; value_.active_offset_ns=offset%length;
    value_.rate_remainder=static_cast<std::uint32_t>(fraction%p.rate_denominator);
    ++value_.revision;
    return WorldClockError::ok;
}
WorldClockError WorldClock::set_paused(bool paused) noexcept {
    if(!configured_) return WorldClockError::invalid_state;
    if(value_.paused==paused) return WorldClockError::ok;
    if(value_.revision==std::numeric_limits<std::uint64_t>::max()) return WorldClockError::overflow;
    value_.paused=paused; ++value_.revision; return WorldClockError::ok;
}
WorldClockError WorldClock::pause() noexcept { return set_paused(true); }
WorldClockError WorldClock::resume() noexcept { return set_paused(false); }
WorldClockError WorldClock::snapshot(WorldClockSnapshot& out) const noexcept {
    if(!configured_) return WorldClockError::invalid_state;
    out=value_; return WorldClockError::ok;
}
WorldClockError WorldClock::restore(const WorldClockSnapshot& s) noexcept {
    if(!snapshot_valid(s)) return WorldClockError::invalid_policy;
    value_=s; configured_=true; return WorldClockError::ok;
}
WorldClockError WorldClock::state(WorldClockState& out) const noexcept {
    if(!configured_) return WorldClockError::invalid_state;
    const auto second=value_.policy.active_begin_second+static_cast<std::uint32_t>(value_.active_offset_ns/billion);
    const auto* t=value_.policy.phase_begin_second;
    WorldDayPhase phase=WorldDayPhase::night;
    if(second>=t[0] && second<t[1]) phase=WorldDayPhase::sunrise;
    else if(second>=t[1] && second<t[2]) phase=WorldDayPhase::day;
    else if(second>=t[2] && second<t[3]) phase=WorldDayPhase::sunset;
    else if(second>=t[3] && second<t[4]) phase=WorldDayPhase::evening;
    out={date_from(ordinal(value_.policy.epoch_date)+value_.day_number),value_.day_number,value_.revision,
        second,static_cast<std::uint32_t>(value_.active_offset_ns%billion),value_.rate_remainder,phase,value_.paused};
    return WorldClockError::ok;
}
} // namespace cedar
