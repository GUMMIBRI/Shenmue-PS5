# Native offscreen scene producer

Original facility: `include/cedar/scene_target.hpp`, `src/scene_target.cpp`.
Uses existing public SDL GPU APIs and the lead's real `GpuRenderer` drawing path.
No game source, assets, renderer internals or third-party implementation is copied.

`SceneTarget::prepare(device,config)` allocates a real offscreen color attachment
with both SAMPLER and COLOR_TARGET usage, after querying actual format support.
It accepts RGBA8/BGRA8 UNORM and RGBA16_FLOAT storage with known producer-specified
perceptual SDR, linear SDR or linear HDR encoding. Linear HDR requires float16;
no texture format or resolution is used to guess transfer or scene identity.
There is no gamma or color-space conversion in this class.

Preparing a target publishes no `PostSource`. Its native texture contents are
undefined until real drawing, and no public texture getter exposes that allocation
as an apparently valid scene. The prepared/config/byte getters describe allocation
only. Repeating the same size/storage avoids reallocation; a changed transfer tag
is only a producer choice for the next genuine frame recording.

`record_scene(commands,renderer,draws,count,clear,content,serial,opaque,source)`
requires explicitly game content, a nonzero actual frame serial and a nonempty
draw list. Movies and native UI have separate producer paths. Device and renderer
color format must match. The method invokes `GpuRenderer::record_to_texture` at
the chosen extent, using the real uploaded meshes, textures and caller camera
matrices. Every renderer error propagates and leaves the result empty. Only a
successful real recording publishes a bounded full-target `PostSource`.

The published source is ordered earlier in **that same command stream**. It is
not submitted or fence-completed. Consumers such as FSR must record afterward in
that stream before its owner submits/cancels it. The frame serial belongs to the
caller; it is not generated from resolution, assumed monotonic or proof of GPU
completion. The explicit opacity assertion is retained exactly, including false;
this class does not infer alpha from clear color or fabricate an RGB-only gate.

This is the genuine before-post-processing hook: geometry is drawn into an
offscreen target of the selected dimensions. It does not resize an already
presented image and label that higher geometry resolution. The actual engine
still owns render-scale selection, camera/projection, draw generation, render
order, material behavior and integration. No engine binding, observed fidelity,
frame-rate benefit or complete Shenmue renderer is claimed.

## Bounds and lifetime

Dimensions are capped8192 each and the logical color allocation is capped128 MiB.
For example7680x4320 RGBA8 fits that color budget;8192x8192 does not. Float16 uses
eight bytes per pixel. Format capability/allocation can impose tighter hardware
limits. The renderer's own draw/depth bounds also apply; a valid color allocation
does not guarantee an8K depth-enabled render will be accepted or performant.

The128 MiB budget is **per logical color texture**, not a hardware memory or total
driver-pool measurement. Resize allocates a replacement first and preserves the
previous target on failure, then releases the old one through SDL deferred
disposal. Old/new allocations and pending driver use may overlap temporarily.
No aggregate pending allocation budget or VRAM availability is invented.

The device is borrowed and must outlive target shutdown/destruction and every
recorded resource use. All methods/destruction belong to the owning main/GPU
thread. Before replacement or shutdown, callers must finish encoding every old
source consumer and account for producing/consuming command buffers through
submission or cancellation. Released texture handles must not be referenced by
new commands. SDL handles safe pending GPU disposal; this class never waits idle,
destroys the device, owns a window, presents, submits or cancels foreign commands.

The caller owns result objects and all provided storage. Source/draw/config/result
objects must be valid and distinct while used. On record failure, do not reuse an
old result as though a new frame exists. A partial native renderer error may
require the command owner to discard/recover its buffer; no blank-output fallback.

No runtime, device calls, fixture/game tests, asset decoding, console actions or
image output were performed in this worker task. Lead owns coherent compilation,
linkage and actual render-scale/FSR pipeline integration.

Lit rendering now exposes actual output-transfer metadata. SceneTarget rejects a
lit renderer whose output transfer differs from its configured PostSource tag,
before recording/publishing any scene. This prevents linear lighting output from
being mislabeled perceptual SDR for FSR. Unlit producer assertions stay explicit.
