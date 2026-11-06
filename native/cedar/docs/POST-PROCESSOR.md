# End-to-end native post-processing composition

Original `post_processor.hpp` / `post_processor.cpp` compose the actual existing
`Fsr1`, `DisplayEffects` and `MovieEffects` facilities against the shared
`PostSource`/`PostTarget` producer contract. Algorithm and shader provenance stays
with those dependencies; no shaders, engine game code or baseline substitutes
are copied into this orchestration layer.

## Required actual baseline

Initialize with a live borrowed device, actual target format, and mandatory
`Status(void*, commands, const PostSource&, const PostTarget&)` callback. The
callback must really record the supplied source into the supplied viewport,
honor the target's load/clear contract, not throw, and return success only after
recording its actual original presentation. It must leave no active GPU pass on
return, including failure. It must not submit, cancel or draw
native UI. A missing callback is an initialization error. This facility cannot
prove a callback's implementation, so providing a no-op success callback violates
the caller contract; no such fallback is supplied here.

Initialization borrows metadata only. Defaults have FSR disabled, display Off,
movie cleanup Off and backdrop disabled. That route calls the original callback
with the actual original source and target, creates no post textures/pipelines,
and does not recompute existing engine/movie aspect policy. Existing superior
engine effects remain the caller's original route unless explicit native settings
and ownership have been resolved before configuration.

Record requires real distinct same-device source/target handles, exact producer
crop/encoding/opacity and target viewport. Native UI classification is explicitly
rejected without effect or callback recording. The caller draws its native UI
after this facility's game/movie presentation; no UI filtering route is invented.
All calls/destruction belong to the creating main thread, outside active GPU
passes. Producers retain actual resource/readiness responsibility.

## Game order and failure handling

Requested game FSR records into its own compact/intermediate resources first.
Only a fully successful child record replaces the current valid source. A failed
or unsupported FSR request immediately calls actual baseline on the original
source, with partial child pass counts preserved. An incomplete EASU/RCAS result
is never sampled or advertised as applied.

Requested display filtering then records into the caller target. If it fails or
is unsupported, baseline receives the last fully valid source: successful FSR
output when available, otherwise original source. Off display also uses that
baseline path to present successful FSR output. Neither mode pretends that GPU
submission or completion has occurred.

## Explicit movie order

Producer must call `begin_movie(nonzeroSerial)` and matching `end_movie(serial)`.
Movie record requires explicit `completed` or `earlier_in_same_commands` source
readiness; not-ready sources are not sampled even by fallback. No dimensions,
filename, resolution, timer or renderer state classifies a movie.

The active movie graph supports explicitly perceptual SDR R8/B8 UNORM inputs.
Other encoding/storage combinations use actual original fallback with an explicit
unsupported diagnostic; unknown/HDR data is not silently converted to R8.
Producer supplies the actual movie pixel aspect in `MovieEffectsConfig`; the
default value is not a heuristic assertion that arbitrary movies have square
pixels. Logical display rows likewise remain explicit caller data.

1. Copy the exact producer crop with real nearest `SDL_BlitGPUTexture` into
   owned compact R8 storage. No raw atlas/VRAM gather can bleed outside that
   crop. Copy preserves the actual producer's opaque assertion and transfer data.
2. If requested, record real movie cleanup to a second compact R8 target at the
   original crop extent. Internal cleanup presentation uses pixel aspect 1
   specifically to keep the stored raster positions unchanged. The upstream
   producer's real pixel aspect is still retained for final presentation.
3. If requested, record FSR against that last valid compact source. Output
   dimensions preserve the **exact raw raster ratio**: reduce width/height by
   their GCD, then choose the largest integer multiple of those units fitting
   the planned aspect-correct center. If either axis would downscale, bypass
   FSR explicitly and baseline the last valid source. Original movie pixel
   aspect remains unchanged after successful FSR; it is never forced to 1.
4. Record optional real two-pass blurred backdrop and aspect-correct foreground
   through `MovieEffects` into a compact R8 presentation target with the exact
   caller viewport extent. Cleanup is Off for this stage, preventing duplicate
   cleanup. Half-texel sample clamps are supplied by the existing movie facility.
5. Apply requested display effects to that completed composition, or call real
   baseline with the composed source. If composition failed, do not display-
   filter an uncomposed movie; fallback uses the last valid compact/FSR source
   with its producer-PAR-correct center viewport. When no intermediate is valid,
   original source/target baseline remains unchanged.

Opacity is copied from actual producer metadata throughout; dimensions do not
establish it. FSR's RGB-only opaque requirement remains enforced by the child.
Movie serials bracket resource lifetime; no decoding/playback or source fence is
fabricated. Internally produced sources use actual earlier-in-this-command-stream
readiness after their commands have been recorded.

## Truthful diagnostics and failure latches

Each result distinguishes requested/effective effect masks, latched requested
features, first effect status/message, baseline called/succeeded/fallback, and
whether final caller-target presentation was actually recorded. Effective flags
describe fully recorded valid intermediate/effect routes, not proof that final
presentation or GPU execution completed. A failed final callback still returns
failure even if earlier effect passes succeeded.

`passes_recorded` retains child-reported shader passes, including partial failure,
plus a successful display pass. `copies_recorded` counts this graph's explicit
crop blits only. FSR's internal crop-copy count and callback-internal command/pass
counts are not exposed by their APIs and are deliberately not invented. No count
is an FPS, submission, fence or visual qualification claim.

An effect failure followed by successful baseline returns overall success for
the recorded fallback presentation, while keeping effect error/effective masks
truthful. Baseline failure cannot produce overall success. `last_valid_source`
is diagnostic borrowed metadata, not an independently retained output lease;
do not reuse it after resize, later records, movie end or shutdown. Caller input
objects are const and never cleared, retired or relabeled by an effect failure.

Native resource failures latch their facility; movie target allocation/budget
failure latches target preparation. Repeating the same frame/configuration does
not allocate endlessly. A genuine configuration change or explicit
`reset_failure()` permits retry; repeatedly submitting identical configuration
does not erase latches. Unsupported source contracts remain explicit per-frame
validation failures, rather than being falsely reported as completed effects.
The original latched status/message is copied and retained, including across
later records and movie retirement; it is not replaced by a generic unavailable
message that erases the actual resource failure.

## Memory and native ownership limits

This graph's currently owned compact-crop, optional compact-cleanup and compact-
presentation R8 targets total at most64MiB logical pixel storage. Old logical
targets are released before replacements are allocated. SDL can defer actual
destruction of in-flight physical versions; this is not a total VRAM bound.
FSR's separate160MiB and MovieEffects blur's separate64MiB implementation budgets
remain additional, as do driver allocations/pipelines/swapchain resources.

Caller source and target must not alias graph-owned intermediates. Native
resources are lazily allocated only for active routes and disposed through SDL's
deferred-safe release APIs. Device and actual GPU use outlive every component.
The facility owns no command submission/cancellation, swapchain, window, engine
boot, source decoder, fence, timer, native UI or asset files.

Only source/API review was performed. No CMake/shared-source changes, compilation,
tests, GPU/window calls, game execution or asset reads/copies occurred during this
worker tranche. Lead owns integration/compilation and the human owns runtime
qualification.
