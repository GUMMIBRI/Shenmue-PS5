# Explicit PKF texture decoding pipeline

Original composition: `include/cedar/package_texture.hpp`,
`src/package_texture.cpp`. Existing PKF/PVR MIT provenance and miniz MIT notices
remain applicable to their corresponding facilities; no new third-party source
is incorporated by this wrapper. See PKF-SCOPE.md, PVR-SCOPE.md, DDS-SCOPE.md and
the retained notices in `third_party`.

`decode_package_texture(request, workspace)` accepts exact plain PAKF bytes or
an RFC1952 gzip envelope. Gzip must fully verify before PKF parsing. The caller
selects a TEXN index; its exact body is obtained from `PkfView`. At that body's
start, only DDS, PVRT or the supported fixed12-byte GBIX profile is accepted.
The separately proven DDS-in-PVRT transport is accepted at its exact bounded
offset through PvrDdsView (see PVR-DDS-SCOPE.md). No signature scan, nested gzip
guess, texture-ID hash inference or fallback from malformed DDS hints occurs.
Normal PVR decoding is selected only when the envelope explicitly reports
not_dds_profile. Outer wrapper metadata is retained; inner DDS governs output.

The request selects the DDS mip explicitly. The supported PVR profile has one
level; a nonzero mip request returns `unsupported_mip`, preserving parsed metadata
but producing no valid output. A TEXN's eight ID bytes are returned unchanged as
opaque bytes, not interpreted as text, a filename or archive key.

Results retain nested `GzipStatus`, `PkfStatus`, `DdsStatus`, `PvrStatus`, `PvrDdsStatus` and
stage-attempt/success flags. The format tag selects complete DDS or PVR metadata.
DDS color-space and alpha mode are preserved; PVR transfer, display origin and
alpha association remain unspecified. There is no vertical flip, color-space
conversion or inferred alpha processing. PVR raw declared size fields retain
their explicitly unverified status. The bounded TEXN body supplies their extent.

Storage is caller-owned and decoding allocates nothing. Raw package input has
an explicit 256 MiB cap. Gzip keeps its 64 MiB source/inflate-capacity limits.
Full declared source, RGBA, gzip scratch, workspace and immutable request ranges
are checked for overflow and forbidden overlap before mutable writes. Scratch
and workspace are ignored for a plain PKF. Decoders independently validate pixel
geometry, payload bounds and pitched output span before writing RGBA.

`source_bytes` is supplied input length. For verified gzip, `source_consumed` is
the gzip decoder's framing progress; on failure it is not authenticated output.
For plain PKF it is the accepted supplied view length after parse, not a claim
that all opaque remainder bytes were decoded. `package_bytes` is the plain view
length, `texture_bytes` the bounded TEXN body, and `texture_consumed` the selected
decoder's header-plus-payload extent. Inflated bytes on gzip failure are untrusted.
RGBA byte count excludes row padding; RGBA span includes the space between rows.

`output_valid` becomes true only after the entire chosen pipeline succeeds.
Nested failures never set it. A failed gzip can modify its scratch/workspace;
prior RGBA contents are not cleared and must not be used as a new result.
No partial metadata implies successful pixels. DDS mip details and output counts
are valid only for successful output; parse metadata uses its explicit flags.

No filesystem, archive-file lookup, extraction, SDL, window, presentation, console
or runtime/fixture actions were performed. No game assets were opened. This code
provides a callable in-memory asset path; actual archive selection and native
game integration remain separate work owned by the lead.
