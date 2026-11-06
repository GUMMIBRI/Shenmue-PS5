# Explicit bounded render/aspect planning

`plan_render_layout` is a CPU-only request planner over the existing
`GraphicsSettings` and actual `PostSource` descriptors. It allocates nothing and
calls no SDL/GPU/window/event/asset API. It does not resize a texture, change a
camera, reproject geometry, rotate source pixels, enable an effect, mutate an
engine setting or claim any frame-rate improvement.

## Inputs and aspect meaning

Supply actual native output dimensions, actual source texture dimensions and
the producer's **visible crop**, positive finite source pixel aspect, source
orientation, and an explicit per-image pixel budget. Aspect uses crop width and
height, not arbitrary whole-page dimensions or heuristics. Pixel aspect is
physical source-pixel width divided by height; zero is deliberately invalid
rather than silently assuming square pixels. Orientation must be one of the
declared identity/90/180/270-degree values. A quarter turn reciprocates the
computed display aspect; a half turn preserves it.

The game settings use the actual existing enum values:

| `GraphicsAspect` | Planned gameplay viewport |
| --- | --- |
| `preserve_engine` | Exact caller-supplied valid `engine_viewport`, with no guessed 4:3 or inherited PS1 policy |
| `four_three` | Largest centered fit with target aspect 4:3 |
| `sixteen_nine` | Largest centered fit with target aspect 16:9 |
| `fit` + `preserve_source_aspect` | Largest centered fit using actual crop/pixel aspect/orientation |
| `fit` + explicit `stretch_game_to_output` | Full native output rectangle; only this policy sets `gameplay_stretched` |

The schema's `fit` value does not itself establish stretching. A separate
`LayoutFitBehavior` makes that choice explicit. Fitted dimensions round to
nearest integer, ties upward, while the constraining axis fills the output.
Fixed-aspect labels therefore have at most ordinary raster integer-rounding
error; dimensions are not quantized to large rational multiples that discard
otherwise usable pixels. Center offsets use integer division; an odd remainder
goes to right/bottom. An unrepresentable aspect that would round to a zero
extent fails instead of being clamped to a fabricated one-pixel viewport.

The returned orientation is a request/description only.
`orientation_transform_required` says the actual producer/render pipeline must
perform a nonidentity transform before treating pixels as oriented. The planner
does not claim that transformation happened or silently rotate any crop bytes.

## Movie and native UI rules

Movies always use a source-aspect-preserving fit within native output,
regardless of gameplay fixed-aspect/stretch controls. `movie_aspect_preserved`
records that policy. Their reported render extent is the actual decoded crop;
there is no game-internal render-scale request for a movie. This does not detect
content or crop baked bars: `FrameContent::movie` and the true visible crop must
come from the actual producer.

Native UI always receives the full native-resolution viewport and output-sized
extent, with `bypass_post=true`, no scale request and no FSR eligibility. It does
not inherit gameplay/movie effects, crop or orientation. The planner still
validates settings, output/budget and supplied scalar/enum inputs, but does not
require an image texture/crop for directly rendered native UI. UI orientation
in the returned plan is identity. No game-HUD guessing or UI rendering occurs.

## Render-scale request, not a claimed resolution change

Game content supplies an explicit **unscaled engine baseline extent**. This
avoids repeatedly scaling an already-upscaled current source crop or deriving
resolution from unrelated texture-page storage. With preserve-engine scale,
that extent is reported unchanged and `engine_extent_requested=false`.
With native scale selected, 50–200 percent is applied independently to each
baseline axis using uint64 integer multiplication and nearest/ties-up rounding.
Only then is `engine_extent_requested=true` set.

The baseline is not secretly chosen from native output, a reference game's
resolution or current camera. The owning renderer chooses the genuine baseline
and must consume the extent request to create/resize its real offscreen target.
The plan does not prove that the current `PostSource` was rerendered at that size;
FSR eligibility below continues using the actual current crop. Always inspect
`engine_extent_requested` before treating the returned extent as a request to
change an engine. For movies/UI that flag remains false.

## Bounds and failure behavior

Each output/source/baseline/planned render axis is bounded by 8192 where used.
Output and planned render extents must fit the explicit pixel budget, which
must be in 1–8192². This is a **per-image pixel cap**, not an aggregate byte or
total GPU-memory guarantee; format, buffering, depth and other allocations
remain the caller's budgeting responsibility. No quality is silently reduced
to fit a limit. A 200-percent request exceeding an axis/pixel cap fails rather
than quietly becoming a smaller scale.

All settings pass the existing `validate_graphics_settings` schema, including
latent tuning bounds. Invalid settings retain that nested status. Source crop
ranges use subtraction-based bounds checks, products use uint64, aspect math
uses checked finite doubles, and casts occur only after results are proven
nonzero/in range. On any error, there is no partial viewport/extent plan.
Successful viewport and reported extent dimensions are always nonzero.

## FSR eligibility is not activation

`fsr_eligible` requires actual producer-declared opaque, perceptual SDR content,
supported non-sRGB RGBA8/BGRA8 UNORM or RGBA16_FLOAT storage, and both actual crop
axes no larger than their output viewport axes. Unknown/linear/HDR encoding and
sRGB storage are not treated as an implicit compatible path. A nonidentity
orientation request is ineligible until a real producer transforms its pixels
and reports the new identity-oriented source. Native UI is always ineligible.

Eligibility is data/geometric information even when no FSR mode was selected.
It is not shader/device support, selected mode, native engine ownership,
resolution already changed, geometry expansion, faster rendering or measured
FPS. The caller must still resolve `GraphicsSettings` against real capabilities
and exclusive ownership using the existing settings resolver before applying
any native request. This planner grants no permission to duplicate a better
engine effect. It does not decide post-pass implementation or submit a frame.

## Delivery boundary

Only `include/cedar/render_layout.hpp`, `src/render_layout.cpp` and this contract
were authored. No settings enums, source descriptors, renderer, shader, CMake
or other shared code changed. The lead owns renderer/orchestrator integration
and compilation. This worker performed no tests, compilation, runtime, GPU,
window, event, asset or console operation for this source-only batch.
