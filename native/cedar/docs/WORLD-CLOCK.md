# Native caller-driven world clock

`WorldClock` provides calendar/time/day-phase state for real engine event and
predicate providers. It adapts the active-day/calendar progression of the MIT
New Yokosuka server clock, not an original Shenmue timing specification. Nothing
currently binds it to an engine timer, event dispatcher or save serializer.

Configure an explicit `WorldClockPolicy`: Gregorian date (years 1..9999), initial
civil second, active interval `[begin,end)` within one civil day, positive rational
game-time/elapsed-time rate, and five ordered civil-second phase boundaries.
Each rate term is at most 1,000,000,000. Equal phase thresholds intentionally make
that phase empty. Midnight is second 0; the exclusive interval end may be 86400.
No epoch, rate, story calendar, seasonal/weather rule or active interval is supplied
by default. The epoch time must lie inside the active interval.

Call `advance_elapsed_ns` with a nonnegative elapsed duration from the engine's
chosen time source. Durations are not absolute timestamps: callers must reject
backwards timestamps before subtracting and must not convert negative signed
durations to unsigned. No clock reads, network synchronization, autonomous loop,
wall-time catch-up or game-timer installation occurs. During pause elapsed input
is discarded, including fractional input; previously accumulated fractions remain.
Resuming does not catch up paused time. Repeat pause/resume is a no-op.

The clock advances only through the explicit active interval. Crossing its end
increments the calendar date/day number and wraps to its beginning, including leap
days under the proleptic Gregorian calendar. Engine predicates receive date,
day number, civil second, nanosecond, phase, pause state and revision through
`state`. Phase intervals are night before sunrise, sunrise, day, sunset, evening,
then night. No artificial season or weather is inferred from the date.

Integer division preserves the exact rate remainder, so splitting an elapsed
duration across calls preserves final calendar time/remainder. Revision starts at
1, increments on every successful nonzero unpaused advance (even fractional-only)
and each pause-state change. Revision therefore describes mutations rather than a
wallclock or frame count. Overflow, year-10000 progression and malformed policy
are rejected without changing prior state. Intermediate 64-bit conversion overflow
also returns `overflow`; there is no saturation, wrapping or silent downgrade.

`snapshot` copies policy, day number, active nanosecond offset, exact rate remainder,
revision and pause state. `restore` validates all fields before replacing state;
it is an explicit rewind/revision-reset operation, including on an already running
clock. A maximum revision snapshot can be restored, but subsequent mutating calls
fail overflow. This is a typed in-memory snapshot, not a binary persistence ABI;
save systems must serialize named fields and their version explicitly. Output
arguments remain unchanged on failure. Failed configure/restore retains the old
clock. The class requires serialized caller ownership and no heap allocation.

## Retained source and adaptation

Primary source: [brynnb/new-yokosuka-server clock.go](http<LOCAL_REFERENCE>/clock.go),
pin `8eb44791734f594c7348394ff213705502ba8e1e`, 5401 bytes,
SHA-256 `3923c2328d93ef016156bc4eb0df48040291ded2b8049b1787e6881a299361cd`.
The file has no separate license notice. Its repository MIT license, copyright
2026 Brynn Bateman, is retained verbatim in
`third_party/licenses/new-yokosuka-server-world-clock-MIT.txt` (1070 bytes,
SHA-256 `2f9dc7763f08faad9987952999394dcbd071a51621ecdc1fb0ae056dd86ff713`).

The source projects elapsed time into an active day, advances the calendar at the
active end, classifies time of day and carries revision metadata. This C++
adaptation preserves those useful world-state responsibilities but replaces its
floating-point/wallclock/server epoch mechanism with checked caller elapsed input,
explicit configuration, pause and exact snapshots. Source weather/season replication
and time-of-day setters are outside this module.

For callers deliberately selecting the server's demonstrated policy, its full
24-hour scale corresponds to rate 15/1; its active interval is 08:30..23:30
(30600..84600 seconds), giving a 60-real-minute active day. Its phase transitions
are 08:30, 08:52:30, 18:30, 19:22:30, 20:07:30. The source labels the first
transition region `sunset`; this API explicitly calls that region `sunrise`.
Thus selecting these numeric thresholds is not a claim of identical protocol
labels. Its June 9, 1986 shared multiplayer calendar origin is deliberately not
adopted as an original story epoch. None of these source policy values are
automatically selected or claimed as verified original game behavior.

Delivery is source-only: no runtime, fixtures or game-time qualification was run.
Lead integration must add `src/world_clock.cpp` to the appropriate native core
target and connect actual event/predicate/save providers explicitly.
