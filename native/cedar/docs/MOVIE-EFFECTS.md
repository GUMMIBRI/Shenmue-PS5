# Explicit movie-only GPU effects

Owned files: `include/cedar/movie_effects.hpp`, `src/movie_effects.cpp`,
`shaders/movie_cleanup.frag.glsl`, `shaders/movie_blur.frag.glsl`.
The movie cleanup shader and preset mapping are now source-informed **MPL-2.0**
adaptations of the selected PE1 non-game cleanup component. The native ownership
class and separable backdrop facility retain their own authored implementation.
No PE1 game code, art, assets, VRAM protocol or title-specific integration is
copied. Filter math/preset parity does not establish bit-identical displayed
frames or a visual improvement, and no clean-room claim is made.

## Reference licensing correction

The supplied read-only prefix was
`<LOCAL_REFERENCE>/psyz-src`.
Its actual root LICENSE places `psyz/src/platform/` under **MPL2.0**, not MIT.
The complete license is retained at `third_party/licenses/PE1-platform-MPL2.txt`.
The cleanup reference files individually contain MIT SPDX markers, while the
root policy assigns the platform directory MPL2.0. This adaptation conservatively
carries MPL-2.0 markers and retains the complete platform license. Reference files
remain unchanged with their original notices; they are not relabeled.

The earlier root-file-only listing missed the **`movie_cleanup/` subdirectory**.
That lookup was not proof the component was absent. Its actual shader, C module,
header and integration seam have now been read. The earlier independent filter
approximation was not established better, so it was replaced with the proven
Gentle/Strong source math and strengths below.

Reference receipts (SHA256):

- Root LICENSE: `9b94b69a84f835bf22a06a879778541fc32ecb89d66622cce44a5fb067785d04`.
- `psyz/src/platform/movie_blur.h`:
  `5f01407ccb9338719fb803cfc00198115e341088253497f3be0ddf989285189f`.
- `psyz/src/platform/movie_blur.frag`:
  `d6f3c99eff55ca69092a1030c9d7cf569b13211bc1aae737b2e47b662367376a`.
- `psyz/src/platform/movie_lifecycle.h`:
  `2148e421c8f51a5770b6196fe9209319843e5b2093326d8bd24d373d7e1dca3a`.
- `psyz/src/platform/movie_cleanup/movie_cleanup.frag`:
  `5e5773ba33494bb7d9f0457520eefda7827938354ff295534c6aa208f6eb7b46`.
- `psyz/src/platform/movie_cleanup/movie_cleanup.c`:
  `f2473ba17ea1fbca16754a1c19388a695546cf61946947fce424b5b4d92222fe`.
- `psyz/src/platform/movie_cleanup/movie_cleanup.h`:
  `a3da8c87b142271d85d206e5916ee4d8fcc5732b86789debe55994e375050e81`.
- `psyz/src/platform/movie_cleanup/present_movie_cleanup.h`:
  `5c10e217f5818b4e4fc09c1262d48f48f6154d764c88de574b451ad4d16b9fcd`.

The cleanup image algorithm/presets are adapted with attribution. Reference
gameplay fill policy, RGB15/24 and scaled-VRAM flags, game-specific restoration,
logging and movie-lifecycle state are not imported. The separate blur reference's
GLSL coefficients remain unimported: the optional backdrop uses the original
binomial filter documented below. Full MPL notice:
[PE1-platform-MPL2.txt](<LOCAL_REFERENCE>/PE1-platform-MPL2.txt).

## Native API and truthful lifetime

`MovieEffects::initialize(device,target_format)` creates real SDL GPU pipelines
and a sampler. A real producer calls `begin_source(nonzero_serial)` when its
movie ownership begins, and `end_source(the_same_serial)` when it ends. Frames
must explicitly carry `FrameContent::movie` and the active serial. No dimensions,
aspect ratio, pixel format or resolution heuristic classifies a movie.

`record(commands,serial,PostSource,PostTarget,readiness,config,result)` borrows
the common post-processing source/target types. Readiness is explicitly either
completed (the owner actually observed the producing submission complete), or
earlier-in-the-same-commands (the owner actually recorded source writes earlier
in this exact command buffer). Not-ready sources are rejected. Those are caller
contracts, not a fabricated decoder, source detector or autonomous playback hook.

The result counts render passes and reports `output_recorded`, never completed
pixels or GPU readiness. The caller owns submission/cancellation and its actual
fence. A late failure can leave partial passes in the command buffer; the result
retains that count and must not be interpreted as a complete output. The caller
must discard/cancel or explicitly recover that buffer before presenting it.

All calls and destruction belong to the owning main/GPU thread. Device, source
and target must remain valid through their recorded GPU use. Intermediate
textures use SDL target cycling for reuse across queued frames. Source changes,
retirement, resize and shutdown release only owned intermediates/pipelines through
SDL's safe deferred disposal, without a device-idle wait or foreign destruction.
No window, event loop, movie decoder, frame submission or game-state ownership.

## Effects and crop/aspect behavior

Cleanup defaults **Off**; optional strengths are Gentle and Strong. The current
implementation follows the actual PE1 Gentle/Strong spatial3x3 filter:

- Center contributes weight4; the other eight decoded logical texels are fetched
  directly, without the earlier bilinear/luminance-weight approximation.
