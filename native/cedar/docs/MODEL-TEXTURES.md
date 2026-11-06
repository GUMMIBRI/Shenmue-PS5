# MT5 texture-index to exact resource linkage

Original `model_textures.hpp` / `model_textures.cpp` connect real MT5 mesh
texture-index references to opaque eight-byte IDs, then to caller-selected PKF
TEXN resources. This returns bounded raw texture bodies, not fabricated materials,
default textures, shaders or renderer uploads. No filename hash is guessed.

## Primary compatible evidence

Independent Shenmue-Mods/ShenmueDK MIT license was read before new source at exact
revision `1dae443814b3755281d207a13f25571c0e51a5d1`. Copyright (c) 2019 Phil;
complete notice remains in `third_party/licenses/shenmuedk-MIT.txt`. No conflicting
file-level license appeared in reviewed files. No restricted LemonHaze source
or recovered game logic was inspected/incorporated.

- [MT5 reader](http<LOCAL_REFERENCE>/mt5.cpp) collects appended TEXN entries in encounter order, then constructs `model.textures` from all those entries followed by every NAME ID. TEXD, when present, checks their combined count.
- [Strip/material reader](http<LOCAL_REFERENCE>/mt5_instruction.cpp) assigns the current raw texture-index state to each strip material.
- [DAE exporter](http<LOCAL_REFERENCE>/dae.cpp) directly indexes `model.textures` with the surface material's texture index. This establishes the association for in-range indices in this compatible source profile. Its out-of-range texture-zero fallback is deliberately not reproduced.
- [TEXD body](http<LOCAL_REFERENCE>/texd.cpp) contains one 32-bit total count.
- [NAME reader/writer](http<LOCAL_REFERENCE>/name.cpp) stores consecutive [eight-byte TextureIDs](http<LOCAL_REFERENCE>/texture_id.h).
- [TEXL definition](http<LOCAL_REFERENCE>/texl.h) and [reader/writer](http<LOCAL_REFERENCE>/texl.cpp) preserve an opaque header word/count and three-word records. They do not resolve `textureIDOffset` or use TEXL to remap `model.textures`. No origin, pointer or TEXL binding rule is invented here.
- [PTRL reader](http<LOCAL_REFERENCE>/ptrl.cpp) has no body interpretation. PTRL is therefore retained only as opaque bytes.
- [TEXN reader](http<LOCAL_REFERENCE>/texn.cpp) supplies the eight-byte ID followed by a raw PVR-side body, matching the existing bounded `PkfView` contract.

The table order is source-established, not a universal claim about unknown
variants or original runtime pointer-table behavior. TEXL offsets remain an
explicit missing semantic rather than being silently treated as NAME offsets.

## Implemented table contract

`Mt5TextureTable::parse(model)` consumes only `Mt5View::appended_data()` and its
bounded length. It validates every node's inclusive eight-byte header/size.
Recognized layouts are TEXD with exactly one count word, NAME with exact eight-
byte records, TEXL with its exact two-word header/three-word records, TEXN with
at least its eight-byte ID, and opaque PTRL. Extra/unknown layouts fail explicitly.
TEXD, NAME, TEXL and PTRL may each appear only once, matching primary uniqueness.
Trailing incomplete node headers are truncated, not guessed padding.

Successful parsing exposes a maximum of one million texture references.
`texture(meshIndex)` returns embedded TEXN entries first and NAME references
second, regardless of NAME's position among appended nodes. Each ID is copied
as eight exact bytes. Embedded references also return their raw model texture
body; external NAME references have no fabricated payload. Missing mesh indices,
duplicate table nodes, malformed lengths, unsupported nodes and TEXD count
mismatches are explicit errors. Failed parsing discards previous state.

TEXL records and header metadata can be inspected without pretending that their
offsets point anywhere. `opaque_ptrl_body` is raw-only. No node, image, file name,
material or default reference is synthesized to make a lookup succeed.

## Explicit PKF selection

`find_pkf_texture(id, packages, count, policy, result)` compares all eight ID bytes
exactly against actual PKF TEXN IDs. The caller supplies package views and an
explicit policy:

- `first_matching_package`: earlier caller-listed packages have priority. The
  whole first matching package is checked; duplicate matching IDs inside it are
  ambiguous. Later packages are deliberately not considered.
- `unique_across_packages`: all supplied packages must contain at most one total
  matching entry. Any duplicate, including identical-looking payloads, is
  explicitly ambiguous.

Unparsed/null packages, invalid policy, no match and search-budget exhaustion
fail explicitly. The bounded search permits at most 256 packages and one million
visited-package TEXN records. No directory enumeration, package discovery, path
normalization, case folding, ID string parsing or filename hashing occurs.

`resolve_mt5_texture(table, meshIndex, ...)` composes the proven in-range table
association with that exact-ID package lookup. It never silently falls back to
texture zero or chooses a model-embedded body instead of a requested PKF lookup;
callers can explicitly use the embedded body returned by `table.texture`.

## Lifetime and verification limits

Implementation is allocation-free. Model tables/references borrow immutable
model bytes; matched texture spans borrow immutable selected PKF bytes. Retain
that storage throughout use and keep output objects disjoint from source storage.
Output records are cleared on failure and are valid only after success. IDs
themselves are copied; no NUL terminator, byte order or character encoding is
assumed. No checksums, image decoding, GPU upload or final material/shader binding
are implied by finding a resource.

No MT5/PKF/shared code, CMake, renderer or runtime/test changes were made. No game
assets were traversed, read or copied in this tranche. Lead owns compilation and
integration with the draw-record and texture-upload pipelines.
