# Sparse graphics settings to real native configuration

`plan_graphics_apply` translates the existing settings schema into the actual
`Fsr1Options`, `DisplayEffectSettings`, `MovieEffectsConfig`,
`GpuTextureSampling` and render-layout interfaces. It returns data and exact
update masks, not an effect-applied result. It calls no GPU, window, producer,
asset, event, I/O or engine API and changes no caller settings/resources.

## Capability and ownership gate

Every call first uses `resolve_graphics_settings` with the actual caller-supplied
capabilities. Its schema/mode/device-limit/engine-state/ownership rules remain in
force: unknown or existing equal/better engine effects are not silently
duplicated; required exclusive native control must be genuinely confirmed.
Neither selection, dimensions nor this translator discover device support or
whether a PC engine effect is better. Even falsely advertising capability does
not enable native gamma or saturation: those have no implemented module here
and return `unimplemented_feature`. Preserve their engine handling.

Failure leaves an entirely default/disabled plan: zero native mask and update
flags. Resolution diagnostics, offending feature and an attempted layout's
diagnostic remain available. There is no partially enabled future-feature stub.
All-preserve settings are a valid no-op without producer/layout requirements.

## Sparse updates and actual producer scope

Only fields whose module update flag is true may be passed to that module.
Preserve selectors do not configure a module or replace its parameters with
defaults. Optional baseline pointers describe **actual current Cedar native
configuration**, not the game engine. Null asserts a fresh disabled native
module; never pass null for a live configured native module to manufacture a
blank history. Fresh FSR/EASU, RCAS, display filters and movie effects all remain
Off until explicitly selected.

`plan.native_mask` identifies capability-resolved, successfully translated native
requests applicable to this explicit producer content. `inactive_content_mask`
explains otherwise valid native requests belonging to another content type:
movie-only fields are inactive on games, game render/aspect changes on movies,
and all native overrides on native UI. Native UI sets `bypass_post` and produces
no update flags. This is explicit `FrameContent` routing, not dimensional
classification or a claim that the feature is unavailable forever.

`result.layout_attempted` distinguishes a real layout from default untouched
storage. Full no-op, content-only bypass and sampling-only updates require no
image layout and must not consume the zero default layout as a viewport. Active
post/layout requests use the genuine crop, pixel aspect, orientation, engine
baseline, output and budget from `inputs.layout`; authoritative validated
settings replace only the request's settings field. `layout_update` signals
native render-scale/aspect requests, while post modules may still need a valid
layout for current source/viewport eligibility. No producer/camera/resolution
change has happened merely because an extent request was returned.

## FSR1 and RCAS

FSR1 selection enables real EASU and leaves RCAS disabled unless explicitly
requested or an actually active native baseline already used it. A latent true
RCAS field on a disabled module is not evidence of an existing effect and does
not authorize a new RCAS pass. Normalized sharpness maps exactly as:

```text
AMD sharpness stops = 2 * (1 - normalized_sharpness)
```

Thus normalized 0 maps to 2 stops, 1 maps to 0. Explicit RCAS Off disables it;
On requires final real EASU enabled. The current `Fsr1` API has no RCAS-only path,
so On without selected FSR1 or a genuinely already-enabled native FSR baseline
returns `rcas_requires_easu`. Disabling EASU while preserving a known active RCAS
also returns `preserve_rcas_conflict`; the caller must explicitly turn RCAS Off
rather than letting an inseparable pass silently disappear.

`effective_easu_enabled`/`effective_rcas_enabled` describe the translated native
configuration only when `fsr_update=true`. RCAS is effective only when both
enabled fields are true. These flags are not global engine introspection,
recorded passes, output readiness or GPU execution.

Enabled FSR must satisfy the actual layout's opaque/perceptual-SDR/storage/
upscale-only checks and retain an explicitly available original fallback path.
Future actual source/output comes from the real FSR record, not a fabricated
PostSource changed by this translator.

Native and linear upscale modes produce an explicit original-presentation
directive, disable this native FSR route, and carry an actual `SDL_GPUFilter`.
Native preserves the caller's original filter; linear chooses LINEAR only when
the caller confirms that original presenter supports it. Real availability and
nearest/linear support flags are mandatory; the translator does not pretend a
new blit implementation exists. Original route updates and FSR configuration
updates remain distinct flags.

## Display and movie controls

Native display mode maps to actual Off/Scanlines/CRT. Normalized settings map
to the facility's bounded maxima: scanline strength times 0.25, mask strength
times 0.15. Mask-color weights and existing filter choice come from a supplied
native baseline, otherwise the fresh facility's neutral weights and caller's
actual original filter. Active filtering requires actual producer `logical_rows`
in 1–8192, perceptual SDR, supported source/target storage and original fallback.
No source-height heuristic, second gamma conversion or default CRT activation
is added.

Movie cleanup maps exactly to Off/Gentle/Strong. Explicit blurred borders enable
the actual backdrop and map normalized blur strength to `0.25 + 15.75*t` source
pixels, the backend's supported radius. Strength zero is the smallest supported
enabled blur, not a falsely reported disabled pass. Newly selected blur uses
brightness 1 so it does not secretly dim or recolor the original image. A
preserved live baseline retains untouched module options.

Explicit black borders set `movie_clear_to_black`. The owning presenter must
actually clear the caller target to opaque black (RGBA 0,0,0,1) before the
aspect-preserved movie image; merely changing the boolean is not presentation.
The plan never clears/modifies a producer texture itself. Movie pixel aspect
comes from the actual producer, must fit the backend's 1/16–16 range, and is
recomputed for that producer rather than guessed from dimensions. A pending
orientation transform, unsupported numeric format/transfer or crop above the
backend's 16-megapixel limit returns unavailable diagnostics. Existing unknown/
HDR Off-path storage restrictions are respected. Movie source serial/readiness,
actual lifetime bracketing and recording remain the caller's responsibility.

## Real texture sampling metadata

Any sampling override requires an actual `GpuTextureSampling` baseline so U/V
address modes remain untouched. Nearest maps min/mag/mip filters to nearest;
linear maps them to linear; anisotropic uses real linear/trilinear metadata and
the selected bounded anisotropy count. This creates no mip chain and makes no
claim that already-uploaded textures changed: the owning texture/upload path
must consume the metadata with actual mip data or recreate its sampler.

Above 1x, anisotropy requires both resolver-approved device limits and explicit
confirmation that the real device was created with sampler anisotropy enabled.
A selected mode or maximum number is not confirmation. At 1x it is effectively
linear/trilinear sampling with no greater-than-one anisotropic enablement claim.
Sampling-only translation can precede a frame and does not require invented
image dimensions or layout.

## Application boundary

Only `include/cedar/graphics_apply.hpp`, `src/graphics_apply.cpp` and this contract
were authored. Existing settings, post, context, shader and CMake files were not
changed. The lead owns configuration calls, actual renderer/producer consumption
and target compilation. Each module's later record/fence result remains the
authority for commands recorded or resource completion. This worker performed
no compilation, runtime test, asset read, I/O, GPU or console operation.
