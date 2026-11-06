# Standalone filtered-vgmstream AICA nibble decoder

Original native composition: `include/cedar/aica_adpcm.hpp`,
`src/aica_adpcm.cpp`. This implements **only**
`VgmstreamAicaProfile::filtered_254_256`, not the unattenuated Flycast hardware
profile. There is no selectable unfiltered branch, emulator, AICA register/memory
map, CPU, DSP, BIOS, scheduler, mixing, sample-rate handling or playback.

## Exact permissive implementation provenance

Adapted narrowly from the authored math of `yamaha_aica_expand_nibble` and the
explicit nibble selection/state retention of `decode_aica` in official
vgmstream/vgmstream:

- Revision: `6802153425adde134e49394fc653455166d7d2f2`.
- [Source file](http<LOCAL_REFERENCE>/yamaha_decoder.c):11,787 bytes, SHA256
  `5f21dc3f9342547f94507e4847f26cbbc514cc0bc620646a9ae9ee727776b98a`.
- [COPYING](http<LOCAL_REFERENCE>/COPYING):1,314 bytes, SHA256
  `fcc26d35c4c4365e31f21734f846e4ee362ff0b57ba696085605a7b4768d8763`.
- Complete exact COPYING retained as
  `third_party/licenses/vgmstream-yamaha-ISC.txt`; it includes the original
  authors/portion attributions, permissive grant and warranty disclaimer.
- `clamp16` semantics were checked in the same pin's `src/util.h`. No overriding
  file license was present in the selected codec/helper. License was read first.

The native adaptation is modified: no streamfile/VGMSTREAM object dependency,
heap, file read, external utility dependency or stereo/channel-layout wrapper;
it validates caller storage/state and explicitly returns nibble progress. The
complete retained source notice governs the adapted standalone component; it
does not relabel other project files or grant any game-data rights.

`AICA-CODEC-EVIDENCE.md` separately records Flycast's GPL2-or-later reference and
the material feedback-filter difference. No Flycast implementation branch is
copied into these files and no GPL-derived expression is presented as ISC.
This work is source-informed, not a clean-room claim. External research links
mentioned in upstream comments were not opened or incorporated.

## Exact supported sample arithmetic

For a valid current PCM16 history h, step q in[127,24576] and nibble n:

1. Attenuate history as signed integer `h * 254 / 256`, truncating toward zero.
2. Compute magnitude `floor(q * (2*(n & 7)+1) / 8)` and cap it at32767.
3. Negate that magnitude when bit3 is set, add to attenuated history and clamp
   the sample to[-32768,32767]. This is the emitted PCM16 and next history.
4. Compute the next step as `floor(q * factor[n & 7] / 256)`, clamped to[127,24576].
   Factors are230,230,230,230,307,409,512,614.

The positive shift/divide operations are expressed as positive integer division.
No implementation-defined right shift of a negative predictor is used. Valid
state bounds prove intermediate products/sums fit signed32-bit arithmetic; no
overflow, floating-point rounding, dither or alternate filter is introduced.
These equations follow the selected source; no runtime sample comparison or
hardware-equivalence qualification has been performed.

## Explicit state and chunk continuity

Default state is deliberately `unspecified`, not an automatically admitted codec.
`reset_vgmstream_aica(state, filtered_254_256)` explicitly chooses history0 and
step127. That is a caller-requested starting state, not evidence that an unknown
voice/container starts there. `restore_vgmstream_aica` accepts an explicit valid
PCM16 predictor and step for a proven checkpoint. Invalid profile/state leaves
the previous state unchanged. Unlike the source's outer wrapper, invalid steps
are rejected rather than silently preclamped; valid seeded math is unchanged.

Maintain one independent state for each proven encoded channel. The decoder
outputs contiguous PCM16 for that one channel. It does not implement packed
stereo, planar/interleaved channel discovery or automatic predictor reset on
blocks/loops. Seeking requires replay or a valid state checkpoint; moving a byte
pointer alone cannot reconstruct predictive history.

`decode_vgmstream_aica` requires explicit low-then-high or high-then-low order,
first logical nibble phase0/1, encoded byte extent, encoded sample count and PCM
capacity. Phase means first/second under that order, not always low/high.

On success:

- `samples_written` is the actual PCM16 count.
- `bytes_read` includes a byte from which only one nibble was read.
- `bytes_advanced` is the number of complete encoded bytes that may be discarded.
- `next_nibble_phase` tells which logical nibble of the retained byte is next.

For example, one sample from phase0 reads one byte but advances zero complete
bytes and returns phase1. Keep that same byte for the next call. A call ending
mid-byte cannot replace its input with the next whole-byte chunk and silently
drop the unconsumed nibble. No hidden pending byte is stored or guessed. A zero-
sample call reads/writes no samples and retains phase/state.

## Bounds, atomic metadata and delivery limits

Whole-call validation precedes any output/state mutation. Limits are1,048,576
samples per call and64MiB declared input, not assertions about file format maxima.
The required encoded span, output sample capacity, pointer-address overflow,
PCM16 alignment, enums, phase, profile and state ranges are checked. Input,
written output and decoder state must not overlap; the entire declared input
is protected against output/state writes, including future unconsumed bytes.

Any validation error writes no PCM and leaves state unchanged; progress fields
are valid only on success. After validation, all16 nibbles are defined and the
bounded arithmetic has no data-dependent failure path, so state commits only
after producing the complete requested PCM span. The decoder allocates nothing
and performs no I/O. Callers retain valid source/output storage and synchronize
state access; no thread or device is created.

No source/asset reader currently proves that Shenmue STR uses this filtered
profile, nibble order, initial state, block reset, channel layout, rate or loop
rules. None is inferred from this codec. PCM production is not voice playback,
dialogue association, audio fidelity, continuous gameplay or a playable native
port. Container admission and actual audio-producer integration remain explicit
separate work.

No CMake/shared-source edits, compilation, tests, fixture/sample execution,
game/audio data reads, extraction or runtime occurred in this tranche. Lead owns
source review/compilation. No further codec or emulator component was started.
