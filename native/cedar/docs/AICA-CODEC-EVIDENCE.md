# Bounded AICA codec evidence and preferred native boundary

Scope: standalone encoded-nibble to PCM16 math only. Read current project prompt
and AGENTS, then actual repository licenses/file notices before implementation
selection. No CPU, guest memory map, DSP, BIOS, scheduler, mixing/envelopes or
whole emulator is transplanted. No game/audio payload was read or executed.

## Flycast hardware-reference profile

Primary repository flyinghead/flycast exact pin
`7adbf3caef251b153e348df3f469a0f35878339d`.
[Root LICENSE](http<LOCAL_REFERENCE>/LICENSE)
is GPLv2 text. The file-level notice in
[`core/hw/aica/sgc_if.cpp`](http<LOCAL_REFERENCE>/sgc_if.cpp)
explicitly permits GPL version2 or later and separately attributes some PLFO/FEG
code. Only codec/reset/history/nibble selection was relevant; no such PLFO/FEG
logic is selected for reuse. File length38,709 bytes, SHA256
`5b78b05dbd29d8c41d1e610bd4d6035625627cb48722275da84f2c8322ebc5e2`.

For a nibble n, previous PCM history h and quantization step q, its codec uses:

- Magnitude `m = n & 7`, negative sign from bit3.
- Delta magnitude `min(floor(q * (2*m+1) / 8), 32767)`.
- Add signed delta to h; clamp the resulting sample to[-32768,32767].
- Update q with magnitude factors230,230,230,230,307,409,512,614 divided by256
  using positive integer truncation; clamp q to[127,24576].
- Store that clamped PCM result as history. There is **no feedback attenuation**.
- Hardware channel reset chooses history0 and step127. Packed sample addresses
  select the low nibble for even sample indices, high nibble for odd indices.

Normal ADPCM channel mode snapshots predictor/step at loop start and restores
them when repeating; the long-stream mode keeps adapting rather than restoring
that snapshot. Those hardware loop/mode rules are evidence, not native voice
container semantics or a reason to transplant channel/scheduler logic.

## One permissive standalone comparison: vgmstream

Primary official vgmstream/vgmstream exact pin
`6802153425adde134e49394fc653455166d7d2f2`.
[COPYING](http<LOCAL_REFERENCE>/COPYING)
grants use/copy/modify/distribute with or without fee while retaining its notices
(ISC-style permissive grant). Exact COPYING length1,314 bytes, SHA256
`fcc26d35c4c4365e31f21734f846e4ee362ff0b57ba696085605a7b4768d8763`.

The codec file
[`src/coding/yamaha_decoder.c`](http<LOCAL_REFERENCE>/yamaha_decoder.c)
has no overriding file-level license. Exact length11,787 bytes, SHA256
`5f21dc3f9342547f94507e4847f26cbbc514cc0bc620646a9ae9ee727776b98a`.
Relevant authored functions are `yamaha_aica_expand_nibble` and `decode_aica`,
with `clamp16` verified in the same pin's `src/util.h`. Comments credit other
format research, including a different game's decompilation; none of those
external references or recovered bodies were opened/copied. This is explicitly
source-informed work, not a clean-room assertion.

Its AICA helper uses the same bounded delta/step equations but first attenuates
history by **254/256**, with signed integer division toward zero. It then adds
the signed, capped delta, clamps PCM16, stores that sample and updates the step.
This materially differs from the Flycast no-attenuation profile. The source's
own comment discusses uncertainty about that filter: do not merge the variants
or advertise bit-exact hardware equivalence.

vgmstream's outer function receives history/step externally, preclamps step,
supports explicit low-first/high-first input and preserves per-channel history
after a call. Its mono layout has consecutive nibbles; its explicit stereo mode
selects a channel nibble from each byte. Those are options of that API, not proof
that Shenmue STR uses either layout. Arbitrary seeking still requires predictor/
step reconstruction or a valid checkpoint, not merely moving a byte pointer.

## Selected finite component

Implement **only the permissive filtered-vgmstream profile**, with truthful
naming, explicit nibble order/phase, caller buffers and per-channel predictor/
step state. Do not include an unfiltered/Flycast code branch or relabel GPL-
derived expression as ISC. Retain the complete vgmstream COPYING as
`third_party/licenses/vgmstream-yamaha-ISC.txt`; no file from Flycast is copied.

That component can decode caller-selected encoded bytes to PCM16 once the caller
has actual codec/profile, ordering and seed evidence. It does not establish STR
header/layout, block reset rules, voice channel association, sample rate, loop
format, resampling, playback or original-game integration. These remain explicit
missing container/producer inputs. No further codec/source survey is started.
