# Bounded native SRF dialogue-resource view

`include/cedar/srf.hpp` and `src/srf.cpp` are a modified allocation-free native
adaptation of authored brynnb/new-yokosuka `parse_srf` / `parse_srf_timing` at
`b0df7f2720a7eecabce2e610fdedec35bf1243e3`. They are marked **GPL-3.0-only**,
not covered by the separate MIT ShenmueDK notice. The root GPL license was read
before source; preserve `third_party/licenses/new-yokosuka-GPL3.txt`.

Exact source:
[`tools/scripting/extract_dialogue_inventory.py`](http<LOCAL_REFERENCE>/extract_dialogue_inventory.py),
17,186 bytes, Git blob `7029f4b7ec34cdf4cb8d8f12bc25a582dd589649`,
SHA256 `526deac8f4a3cb8050dd3c4bda8787c7a344976a2c8dd89de16e66683747fa7f`.
No original dialogue, voice, actor table, recovered game logic or other data is
copied into this module. `DIALOGUE-RESOURCE-EVIDENCE.md` records the bounded audit.

## Exact supported bytes

The caller explicitly supplies an SRF member; there is no invented file magic,
archive search or admission based on resolution/name. Each record is three
consecutive blocks:

| Order | Block payload | Prefix |
| --- | --- | --- |
| 1 | Speaker ID bytes, possibly NUL/space padded | LE u32 inclusive block length |
| 2 | Subtitle bytes, possibly trailing NUL padded | Same |
| 3 | Timing bytes or empty payload | Same |

Every block length includes its four-byte prefix, is at least4, is a multiple
of4 and must fit the supplied member. No alignment/padding is skipped between
these three blocks. Speaker ID trims only trailing NUL and ASCII space; remaining
bytes must be printable ASCII0x20–0x7E. An empty ID remains empty and never
selects a fabricated actor. Both raw speaker bytes and trimmed ID are exposed.

Text is exposed as exact raw bytes and a separate view removing trailing NUL
bytes only. No EUC-JP, CP932, ASCII, UTF conversion, locale detection, replacement,
control-glyph normalization, font, story selection or playback is performed.

Between complete records, an all-zero tail ends the resource. Otherwise a zero
length word can only introduce zero-filled padding through the next0x800 sector
boundary. Entire additional zero sectors may be skipped, but the next record
must start at a sector boundary; a nonzero byte elsewhere in that padding is an
error. Padding cannot conceal a truncated block inside a record. This preserves
the primary parser's explicit all-zero final-tail behavior, including short tails.
Nonnull zero-length/all-zero resources are valid empty streams, not dialogue.

## Timing profile and deliberately absent interpretation

An empty timing payload has zero cues. Otherwise it ends with exactly four FF
bytes after a multiple of8 bytes of tuples. A terminator-only block also has zero
cues. Each tuple contains four little-endian signed16 fields:

| Offset | Field | Supported values |
| --- | --- | --- |
| 0 | Numeric mouth-shape selector | 0–5 |
| 2 | Channel | 2 |
| 4 | Duration ticks | 1–32767 |
| 6 | Reserved | 0 |

Negative/high-bit durations fail instead of wrapping. Unsupported shapes,
channels, reserved fields, missing terminators and malformed tuple extents fail
explicitly. Values are exposed as exact numeric source fields; no facial mesh,
bone, phoneme, blend shape or interpolation is inferred. The upstream inventory
labels a60Hz timebase, but this module intentionally exposes raw ticks only:
no native playback clock or seconds conversion has been proven/integrated.

## APIs, limits and failure handling

`SrfView::parse(bytes,length)` validates the **entire** stream before publishing
any view. `cursor().next(record)` provides linear iteration; indexed `record(i)`
scans preceding records. A record exposes offsets, byte length, borrowed speaker/
text/timing spans and cue count. `record.cue(i)` performs bounded O(1) tuple access
and rechecks its timing extent/terminator before returning values.

Implementation limits are64MiB input,65,536 records and1,048,576 total cues; they
are resource/work bounds, not asserted format maxima. Pointer/offset arithmetic
and all block/cue extents are checked. Zero-prefix scans consume/skip that run or
end the stream, rather than repeating the Python source's whole-tail `any` scan
for every record. A complete parse/full cursor walk is linear in member bytes
and returned records/cues, rather than quadratic in record count.

Input bytes remain immutable and alive throughout every view, cursor and span
use; do not mutate returned metadata or resume a cursor after a non-end error.
The code allocates nothing and performs no I/O. Writable record/cue outputs are
checked against the full borrowed input and relevant view/cursor objects before
clearing or writing them. Input overlapping the parser object is likewise
rejected without writes. These alias errors leave both objects unchanged;
ordinary parse failures clear any prior view, and ordinary accessor failures
clear their output. Successful views are committed only after complete validation.

This module does not match STR voices, traverse AFS/TAC files, discover resource
names, implement a script VM, duplicate dialogue progress state, create actors,
render subtitles or establish playable dialogue. The lead's separate AFS index
may supply an explicitly selected member span; the current PC TAC payloads have
not been inspected or proven to contain this profile.

No CMake/shared-source changes, compilation, tests, parser execution, runtime,
game-data reads, extraction or asset copying occurred in this worker batch.
Lead owns source review and compilation; human runtime qualification is separate.
