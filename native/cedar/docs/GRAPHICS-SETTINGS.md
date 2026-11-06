# Portable graphics settings and capability ownership

Original `graphics_settings.hpp` / `graphics_settings.cpp` define validated
settings, one bounded versioned wire record, and a pure capability/ownership
resolver. There is no filesystem access, game save codec, shader application,
renderer mutation or claim that Shenmue PC lacks any particular feature.

## Defaults and supported data

Every feature defaults to `preserve_engine`. Stored render scale is 100%, gamma
is 1, saturation is 1 and anisotropy is 1. Latent controls match the reference
defaults when explicitly enabled: RCAS0.5 maps to AMD stops1, scanline0.32 to
0.08, mask0.36666667 to0.055, and blur0.11111111 to2px. None enables an effect or forces the
engine to adopt that value. In particular the module does not switch an engine
filter off merely because native defaults preserve the original presentation.

Selections are intentionally limited to the current requested feature family:

| Feature | Explicit selections beyond preserve-engine | Validation |
| --- | --- | --- |
| Render scale | native override | Integer 50–200% |
| Upscale | native, linear, FSR1 | Known enum values only |
| RCAS | off, on | Sharpness 0–1 |
| Display filter | off, scanlines, CRT | Scanline/mask strengths 0–1 |
| Gamma | native override | 0.25–4; neutral 1 |
| Saturation | native override | 0–2; neutral 1 |
| Sampling | nearest, linear, anisotropic | Integer anisotropy 1–16 |
| Aspect | 4:3, 16:9, fit | Known enum values only |
| Movie borders | black, blur | Blur strength 0–1 |
| Movie cleanup | off, gentle, strong | Known enum values only |

All float parameters must be finite, including latent values belonging to a
preserved or disabled effect. Bounds are inclusive. No FXAA, MSAA, TAA, ray
tracing, DLSS, PBR or AO setting is exposed. FSR1 means only a requested spatial
mode; the schema does not fabricate an implementation or performance claim.
Actual shader parameter conventions and movie classification remain integration
work, not guessed behavior in this settings codec.

## Strict v1 wire record

The record is exactly 52 bytes. All multibyte words use little-endian order;
floats are explicit IEEE754 binary32 bit patterns. No native struct bytes,
padding, pointer, enum storage layout or host endian assumption is serialized.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII CGFX magic |
| 4 | 2 | Version 1 |
| 6 | 2 | Total record length 52 |
| 8 | 4 | Control flags: bit0 scale override, bit1 gamma override, bit2 saturation override |
| 12 | 4 | CRC32/IEEE |
| 16 | 2 | Render scale percent |
| 18 | 1 | Upscale enum |
| 19 | 1 | RCAS enum |
| 20 | 1 | Display filter enum |
| 21 | 1 | Sampling enum |
| 22 | 1 | Anisotropy |
| 23 | 1 | Aspect enum |
| 24 | 1 | Movie borders enum |
| 25 | 1 | Movie cleanup enum |
| 26 | 2 | Reserved zero; nonzero is rejected |
| 28 | 4 | RCAS sharpness |
| 32 | 4 | Gamma |
| 36 | 4 | Saturation |
| 40 | 4 | Scanline strength |
| 44 | 4 | Mask strength |
| 48 | 4 | Movie blur strength |

Every mode enum has preserve-engine at zero; remaining numeric values follow
the public header's declaration order. Override flags correspond only to the
three numeric override controls. All other header control bits are rejected.

CRC32 uses reflected polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`,
over all 52 record bytes with bytes 12–15 treated as zero. It detects accidental
corruption; it supplies no authenticity, security boundary or game-save
compatibility guarantee. The format is original and independent of game saves.

Encode validates completely before writing, requires capacity at least 52,
rejects overlap with the settings object and writes exactly one record. It
returns the actual successful wire-byte count; errors write nothing. Decode
requires an exact complete record, rejects truncated/trailing data, unknown
version, length, bits, enums, checksum and invalid/nonfinite scalar values.
**Every decode failure replaces the entire destination with preserve-engine
defaults.** Success commits a fully validated candidate, never partial state.

## Implementation capability and exclusive ownership

`GraphicsCapabilities` is caller-supplied evidence, not discovery. Every feature
has a supported-native-mode bitmask, engine state (`unknown`, `not_present` or
`existing_equal_or_better`) and an exclusive-native-ownership flag. The default
capability object claims no native modes and no knowledge of engine features.

Supported-mode bit N corresponds to numeric enum value N. Preserve mode needs
no implementation, and bit0 must not be claimed. Numeric override controls use
the bit for `GraphicsOverride::native`. Unknown mode-mask bits and invalid
engine states are errors. Sampling additionally respects the caller's proven
maximum anisotropy, which defaults to 1.

For an explicitly requested mode, the resolver requires an actual implemented
native mode. Unsupported modes return `unavailable` rather than fake support.
If the native mode exists but engine capability is unknown, or an equal/better
engine feature already exists, native duplication is blocked unless exclusive
ownership has been explicitly confirmed. That flag means the engine effect has
been bypassed/disabled or the caller otherwise controls the sole processing
path; merely selecting a mode does not establish it.

This specifically prevents native gamma from silently duplicating an existing
PC gamma path. Integration should use the verified engine control when suitable,
or obtain explicit native ownership before enabling another gamma operation.
No official PC setting is assumed absent, disabled or inferior by this module.

Resolution applies nothing. It returns per-feature decisions and requested/
blocked masks. On any blocked mode, the overall result is `unavailable` and the
native apply mask is zero, avoiding an accidentally partial plan. On complete
success the mask identifies the requested native controls that may be applied
by the owning renderer. Preserved controls never enter that mask. Existing
renderer/device/shader limitations still require their own real implementation
and error checks; a persisted setting is not implementation evidence.

## Delivery boundary

Only these settings source/header/docs were added. No other project source,
renderer, original/reference data, saves or assets were read or changed for this
batch. No files are written by the module. Lead owns compilation and subsequent
postfx integration; this worker performed no compilation, runtime or tests.
