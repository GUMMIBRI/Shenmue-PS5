# Producer-driven GPU frame ownership

`GpuFramePresenter` closes a concrete missing integration boundary: the existing
`Application`/`Ps5Session` path presents CPU `SurfaceView` images through SDL's
renderer, while native `GpuContext`, `SceneTarget` and `PostProcessor` separately
provide GPU acquisition, actual scene rendering and post passes. This adapter
connects the GPU facilities' real per-frame ownership without adding another
window, device, renderer, engine, input/audio subsystem or game source.

It does not automatically replace Application's CPU-surface hook. The lead must
connect an actual authorized engine's GPU output to this route and choose one
real presentation owner; do not silently run competing CPU/GPU presenters on the
same window/swapchain or label the new route playable without an engine.

## Required real producer

Construct with live borrowed `GpuContext` and `PostProcessor` references. The
context must already be initialized; the processor must have the actual target
format, real original-presentation callback and resolved native configuration.
Movie lifetime must already be bracketed with its actual nonzero serial by the
producer through PostProcessor. The adapter does not infer a movie from size or
create a substitute movie/scene/engine when one is absent.

`GpuFrameHooks::produce` is a required `noexcept` callback. Missing callback
returns unavailable before acquiring a frame. It receives the actual acquired
`GpuFrame` and returns `GpuProducedFrame` with a real `PostSource`, explicit
readiness and target viewport/load/clear policy.

The producer may record actual draws through `SceneTarget::record_scene` using
this exact frame's commands and return its successful source with
`earlier_in_same_commands` readiness. Or it may return a real external source
whose GPU completion was already established and declare `completed`.
Allocating/preparing a texture alone never establishes a valid image. No demo
draw list, camera, default triangle, fake engine or no-op-success producer is
provided. PostProcessor retains its real game/movie readiness/classification/
serial checks; this adapter does not invent a different game-only readiness gate.

The adapter pre-fills the target device/texture/format/width/height from the
actual swapchain acquisition. The producer may specify only viewport/load/clear
policy; replacing that target shape or ownership is rejected. Viewport must be
nonempty/in bounds. Load must explicitly be LOAD or CLEAR: the initial
DONT_CARE marker is rejected until the producer chooses. CLEAR channels must be
finite. A partial viewport with LOAD requires actual previously recorded/valid
outside pixels; no black bars or guessed background are manufactured here.

## Ordering and callback obligations

Each `present` call performs:

1. Actual `GpuContext::begin_frame` acquisition into the adapter's stable member.
2. Required real producer callback, then immutable frame/target checks.
3. Actual PostProcessor recording against that source and swapchain target.
4. Optional caller-native UI callback only after successful post recording with
   `presentation_recorded=true`.
5. Required command consumption: submit acquired swapchain, otherwise cancel
   only a no-swapchain command when SDL permits it.

Successful original fallback after an optional post-effect failure is a real
presentation and can receive native UI. Nested PostProcessor requested/effective
masks, fallback/latched/error details and pass counts remain in `GpuFrameRecord`.
A post failure without a presentation never calls native UI. A null UI callback
explicitly requests no native UI layer; no HUD detection or fabricated UI occurs.

All producer, original-post and UI callbacks must leave **every GPU pass closed
on every return, including failure**. They must not submit/cancel commands,
acquire another swapchain, call context shutdown/idle/lifecycle methods, reenter
the presenter, change its frame metadata, route an event loop, or destroy any
borrowed resource. UI records after post into the actual native target, not a
postprocessed intermediate. Do not retain frame handles for future commands.
Immutable metadata is checked after producer, post and UI callbacks, but such
checks cannot undo a forbidden raw SDL submission or repair a violated lifetime.

All calls use the context's owning GPU/main thread. Concurrent calls/destruction
are unsupported. Callback reentry is rejected without clobbering the active
outer call's diagnostics. The adapter owns no engine workers or GPU resources;
all actual source, post, UI and device resources remain alive through their
recorded GPU use. Returning from `present` is not a GPU completion fence.

## Error and swapchain release semantics

SDL forbids cancelling after swapchain acquisition. Therefore even a producer,
metadata, post or UI error after acquisition must finish with an actual submit.
That required release can present partially recorded or undefined target content;
the adapter preserves the original failure and does **not** call that a
successful frame merely because submission was accepted. No cleanup shader,
fake-success scene or invented fallback image is inserted to conceal the error.
The record exposes `release_after_failure`, submission attempt/acceptance and
separate release status/message alongside the original error.

An acquisition error that owns a command but no swapchain uses the SDL-permitted
cancel path. Legitimately non-drawable/minimized acquisition is distinguished:
GpuContext consumes its no-swapchain command internally, no producer/post/UI runs,
and the adapter reports `not_drawable`, not a rendered/presented frame.

Native submit/cancel failures can still consume their command references, as
documented by GpuContext. Never retry a consumed command or claim completion from
an error. If validation/thread failure prevents consumption, the stable member
and `frame_pending` flag remain. `finish_pending` performs one explicit retry of
the only legal release operation, returning that release status while preserving
the original primary failure in `record.status`. It records no producer/post/UI
work. No pending frame returns invalid state rather than pretending a retry ran.

Destructor is intentionally passive: it does not submit, cancel, auto-present,
force idle or destroy the context. Destroy only after `frame_pending()==false`
and outside callbacks. A pending frame requires retaining the presenter and its
borrowed context/resources for explicit recovery. Even after command consumption,
device teardown still belongs to its owner and must honor actual GPU idle and
all child-resource lifetime requirements.

## Result and delivery limits

`GpuFrameRecord` separates primary stage/status, producer/post/UI return statuses,
post details, submit/cancel outcomes and pending ownership. Uncalled stages retain
unavailable status. UI flags describe callback invocation/results, not observed
pixels. Submission accepted is not a completion/performance/playability claim.

Only `include/cedar/gpu_frame.hpp`, `src/gpu_frame.cpp` and this contract were
authored by this worker. The lead has selected the source for `cedar_post` batch
integration and owns compilation. This worker performed no runtime, frame
acquisition, command submission, GPU/render/fixture test, asset read, console
action or compilation. No engine-source permissions were expanded.