- Range difference is the maximum absolute RGB-channel difference. The positive
  `1-edge/threshold` weight is squared, with axial factor1 and diagonal0.70710678.
- Gentle uses threshold0.06 and strength0.30; Strong uses0.10 and0.55. The earlier
  native Gentle0.25 value did not match the actual source and is corrected.
- Mixing uses full preset strength on nominal8-pixel-grid residues0 or7 in either
  axis and35% of preset strength elsewhere, exactly as the source does. This is
  a spatial heuristic, **not** detection or metadata of real compression blocks.
- The convex result is clamped to RGB0–1 and alpha is1. Active cleanup therefore
  requires the actual producer's explicit opaque assertion; it never silently
  discards nonopaque source alpha. Off keeps the prior source-alpha behavior.

There is no temporal state, AI, recovered detail, denoising-quality or performance
claim. Source mode3's additional seam-axis stage is not exposed by this API and
is not imported: Gentle/Strong never take that branch in the actual reference.

The reference's authored logical dimensions map to this facility's explicitly
provided decoded crop dimensions. `texel_mode.xy` remains inverse source extent;
the shader derives the integer logical crop grid from those values and normalized
crop size, then clamps logical neighbors and integer fetches to the half-texel
crop interior. No PS1 scale/native5-bit/VRAM heuristic chooses those dimensions.
The owning producer must supply its actual decoded grid, not an already-upscaled
image masquerading as decoded resolution. The lead's movie graph records cleanup
on that1:1 compact grid **before FSR/upscale**. Direct calls with another output
extent map the same logical filter to destination pixels; they do not establish
bit-identical downstream resampling against PE1's presentation chain.

Off skips cleanup math during ordinary aspect-correct presentation; it is not
a gamma transform. Native lifecycle, source readiness, center aspect and crop
contracts are unchanged by this algorithm-parity patch.

The center always uses the complete producer-supplied crop and aspect-fits the
movie viewport. Explicit pixel aspect defaults1 and is bounded to1/16–16; a
non-square producer must supply its actual value. No gameplay stretch setting
enters this class. Without a backdrop, the shader paints black bars only inside
the movie viewport. Outside that viewport, the caller's load operation governs
preservation. An explicit CLEAR load may clear the complete target as SDL defines.

Blur backdrop is optional and defaults disabled. It uses two cropped RGBA8
intermediates and separable9-tap binomial weights `[1,8,28,56,70,56,28,8,1]/256`.
The blurred image aspect-fills behind the preserved center. Brightness0–1 and
tap spacing0.25–16 source pixels are explicit caller settings; neither modifies
the center. No silent source crop, center stretch or downsample fallback occurs.

Every shader sample, including filter taps and passthrough centers, clamps to
the actual source crop's half-texel interior. The second blur pass clamps to its
cropped intermediate. Native sampler addressing is also clamp-to-edge. Source
and target aliasing is rejected; all crop/viewport extents and finite parameters
are checked before rendering.

## Color/storage and resource bounds

Active cleanup or blur requires explicit `perceptual_sdr_unorm` producer encoding.
That legacy tag describes transfer, not mandatory8-bit storage: RGBA16_FLOAT
perceptual SDR is accepted as well as RGBA8/BGRA8 UNORM. Linear/HDR/unknown sources
are unavailable for active effects. Off presentation may retain their numeric
values in supported native storage without pretending a transfer conversion.
Unknown transfer requires unchanged storage format; linear HDR Off presentation
requires float16 source/target, so it cannot silently narrow HDR to UNORM.
Native sRGB storage is rejected to prevent implicit decode/encode from duplicating
the producer's explicit contract. No pow/gamma, unpremultiplication or inferred
alpha conversion is applied. A caller needing another storage/encoding pipeline
must retain its original path instead of labeling it supported here.

Source/target dimensions are capped16384. Center crop has a64MiB RGBA-equivalent
implementation bound. Combined backdrop intermediates are capped64MiB; this
includes3840x2160 but rejects larger working crops beyond that bound. Backdrop
storage is8-bit UNORM even when the center source/target is float; its intentional
quantization applies only to the optional backdrop. No user assets are allocated,
copied or changed by this implementation task.

## Shader/build contract

Lead provides the generic fullscreen `post_vert` and generated
`cedar_post_shaders.hpp` in namespace `cedar::shaders`; fragment symbols are
`movie_cleanup_frag` and `movie_blur_frag`. Vertex stage has no attributes or UBO.
Both fragments bind sampler at set2/binding0 and one std140 UBO at set3/binding0.
The80-byte block consists of five vec4 values in order:

1. `src_rect`: normalized x,y,width,height.
2. `sample_clamp`: crop half-texel minx,miny,maxx,maxy.
3. `dst_rect`: physical target pixel x,y,width,height.
4. `texel_mode`: inverse source width,height; cleanup strength; range threshold.
5. `step_dim`: blur du,dv; RGB brightness; outside-center black-bar flag.

Coordinates derive from `gl_FragCoord`, so no vertex UV convention is guessed.
Lead owns shader compilation, generated headers, CMake and coherent linking.
No runtime, shaders executed, fixture/game tests, asset decoding, console action
or observed-image claim occurred in this worker task.
