# In-memory DDS texture resource composition

Original files: `include/cedar/texture_resource.hpp`, `src/texture_resource.cpp`.
This joins existing bounded facilities; it contains no new third-party code.
The existing gzip decoder retains its miniz MIT provenance and notices.

Call `decode_texture_resource(request, workspace)` with source memory, an explicit
mip index, caller-owned RGBA storage/pitch, and, for gzip, complete inflate storage.
There is no file access, heap allocation, archive search, path inference, asset
extraction or rendering. This does not imply that any archive entry contains DDS.

Only exact `DDS ` or RFC1952 gzip magic is recognized. Raw DDS passes directly to
`DdsView`. Gzip passes through `decode_gzip`, including envelope/CRC/size checks;
the complete verified output then passes to `DdsView`. A nested gzip is not
recursively decoded, and zlib/raw-DEFLATE or other package signatures are never
guessed. Gzip may contain concatenated members as permitted by the gzip decoder;
their combined verified bytes must form the DDS memory view.

Raw DDS is capped at `DdsView::max_file_bytes` (256 MiB). Gzip source and scratch
capacity obey the gzip decoder's 64 MiB input/output caps. All other DDS limits
and rejected formats remain unchanged; see DDS-SCOPE.md. Full source, RGBA,
scratch and workspace ranges are checked for address overflow and forbidden
overlap before decoding. The immutable request object is also protected from
mutable buffer aliasing. Scratch/workspace are ignored on the raw path.

The result exposes stage-attempt flags, nested `GzipStatus` and `DdsStatus`, source
consumption, inflate byte count, RGBA bytes and the occupied row span. Inflated
byte counts on a gzip failure are untrusted progress, not authenticated output.
`output_valid` is false on every failure. A failed gzip can change scratch and
workspace; it does not proceed to an RGBA decode. Existing RGBA buffer contents
are not cleared and must not be treated as a new result after failure.

Complete `DdsMetadata` is retained after successful DDS parsing, including color
space, alpha mode, format, mip count and trailing-byte count. Selected mip details
and RGBA byte counts become valid only after successful output decoding. There
is no sRGB conversion, unpremultiplication or opaque-alpha override. A renderer
must explicitly handle those semantics before using the pixels for display.

No game assets, fixtures, runtime tests, console operations or external downloads
were used. Lead owns batch compilation and actual game pipeline integration.
