# Authored MAPINFO CHRS placement recognition profile

`SceneResourceView` parses real model filename references, object tags,
positions and optional angle/image references from caller-selected bytes using
the pinned New Yokosuka authored recognition profile. It is not a complete
MAPINFO container/script decoder, entrypoint loader, actor scheduler or claim of
an original scene assembled. It emits no default meshes, scale, angle, image,
camera or scene. Empty recognized input has count zero, never fabricated objects.

## Primary source and rejected alternatives

Root GPL version3 was read before authored New Yokosuka source; inspected files
carry no conflicting file-level notice. New header/source are GPL-3.0-only.
Retain `third_party/licenses/new-yokosuka-GPL3.txt`. Exact primary implementation:

[`tools/lib/mapinfo_character_placements.js`](http<LOCAL_REFERENCE>/mapinfo_character_placements.js),
revision `b0df7f2720a7eecabce2e610fdedec35bf1243e3`, blob
`054dceb53dc8b7606a2c3e1afd86dba640a3c660`, length5485 bytes, UTF-8 SHA256
`004b3eeedd50d43eeac68b590a54b61cfdf204a2fde32da074b4f2619a161d75`.
Relevant functions: `readCString`, `relativeString`, `findToken`,
`propertyValue`, `recordStart`, `extractMapinfoCharacterPlacements`.

The exact authored CLI wrapper `tools/worlds/extract_mapinfo_character_placements.js`
(blob `ad0e32d305456ea81083e30898d206d325d5cbe8`) identifies that dependency.
Its model-catalog/prefix/data output adapters are excluded; no catalog or
generated placement data was opened. The other targeted tool,
`tools/worlds/placement_extractor.py` (blob
`369ef68fe505431a95d5c8c83a4d46da31692a55`), explicitly searches heuristic
ID/scale patterns and emits invented slot markers; it was rejected.

Earlier MIT SDK metadata/WDAT header/source inspection identified weather,
not placement. None of those routines was reused. SDK root MIT was read first;
no SDK scene rules were inferred from weather or placeholder file metadata.
No external/recovered C file, generated content, restricted engine, game payload,
asset traversal, runtime or tests were accessed. New code derives its bounded
placement recognition only from the authored dependency linked above.

## Exact supported bytes

The first byte occurrence of CHRS has a little-endian inclusive u32 chunk size
at+4, at least8 and bounded within the supplied bytes. The first following STRG
marker is located starting at CHRS end and validated likewise. These marker
searches follow the authored tool, rather than prove a general top-level chunk
grammar. A malformed first matching marker fails explicitly; later markers are
not guessed as replacements.

Four-byte-aligned candidates relative to CHRS payload are recognized from:

```
relative(model.MT5), relative("Character"), u32(0x22), TAG4, u32(propertyCount)
```

Relative u32 values are unsigned forward displacements from their own word.
Both targets must name nonempty printable-ASCII NUL-terminated strings wholly
inside STRG payload. TAG4 is exactly four A–Z/0–9/underscore bytes and
propertyCount is1..256. The native safety profile additionally requires the
entire introducer inside CHRS payload. A case-insensitive .MT5 extension is
required; exactly one leading `$` or `@` is removed from the borrowed filename,
matching the source. `model_string_offset` retains the original string start.
No extension, path prefix, actor name or archive filename hash is invented.

Record end is the next recognized tag offset or CHRS end, matching the source.
Property references are scanned at four-byte steps from tag+8, not decoded as
an invented full property VM. `property_count` is retained as declared metadata;
the source does not establish complete property lengths/count traversal.
Unknown property names remain uninterpreted. Supported named forms are:

| Name | Type | Payload |
|---|---:|---|
| Position | 0x49 | Three little-endian IEEE754 binary32 values |
| Angle | 0x49 | Three binary32 values |
| Angle | 0x01 | One binary32 Y angle |
| Image | 0x03 | Relative u32 pointer to STRG string |

Every recognized payload must fit inside its actual record, a stricter boundary
than the tool's whole-buffer float checks. Floats must be finite. Position is
required for every recognized record; absent position fails instead of silently
omitting the record. Unsupported types for a known named property and duplicate
recognized properties fail explicitly. Image/Angle are optional and have
presence flags; raw zero-filled absent fields are not defaults. Yaw-only sets
`yaw_only=true`; its X/Z components are absent, not a complete inferred pose.

The authored tool labels Angle as rotationDegrees; that label is retained in
`angle_degrees`, without proving complete original coordinate/SRT conventions.
It also emits default angle0/scale1; this native view deliberately does not.
The caller must establish model coordinate conversion, actual scale, model
loading, texture lookup, actor handle ownership, scene entrypoint and collision
binding separately. Image is an opaque string reference, not automatically a
texture filename or material binding.

## Storage, limits and output validity

Input is at most64MiB, with at most16384 recognized records and strings at most
1024 bytes. Marker scans and property scans are bounded; the string limit also
bounds work when speculative relative words repeat. IEEE754 binary32 is required
at compile time. All reads use explicit little-endian loads and copied float
bits, with no unaligned struct casts or pointer relocation.

Caller supplies aligned `ScenePlacementRecord` workspace and its actual capacity.
Full declared input/workspace/view spans must be valid and disjoint; multiplication,
address overflow and obvious overlap are checked before mutation. Invalid/aliased
buffer arguments leave the prior view untouched, because clearing it could write
through the rejected alias. After safe span validation, parse clears prior view
before parsing; any subsequent failure leaves `parsed()==false`. Workspace can be
partially clobbered on failure and is never valid without successful parse.
Reusing workspace invalidates its earlier views, including after a failed parse.

`insufficient_capacity` reports total recognized starts in `required_records`;
that capacity query has not validated every record's properties and is not a
successful parse. Successful result reports actual complete record count.
General failures report zero required count and a relevant byte offset. Unknown
nonmatching CHRS words are not claimed as valid/supported records; this is the
explicit authored recognition subset, not an inventory of all scene content.

A successful view borrows both the immutable original input strings and workspace.
Keep both alive and unchanged throughout use. `record(index)` returns null outside
the successful count. There is no allocation, IO, extraction, resource readiness,
GPU action or original story/data incorporation. Yoko owns integration/compilation;
this worker performed no compile, runtime, tests or console operation.
