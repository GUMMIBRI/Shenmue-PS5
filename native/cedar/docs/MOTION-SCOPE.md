# Motion-resource investigation: precise decoder blocker

Bounded source-only receipt, November 5, 2026. No motion decoder, motion player,
animation sampler, hierarchy binding or interpolation was implemented. No game
data, assets, binaries, restricted checkout, extraction or runtime/test execution
was used. No alternate format/repository survey was started.

## Compatible primary source and permission

Existing project provenance pins independent Shenmue-Mods/ShenmueDK at
`1dae443814b3755281d207a13f25571c0e51a5d1` (for example `MT5-SCOPE.md`). The
[MIT license](http<LOCAL_REFERENCE>/LICENSE)
was read before this tranche's new source reads. Copyright (c) 2019 Phil;
complete existing notice: `third_party/licenses/shenmuedk-MIT.txt`. No conflicting
file-level license appeared in the reviewed files. The blocker below is format
semantics, not a claim that this independently MIT-licensed utility is forbidden.
It supplies no new permission for restricted recovered engine code.

Primary evidence reviewed:

- [MOTN definition](http<LOCAL_REFERENCE>/motn.h)
- [MOTN reader](http<LOCAL_REFERENCE>/motn.cpp)
- [Sequence/Frame/BoneKeyframes data types](http<LOCAL_REFERENCE>/model.h)
- [Scalar helper](http<LOCAL_REFERENCE>/math.h)

## What is concretely present

The reader is substantive, not an empty stub: it reads a five-word file header,
sequence offset/name tables, sequence headers, descriptor axes, per-axis key
counts, frame times, two-bit key flags and encoded scalar words. It identifies
position/rotation axes and retains numeric bone selectors. That is useful primary
evidence, but does not by itself establish a faithful safe animation decoder.

### Blocking inconsistencies

1. `Sequence::Header::length` is a **signed 16-bit** value. Its high bit controls
   the count-entry width, and the low15 bits control frame-time width. The reader
   nevertheless constructs its terminal frame time from the unmasked signed
   `length - 1`. A flagged/negative length therefore cannot simply be treated as
   a positive duration without choosing an interpretation absent from this source.
2. Eight-bit count/time reads use plain `char`, then promote it to `int`.
   Values128–255 are platform-signedness dependent. Wider reads use signed
   `short`. The source does not explain valid ranges or sentinel meanings that
   would justify silently replacing them with unsigned quantities.
3. The flag-block reader writes **four frame flag pairs unconditionally** per
   byte while only checking the loop bound before that group. It has already
   inserted initial/terminal endpoint records. When a channel's stored count+2
   is not divisible by4, the group can index beyond its vector. The intended
   final-group padding/packing behavior is not specified safely by this code.
4. The helper labeled as IEEE754 half conversion only rebiases/repositions bits;
   it does not implement IEEE zero, subnormal or nonfinite special cases. A zero
   encoded word maps to nonzero binary32 bits. That might be a source defect or
   a format-specific scalar convention; no reviewed primary evidence establishes
   which. Substituting a generic half decoder would invent value semantics.
5. A two-byte sequence-header word at offset2 is not read. Block1 uses the data
   type's hardcoded offset12; subsequent blocks use signed relative offsets.
   `_isValid` always returns false, and the writer has no implementation. There
   is no independent format/version assertion here to narrow all these cases.

The inserted endpoint records have default values when no encoded value is
present. Those defaults must not be advertised as authored keys without proven
endpoint semantics. Numeric bone selectors and easing fields also do not prove
model binding, interpolation rules, playback timing or original gameplay behavior.

## Safe lossless metadata boundary, deliberately not filler code

A separate explicitly selected MOTN metadata index could eventually expose raw
header words, stored sequence offsets, names and opaque attributes without
interpreting numeric motion. The source's concrete reads are:

- Header offsets0/4/8: sequence table, name-pointer table, sequence-data base;
  offset12: attributes; offset16: stored file length.
- Sequence count is computed as `(attributes & 0x0FFF) - 1`; the zero-low-count
  underflow must be rejected or specified, not copied into an unbounded index.
- Each sequence-table record is two32-bit words. The first is added to the
  sequence-data base; the second (`extraDataOfs`) is retained without established
  interpretation. Name-table records are absolute file-origin string pointers.

Such an index would require explicit caller format selection, bounded/terminated
name views, checked signed-offset arithmetic and real file ranges. It would not
decode motion or prove resources are valid animations. No current native motion
consumer was established in this finite task, so a metadata-only catalog was not
added merely to count code as animation progress.

## Exact next evidence required

Need a compatible primary specification or corrected independently licensed
implementation establishing: duration/width-flag handling; count/time signedness
and sentinels; final flag-group packing; encoded scalar semantics including
special values; and implicit endpoint behavior. With those rules, a bounded
caller-buffer key/channel decoder can be implemented independently, while keeping
bone binding/interpolation separate and explicitly unimplemented until proven.
No request to the owner, broader survey or restricted-code workaround is implied.
