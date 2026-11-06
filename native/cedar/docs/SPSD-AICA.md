# SPSD AICA to interleaved PCM16 producer

`SpsdAicaStream` consumes actual bounded SPSD channel bytes through
`decode_vgmstream_aica` and produces caller-owned interleaved signed PCM16 sample
elements. This is a real resource-to-PCM boundary, not playback, voice/actor
association, resampling, mixing, a file loader or game engine. No PCM waveform or
silent successful substitute is generated without decoding the supplied source.

## Proven profile and initial state

Start accepts only a successfully parsed `SpsdView` whose actual codec is 3 and
profile is `vgmstream_filtered_low_first_per_channel`. It rejects the PCM codecs
and unsupported/unspecified profiles instead of guessing how to decode them.
The exact official vgmstream pin and permissive full notices remain those in
`SPSD-SCOPE.md` and `third_party/licenses/vgmstream-yamaha-ISC.txt` (SHA256
`fcc26d35c4c4365e31f21734f846e4ee362ff0b57ba696085605a7b4768d8763`).

Same-pin source proof is complete: SPSD selects `coding_AICA_int`, the official
dispatcher chooses per-channel/non-byte-nibble-stereo decoding and low-first
from zero codec_config, the targeted allocator zero-initializes channel state,
and SPSD sets each initial step to 0x7f. Accordingly each channel starts with
history **0**, step **127**, and the existing explicit filtered254/256 profile.
This is vgmstream compatibility, not the different unfiltered hardware model or
a bit-exact Shenmue console/audio-output claim.

The stream keeps a copy of the validated view plus two fixed predictor states.
The original view object need not survive start, but its entire actual byte
buffer remains caller-owned, immutable and alive through stream use. No bytes
or decoder objects are heap-allocated. Failed start discards the previous stream.
Serialize start/decode/rewind/stop; there is no worker or asynchronous callback.

## Actual bounded decode

`decode_frames` requests an **exact** frame count, at most 1,048,576 per call.
A frame has one sample for mono or two interleaved channel samples for stereo.
Output capacity is in total int16 sample elements; scratch capacity is mono
int16 elements, not bytes or stereo frames. Nonzero requests need at least one
scratch element. A larger scratch permits larger bounded chunks; a scratch
size around one 0x2000-byte block's 16,384 samples avoids one-sample overhead.

First the entire requested range is walked without writes. Output/scratch size
multiplications, address overflow, alignment, capacities and full-span overlap
against each other, the immutable complete source and stream object are checked.
Every channel's actual contiguous slice for every planned chunk is validated
before the first PCM or scratch write. Source data/size accessors added to
`SpsdView` exist for these real borrowed-span checks, not to mutate the resource.

The second pass obtains those actual physical channel slices, decodes to the
single caller scratch buffer with copied candidate predictor states, and places
each mono sample into the corresponding output channel slot. Chunk size is
limited by scratch, the codec sample bound and the current physical channel
block span. This handles mono, planar stereo, full 0x2000 channel-block groups
and short equal-sized tails without claiming whole-channel physical contiguity.

The absolute per-channel frame cursor determines byte offset and first/second
nibble phase. After an odd number of samples, the original immutable byte is
still available for its high nibble on the next call; the stream does not discard
that byte or start a new low nibble accidentally. Predictor history continues
across slices, calls and interleave boundaries. No unproven per-block reset is
inserted. Decoder return counts/phase/byte advancement are checked before copying
each successful mono chunk to interleaved output.

Only complete success commits both channel states and the frame cursor. No
source/table parsing or rate guessing happens during decoding. `sample_rate`,
`channels`, `total_frames` and `frames_remaining` report actual source metadata
and decode-once progress. Total frames are two encoded nibbles per channel byte.
PCM16 values are numeric int16 elements in memory, not a serialized WAV/file.

## Failure, EOF and ownership

Normal preflight failures modify neither output nor scratch and consume no
frames. Unexpected second-pass source/codec/contract failure can occur after
earlier chunks/channel slots were written; it leaves caller-visible predictor
states/cursor unchanged and reports `output_valid=false`. Discard the whole
output from that failed call before retrying.

`output_samples_written` is a count of actual scattered interleaved sample
assignments, **not a contiguous byte prefix**. `completed_prefix_frames` counts
only prior chunks whose every channel was written. `frames_written` is nonzero
only on complete successful calls. `output_modified` and `scratch_modified`
distinguish the two caller buffers, while nested codec/source results and the
failing channel/absolute frame preserve diagnostics. Even a reported completed
prefix on failure is not permission to advance stream state: no partial commit
or rollback of already-written caller memory is pretended.

Requesting more frames than remain returns `insufficient_frames` before writes;
there is no implicit short-read, zero-padding, repeated audio or loop. An exact
zero-frame call is permitted, produces valid zero-sample output without writes
or state change, and can describe EOF. A structurally empty source similarly
contains no generated PCM. Explicit `rewind` alone restores the proven initial
0/127 channel states and cursor; raw source loop metadata is never applied.
Arbitrary seek/loop checkpoints are unsupported rather than reconstructed by
moving a byte pointer.

`stop`/destruction ends the borrow; it never frees or modifies caller data.
Buffers must remain valid through each synchronous call. There is no device
submission or GPU/audio completion lifetime. Actual PS5 output remains 48 kHz
under the existing output facility, whereas this producer reports the original
SPSD rate: a future real conversion/output bridge must handle differences.
No resampling, float conversion, sound start, voice routing or audible playback
is claimed by this PCM producer.

## Delivery boundary

Only `include/cedar/spsd_aica.hpp`, `src/spsd_aica.cpp`, this contract and the two
small const source-span accessors in `spsd.hpp` were authored. The lead owns
integration, source review and compilation. This worker performed no runtime/
fixture test, codec execution, audio-content/file read, playback, GPU or console
action. The shared account check during this batch was 46% used, with no reset
or purchase performed.
