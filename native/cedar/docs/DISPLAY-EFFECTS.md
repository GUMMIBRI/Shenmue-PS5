# Native final-image display effects

`DisplayEffects` records an actual SDL GPU fullscreen render pass with Off,
Scanlines and CRT modes. Off is the default and records no commands or resource
work. The caller still performs its actual original presentation when
`applied=false`; there is no alternate game image, replacement engine, guessed
HUD detection or synthetic output.

## Placement and explicit producer contract

Use the common `PostSource`/`PostTarget` descriptors from the actual producer.
Apply the effect **after the real game-image crop and any FSR/upscale pass, and
before caller-native UI/overlays**. The source crop is explicit in actual texture
pixels, and the destination viewport is explicit in physical target pixels.
`logical_rows` comes from the producer's original image geometry, not a guessed
240/480-line profile or the dimensions of upscaled artwork. Both full texture
dimensions and crop/viewport extents must be nonzero and no larger than 8192;
rectangles must lie wholly within their declared textures.

The source is caller-borrowed base-level 2D, single-sample, sampler-capable;
the target is a distinct caller-borrowed base-level 2D single-sample color target.
Both descriptors must declare the initialized GPU device, and the target format
must match the facility's configured pipeline format. SDL's opaque texture API
cannot independently authenticate descriptor dimensions/ownership: supplying
the actual resources/metadata is the producer's obligation. Same-texture
read/write feedback is rejected, without destroying either caller texture.

Active effects require explicit `PostColorEncoding::perceptual_sdr_unorm`.
That tag declares the producer's transfer/value interpretation, **not storage
bit depth**: non-sRGB RGBA8/BGRA8 UNORM and RGBA16_FLOAT are supported. In
particular the real FSR path can produce display-referred SDR values in an
RGBA16_FLOAT texture. No pow, linearization, gamma conversion, manual sRGB
encoding or color clipping is added. Source alpha is preserved. Unknown,
linear-SDR and linear-HDR tags and sRGB sampler/target formats are rejected
rather than silently applying a second transfer or unsupported HDR treatment.

Explicit game/movie source content is accepted when the caller chooses the
effect; caller-native UI content is unsupported. Baked-in game HUD pixels remain
part of the supplied game image. This facility neither detects/removes them nor
invents a separate native HUD. Render native UI afterward using the caller's
actual UI path.

## Restrained sampling and controls

Each output fragment takes one base-level sample. Its UV is clamped to the
source crop's first/last texel centers before normalization, so linear filtering
cannot gather adjacent pages or letterbox pixels. A one-pixel crop collapses to
that texel center. Sampling is explicitly nearest or linear; mip level is fixed
at zero in shader and sampler. There are no shifted/warped samples.

Scanlines attenuate the source-row phase with a smooth cosine pattern. Both
scanlines and CRT mask fade below two output pixels per original logical row;
there is no hard single-pixel row removal. CRT adds a restrained RGB phosphor
pattern aligned to the physical destination viewport. Bounds are checked before
resource creation/recording and reinforced in the shader:

- Scanline attenuation: 0–0.25, default 0.08.
- CRT mask attenuation: 0–0.15, default 0.055.
- Each caller mask-color channel weight: 0–1, default 1.
- Actual logical image rows: 1–8192 for active effects.

NaN/infinite/out-of-range controls are rejected. Settings do not brighten or
invent source detail; mask color weights control channel attenuation. There is
no temporal history, curvature, warp, overscan, bloom, motion reconstruction,
art replacement, game-specific camera or lighting behavior.

## Recording and lifetime

`initialize(device,target_format)` binds an explicit borrowed device/format
without allocating GPU resources. A pipeline and only the chosen sampler are
created lazily for the first active effect. Shader and pipeline allocation
errors preserve a diagnostic string and return `applied=false`; caller resources
are never released or replaced. Intermediate owned shader objects are released
on success/failure. Existing partial pipeline/sampler resources remain owned and
can be retried or explicitly released by `shutdown`.

`record(commands,source,target,settings,applied)` requires an actual command
buffer with no active pass. It validates all descriptors/controls before GPU
recording, prepares resources, then begins one color render pass, sets the
explicit viewport/scissor, binds pipeline/source sampler, pushes the 64-byte
fragment uniform block and records a fullscreen triangle. It ends its pass and
never submits, cancels, swaps, closes a window or routes UI. Target load is
explicit LOAD or CLEAR and store is STORE; default LOAD preserves pixels outside
the game viewport. An explicit CLEAR clears the whole target according to SDL's
load operation, with finite SDR 0–1 clear channels.

Off returns success with `applied=false`, including before initialization. Active
unsupported/error returns leave `applied=false`: the application should record
its exact existing presentation for that frame, not manufacture success or
substitute blank output. Resource failures happen before a caller pass begins;
a failed native pass begin cannot prove the device/target had no side effects.
The facility never destroys the input. Once a pass exists, SDL bind/push/draw/
end calls return void: `applied=true` means commands recorded, not GPU execution
verified. The caller owns device fault handling and actual submission.

All operations and destruction remain on the owning main/GPU thread. Device and
borrowed source/target resources outlive all recorded use. `shutdown` releases
only owned pipelines/samplers through SDL's deferred-safe APIs; it does not
establish device idle. The device owner must resolve pending work before device
teardown and release this facility before destroying the device. No background
jobs, heap frame history or asynchronous callbacks are created.

## Shader-builder contract

The lead owns the common fullscreen vertex shader and generated header:
`cedar_post_shaders.hpp`, namespace `cedar::shaders`, SPIR-V byte arrays
`post_vert` and `display_filter_frag`, with `sizeof` available for each array.
Both entry points are `main`. The vertex stage uses `gl_VertexIndex`, no vertex
attributes and no uniform resources. Fragment binding layout is:

| Binding | Contract |
| --- | --- |
| set 2, binding 0 | `sampler2D source_image`, SDL fragment sampler slot 0 |
| set 3, binding 0 | std140 four vec4 / 64-byte block, SDL fragment UBO slot 0 |

The vec4 order is source crop `(x,y,w,h)`, destination viewport `(x,y,w,h)`,
effect `(logical_rows, mode, scan_strength, mask_strength)`, mask color
`(r,g,b,0)`. Mode 1 is Scanlines and 2 is CRT. Fragment color location is 0.
No separate fullscreen vertex/header was authored by this worker.

## Provenance and delivery boundary

The facility was written after inspecting Cinder's read-only reference at
`ps5-native/gpu-platform-20261030/attempt-app-data-build18-20261106/psyz-src/psyz/src/platform/display_filter`.
Those reference files individually carry MIT markers, but the actual containing
`psyz-src/LICENSE` assigns `psyz/src/platform/` MPL 2.0 and distinguishes
unlicensed other areas. This work conservatively carries **MPL-2.0** with
truthful source-informed attribution, rather than presenting the whole platform
as MIT or asserting clean-room independence. Retain the full MPL-2.0 text with
the project's third-party notices. No unlicensed PsyZ include/psyq code, PE game
logic, title hooks, private art, shader binary or other game asset was copied.

Only `include/cedar/display_effects.hpp`, `src/display_effects.cpp`,
`shaders/display_filter.frag.glsl` and this contract were added. No root CMake,
other shader, source, engine, helper, Qwen or shared dependency was changed.
The lead owns shader generation, target integration and compilation. This
worker performed no compilation, GPU upload/render, runtime test, asset read,
game launch, console action or visual/performance qualification.
