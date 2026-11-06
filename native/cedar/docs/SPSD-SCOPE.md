# SPSD/STR borrowed resource view

`SpsdView` parses the bounded in-memory SPSD header and exposes actual borrowed
per-channel encoded-byte slices. No file I/O, audio decoding/playback, sample-rate
conversion, loop execution, allocation, game audio asset read or original-game
voice association occurs. It accepts bytes, not a filename-based format guess.

## Proven primary and license

The official pinned primary is
[`src/meta/spsd.c`](http<LOCAL_REFERENCE>/spsd.c)
at vgmstream revision `6802153425adde134e49394fc653455166d7d2f2`.
Actual source length is 3,878 bytes, SHA256
`b323736ae2e974ca6794c03a6ff3a901adc116b17b60a0d1166ab0b2f481c22b`.
The file has no overriding license notice. Root
[`COPYING`](http<LOCAL_REFERENCE>/COPYING)
was read first and grants permissive use/modification/distribution with notices.
Full authorship/grant/disclaimer is retained unchanged in
`third_party/licenses/vgmstream-yamaha-ISC.txt`, 1,314 bytes, SHA256
`fcc26d35c4c4365e31f21734f846e4ee362ff0b57ba696085605a7b4768d8763`.
The new source/header carry ISC provenance accordingly. No unrelated GPL or
restricted Shenmue implementation is copied; no clean-room claim is made.

That primary explicitly identifies `.str` as a Shenmue Dreamcast extension.
This establishes a concrete supported container profile, not equivalence to
uninspected PC archive members or permission to redistribute owned game data.

## Supported bounded header

The view requires `SPSD`, a complete 0x40-byte header and big-endian version
0x01010004 or 0x00010004. It reads codec at 0x08, raw flags at 0x09, little-endian
layout index at 0x0a, declared payload bytes at 0x0c, nonzero little-endian sample
rate at 0x2a, and the exact uninterpreted loop word at 0x2c. Flags bits 0/1 select
two channels when either is set; otherwise one. Raw flags and raw loop bit 0x80
are preserved without inventing meanings for other bits.

Codecs are PCM16LE (0), PCM8 (1), and AICA_int (3). The complete declared payload
starts at 0x40 and must actually fit the borrowed buffer. Total bytes must divide
equally across channels; PCM16LE also requires complete two-byte samples per
channel. Incomplete per-channel padding/sample bytes are rejected rather than
silently discarded. Declared zero-byte payloads are represented as structurally
empty views, never as generated audio or successful playback.

The primary deliberately does not require exact file-length equality because
trailing padding varies. This view likewise excludes all bytes after declared
payload from channel slices and reports them as `trailing_bytes`. It neither
checks them as zero padding nor guesses that they are additional samples.

## Actual channel layouts and slices

| Raw index | Supported physical layout |
| --- | --- |
| 0 | Mono only, contiguous declared payload |
| 0x0d | 0x2000-byte channel blocks, complete channel groups followed by equal-sized short per-channel tail blocks |
| 0xff | One contiguous declared payload segment per channel, each data_size/channels bytes |

`channel_slice` takes a channel number and logical **encoded-byte** offset/length
within that channel. It returns a borrowed physical pointer, byte length and
file-relative offset. It never pretends stereo 0x0d data is a contiguous whole
channel: ranges crossing physical channel-block boundaries return
`noncontiguous_range`, with the maximum contiguous bytes available at that
position. The caller may request that exact smaller slice and continue
explicitly. A short final group is bounded using the declared equal per-channel
tail size. Mono block-layout bytes are genuinely physically contiguous.

Raw slices may split PCM16 words or select a later ADPCM byte; they are byte
inspection/access, not independently decodable samples or predictor checkpoints.
Zero-length requests at logical channel end use a bounded payload-end marker
and return no audio bytes. Out-of-range channels/offsets never expose padding or
another channel's bytes.

## Source-backed AICA compatibility, not hardware claims

One targeted official dispatcher,
[`src/base/decode.c`](http<LOCAL_REFERENCE>/decode.c),
routes `coding_AICA_int` to `decode_aica` with stereo byte/nibble mode false;
high-first depends on `codec_config==1`. The additionally authorized targeted
[`allocate_vgmstream` function](http<LOCAL_REFERENCE>/vgmstream.c)
allocates the structure using calloc, and this SPSD parser never sets that
configuration field. The resulting SPSD compatibility route is per-channel,
low-nibble-first. The already documented same-pin Yamaha decoder supplies the
**filtered-vgmstream** AICA profile, not the different unfiltered hardware
reference described in `AICA-CODEC-EVIDENCE.md`.

For codec 3, `aica_profile` records that proven compatibility label. This module
does not invoke a decoder, initialize predictor/step state, apply block resets,
or derive arbitrary seek/loop checkpoints from byte offsets. Raw loop flag/word
remain raw: the primary's adjusted loop sample calculation is not turned into
automatic native playback here. Do not equate source compatibility with verified
bit-exact console hardware or tested Shenmue audio output.

## Lifetime, bounds and delivery

Retain the complete input buffer immutable/alive throughout view and slice use.
Address overflow and overlap with the view object are rejected; reads use
explicit endian assembly, not unaligned struct casts. A failed parse clears the
prior view/metadata. Slice outputs clear on ordinary failure after alias checks;
output aliases with the view or any input-buffer span are rejected without
writing/corrupting that storage. Slice mapping uses bounded uint64 arithmetic
before conversion back to the already-validated actual buffer span.

Only `include/cedar/spsd.hpp`, `src/spsd.cpp` and this contract were authored.
The lead owns CMake/source review and compilation. This worker performed no
compilation, runtime/fixture test, game audio/content read, file operation on
game data, GPU/console action or playback. Targeted official source/license
inspection and provenance hashing are separate from gameplay implementation.
