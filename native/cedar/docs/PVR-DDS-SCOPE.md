# Proven DDS-in-PVRT transport subset

Original bounded view: `include/cedar/pvr_dds.hpp`, `src/pvr_dds.cpp`. Format
derivation uses independently MIT-licensed ShenmueDK revision
`1dae443814b3755281d207a13f25571c0e51a5d1`, Copyright (c) 2019 Phil. License was
read before source, and its complete notice remains at
`third_party/licenses/shenmuedk-MIT.txt`. No restricted recovered game source,
DevIL implementation, SDK proprietary headers or game bytes were used.

## Exact primary evidence

- [MIT license](http<LOCAL_REFERENCE>/LICENSE)
- [PVR header and GBIX structures](http<LOCAL_REFERENCE>/pvr.h)
- [PVR pixel and data enums](http<LOCAL_REFERENCE>/formats.h)
- [PVR reader/writer transport](http<LOCAL_REFERENCE>/pvr.cpp)
- [DDS stream reader/writer](http<LOCAL_REFERENCE>/dds.cpp)

The PVR reader reads the entire 16-byte header and immediately invokes its DDS
reader for data formats `0x80` or `0x87`. The writer emits optional fixed12-byte
GBIX, then the PVR header, then DDS. Its pixel values `0x80` and `0x81` select
DXT1 and DXT3 respectively. Thus the exact DDS start is16 for bare PVRT or28
after the compatible fixed GBIX form; no offset scan or guessed alignment.

The compatible DDS stream reader takes the remaining enclosing stream extent
and delegates DDS parsing. Its writer asks a separate library to produce DDS
file bytes and writes them at that stream position. No external image library
code is copied or linked by our transport view.

## Implemented boundary

`PvrDdsView::parse(resource, bytes)` accepts only that bounded transport subset.
Both a recognized DDS pixel value and DDS data value are required. It verifies
exact `DDS ` magic at the established offset, then `dds_bytes()` exposes the
remaining caller-bounded extent. It does not authenticate an inner DDS header or
payload: the returned span MUST separately pass `DdsView` before use.

`not_dds_profile` is returned only when neither pixel nor data code signals DDS
in an otherwise readable PVRT header. This lets a caller select the narrow
16-bit PVR decoder. A partial DDS hint with an invalid partner is an explicit
unsupported error. Malformed/truncated wrappers and incorrect inner signatures
must never be masked by fallback to another decoder.

Outer width, height, reserved bits, pixel/data values, GBIX index and raw size
fields are preserved without overriding inner DDS metadata. The compatible
reader does not establish a required equality between outer and inner dimensions;
the inner DDS decoder governs decoded dimensions, alpha and transfer semantics.
Likewise this view does not force the outer pixel label to rewrite DDS alpha or
silently reinterpret an inner format. Actual DDS support remains that decoder's
explicit supported subset.

The reviewed DDS-path writer records its DDS byte length in the PVRT size field,
but the reader does not use that as its stream bound. This view therefore leaves
the raw size fields semantically unverified, and records only an observed
`declared_size_matches_dds_span` equality. The caller MUST supply a separately
bounded exact resource, such as a validated TEXN body. Any included trailing bytes
are subsequently reported by the DDS decoder, never assigned guessed meaning.

No four-byte prefix/RLE variant, generic GBIX extension walking, signature search,
offset fallback, palette or classic-PVR decode is performed. Input is immutable,
non-owning and capped at256 MiB. Failed parse clears prior state. Header accesses
and pointer range arithmetic are bounded. No allocation, files, extraction,
runtime checks, console operations or asset copies occurred. Lead owns integration
and compilation; this is not evidence every PC texture uses this profile.
