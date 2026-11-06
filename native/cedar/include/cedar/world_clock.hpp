// Adapted from Brynn Bateman's MIT-licensed worldstate clock; see WORLD-CLOCK.md.
#pragma once
#include <cstdint>

namespace cedar {
enum class WorldClockError { ok, invalid_policy, invalid_state, overflow };
enum class WorldCalendar { gregorian };
enum class WorldDayPhase { night, sunrise, day, sunset, evening };
struct WorldDate { std::uint16_t year; std::uint8_t month, day; };
struct WorldClockPolicy {
    WorldCalendar calendar;
    WorldDate epoch_date;
    std::uint32_t epoch_second; // Civil second of day, inside active interval.
    std::uint32_t active_begin_second, active_end_second; // [begin,end), end <=86400.
    std::uint32_t rate_numerator, rate_denominator; // game ns / caller elapsed ns, each 1..1e9.
    // Ordered civil-second thresholds: sunrise, day, sunset, evening, night.
    std::uint32_t phase_begin_second[5];
};
struct WorldClockSnapshot {
    WorldClockPolicy policy;
    std::uint64_t day_number; // Days since epoch date, not total active 24h periods.
    std::uint64_t active_offset_ns;
    std::uint32_t rate_remainder; // Exact numerator remainder modulo denominator.
    std::uint64_t revision;
    bool paused;
};
struct WorldClockState {
    WorldDate date;
    std::uint64_t day_number, revision;
    std::uint32_t second_of_day, nanosecond, rate_remainder;
    WorldDayPhase phase;
    bool paused;
};
// Serialized caller ownership. No wallclock reads or internally driven updates.
class WorldClock {
public:
    WorldClockError configure(const WorldClockPolicy&) noexcept;
    WorldClockError advance_elapsed_ns(std::uint64_t elapsed_ns) noexcept;
    WorldClockError pause() noexcept;
    WorldClockError resume() noexcept;
    WorldClockError snapshot(WorldClockSnapshot&) const noexcept;
    // Explicit restoration may rewind. All policy/state fields are validated first.
    WorldClockError restore(const WorldClockSnapshot&) noexcept;
    WorldClockError state(WorldClockState&) const noexcept;
    bool configured() const noexcept { return configured_; }
private:
    WorldClockError set_paused(bool) noexcept;
    WorldClockSnapshot value_{};
    bool configured_ = false;
};
} // namespace cedar
