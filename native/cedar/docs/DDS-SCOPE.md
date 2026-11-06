# Original bounded DDS facility

Source: `include/cedar/dds.hpp`, `src/dds.cpp`. Authored here from published format
descriptions; no third-party decoder code or restricted Shenmue implementation
was read, incorporated or translated. No game textures were opened or copied.

## API and limits

`DdsView::parse(memory, size)` retains an immutable, caller-owned memory view.
`metadata()` describes the surface; `mip(index, descriptor)` returns validated
byte boundaries; `decode_rgba(index, output, capacity, row_pitch)` writes one mip
as RGBA bytes without allocations. A failed parse clears the view. Decode checks
output bounds/overlap before any writes and leaves row padding untouched.

Limits: 256 MiB input, 16,384 maximum dimension, at most 15 levels, and 64 MiB
decoded bytes per mip. Declared mip counts cannot exceed the dimensions' complete
chain. All declared levels must fit the memory view. Extra trailing bytes are
reported and ignored; they do not become guessed faces or additional surfaces.

Only a single 2D surface is accepted. Arrays, cubes, volumes, 1D resources and
unknown resource flags are rejected. Legacy headers support DXT1, DXT3, DXT5,
and exact 32-bit RGBA/BGRA masks, including RGBX layouts with absent alpha.
DX10 headers support only typed UNORM and UNORM_SRGB RGBA8, BGRA8, BC1, BC2, BC3.
Typeless, signed, float, BC4–7, other masks and legacy DXT2/DXT4 are unsupported.

The top uncompressed mip may have an explicitly declared padded row pitch only
when there is a single mip. Padded multi-mip layouts are rejected because the
lower mip alignment is not specified here. Other rows are tightly packed.
Compressed payload lengths are calculated from complete 4x4 blocks rather than
trusted from the header's advisory linear size. Edge texels are cropped on decode.

## Sample and alpha behavior

Legacy color space remains `unspecified`. DX10 UNORM and sRGB variants preserve
their explicit transfer metadata. Decoding never linearizes sRGB, unpremultiplies
RGB, or applies display color management. DX10 alpha mode is retained, including
custom and opaque declarations; it does not silently override encoded samples.
RGBX without a stored alpha channel produces alpha 255.

BC1 uses the endpoint order to choose four opaque colors or three colors plus
transparent black. BC2 always uses four color entries and expands each explicit
four-bit alpha to eight bits. BC3 always uses four color entries; its two alpha
endpoints select the documented six-interpolant or four-interpolant-plus-extremes
case. Alpha interpolation uses nearest-integer rounding as in Microsoft's tables.

RGB565 endpoints are treated as exact normalized 5/6-bit values; interpolation
occurs before nearest-integer conversion to RGBA8. This avoids an intermediate
endpoint-quantization step. GPU implementation-specific interpolation precision
is not claimed bit-identical. No texture-channel swizzles or game-specific normal
map conventions are inferred.

## Primary specification references

Read November 5, 2026. These specify the container fields, enumerants and block
semantics; this is an original implementation, not copied example source.

- [DDS container layout and pitch guidance](http<LOCAL_REFERENCE>/dx-graphics-dds-pguide)
- [DDS_HEADER field definitions](http<LOCAL_REFERENCE>/dds-header)
- [DDS_PIXELFORMAT masks and flags](http<LOCAL_REFERENCE>/dds-pixelformat)
- [DX10 resource and alpha metadata](http<LOCAL_REFERENCE>/dds-header-dxt10)
- [DXGI format enumerants](http<LOCAL_REFERENCE>/ne-dxgiformat-dxgi_format)
- [BC1 layout and endpoint-order behavior](http<LOCAL_REFERENCE>/opaque-and-1-bit-alpha-textures)
- [BC2/BC3 alpha layouts and interpolation](http<LOCAL_REFERENCE>/textures-with-alpha-channels)

No runtime, fixtures, game assets, console actions or external downloads were
performed. Lead owns batch compilation. This facility is not yet connected to a
native game texture producer, and does not establish actual asset compatibility.
