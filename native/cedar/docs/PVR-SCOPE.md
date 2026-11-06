# Narrow PVR resource decoder

Owned implementation: `include/cedar/pvr.hpp`, `src/pvr.cpp`. This is independent
bounded view/decoder code informed by MIT ShenmueDK at exact revision
`1dae443814b3755281d207a13f25571c0e51a5d1`, Copyright (c) 2019 Phil. Its license
was read before codec source. Preserve `third_party/licenses/shenmuedk-MIT.txt`
with any source delivery. No restricted LemonHaze game implementation was used.

## Proven supported profile

Pixel values: ARGB1555 `0x00`, RGB565 `0x01`, ARGB4444 `0x02`. Data values:
square twiddled `0x01`, full-codebook VQ `0x03`, tightly packed rectangle `0x09`,
and rectangular twiddled `0x0D`. Square twiddling requires
equal power-of-two dimensions and uses x bits in odd address positions, y bits
in even positions, then two bytes per texel. The compatible source's twiddle map
and square codec establish that ordering explicitly. No guessed Morton-axis swap.

Rectangular twiddling uses square tiles of side `min(width,height)`, laid out
by tile rows then tile columns. Each tile uses the same x-odd/y-even ordering.
This follows `RectangleTwiddled::decode` in the compatible source. The minimum
dimension must be a power of two and divide both dimensions, ensuring full dense
tiles; other shapes are explicitly unsupported rather than indexing sparse gaps.

VQ is restricted to square power-of-two dimensions with a full2048-byte codebook:
256 vectors, each containing four16-bit texels. An 8-bit twiddled index selects
each2x2 vector. Texel order inside a vector is `(0,0),(0,1),(1,0),(1,1)` because
the compatible VQ decoder advances y inside x. The index plane contains
`max(1,width*height/4)` bytes. The1x1 case uses the selected vector's first texel,
as explicitly implemented by the compatible decoder. All256 vectors are retained
even for tiny textures; small-codebook VQ is not silently treated as this profile.
The complete codebook and index plane must fit the bounded resource before use.

RGB channels use the compatible pixel codec's integer floor expansion to 8-bit;
ARGB1555 alpha is 0 or 255, RGB565 alpha is 255, ARGB4444 alpha is nibble times17.
Output is RGBA rather than the compatible pixel codec's intermediate BGRA buffer.
Rows retain the codec's encoded coordinate order. The higher-level compatible
image reader later flips vertically; that image-library convention is not imposed
by this resource decoder. Display origin, transfer function and alpha association
remain unspecified; no hidden flip, sRGB conversion or premultiplication occurs.

Bare PVRT uses the compatible 16-byte header. Optional GBIX supports only that
reader's fixed12-byte struct followed immediately by PVRT. No generic extension
scan, four-byte size prefix, alternate alignment or RLE inference is implemented.
GBIX extension variants that place PVRT elsewhere are rejected by signature.

**Size-field limitation:** the reviewed compatible reader and incomplete writer
do not establish universal PVRT/GBIX size semantics. Both raw size fields are
therefore preserved as opaque UNVERIFIED metadata and never used as bounds.
The caller MUST supply a separately bounded exact resource, such as a validated
TEXN body. Required pixel bytes come solely from proven supported dimensions and
16-bit layout (or proven VQ codebook/index layout) and must fit that caller extent.
This is not a generic PVR chunk
walker and does not authenticate a declared size. Bytes after required pixels
are reported as trailing and ignored, without assigning padding meaning.

Small-codebook VQ, palette/indexed, YUV, bump, mips, rectangle stride,
DDS-in-PVRT, modern Imagination PVR2/PVR3 and all other layouts are unsupported.
No fallback interprets unsupported compressed storage as linear texels.

## API and bounds

`PvrView::parse(memory, bytes)` retains immutable caller-owned storage and clears
its previous state on failure. `metadata()` describes the parsed narrow profile;
`decode_rgba(output, capacity, row_pitch)` writes caller-owned RGBA. No heap,
file operations, archive traversal, extraction, presentation or format guessing.

`pixel_offset/pixel_bytes` cover the complete encoded payload. For VQ, separate
`codebook_offset/codebook_bytes` and `index_offset/index_bytes` expose the checked
subspans; other layouts leave those fields zero. Consumed bytes include the full
codebook and index plane, and trailing counts begin after that complete payload.

Limits are 64 MiB input/decoded RGBA and dimension4096. Payload multiplication,
source availability, pitched output span, address wrap and overlap are checked
before output writes. Row padding is untouched. Metadata and source storage must
remain immutable throughout decode. No game asset inspection or runtime tests
were performed; parent owns compilation and later native asset integration.

## Exact compatible primary references

- [MIT license](http<LOCAL_REFERENCE>/LICENSE)
- [Header structs](http<LOCAL_REFERENCE>/pvr.h)
- [Pixel/data enum values](http<LOCAL_REFERENCE>/formats.h)
- [Twiddle bit-spreading map](http<LOCAL_REFERENCE>/twiddle.h)
- [Square and rectangle codec order](http<LOCAL_REFERENCE>/data_codec.cpp)
- [16-bit channel/scaling codecs](http<LOCAL_REFERENCE>/pixel_codec.cpp)
- [Reader prefix and image-coordinate conventions](http<LOCAL_REFERENCE>/pvr.cpp)

No third-party source file is copied wholesale. Structural constants and the
compatible codec behavior are derived with attribution, not claimed clean-room.
