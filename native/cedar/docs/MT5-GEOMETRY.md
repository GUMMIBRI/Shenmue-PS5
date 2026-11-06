# Native renderer-facing MT5 triangle stream

Original `mt5_geometry.hpp` / `mt5_geometry.cpp` connect the completed MT5 parser
to caller-owned draw records and an actual expanded sequential triangle vertex
stream. This performs geometry extraction into memory, not filesystem extraction
or a renderer/game frame. No placeholder vertices, faces, materials or scenes
are generated.

## Source behavior and provenance

The implementation consumes the existing original `Mt5View`, `Mt5Strip::triangle`
and `static_vertex` APIs. Exact compatible MIT primary evidence and preserved
notice are in `MT5-SCOPE.md` and
`third_party/licenses/shenmuedk-MIT.txt`: Shenmue-Mods/ShenmueDK revision
`1dae443814b3755281d207a13f25571c0e51a5d1`, Copyright (c) 2019 Phil.
No additional third-party code or restricted LemonHaze source was read/copied.

Each source strip with at least three corners produces one draw and exactly
`(cornerCount - 2) * 3` expanded vertices. The existing primary-compatible
winding is preserved. Degenerate triangles remain real source triangles;
zero-, one- and two-corner strips produce no faces or draws. No vertex welding,
deduplication, guessed index reconstruction or implicit cleanup is performed.

Each emitted corner is resolved through its actual `source_node`, including
proven static linked indices. Draw ownership remains the original mesh node;
vertex ownership can differ. Vertices preserve local position/normal, world
position, signed raw index, global index, source-node/local index, raw/normalized
UVs and optional BGRA bytes. The legacy translated normal is separately named
and is never advertised as a proper lighting normal. No inverse-dependent
lighting operation occurs, so singular/zero-scale nodes retain supported forward
geometry. No runtime skinning/animation or coordinate conversion is invented.

Draw metadata preserves unresolved texture-index references, command and opaque
strip word, UV size/precision/mirror state, and a borrowed span of raw attribute
bytes. Absent UV/color flags remain explicit; zeroed unused fields are not
default colors, texcoords or fabricated material bindings.

## Complete preflight and memory contract

`measure_mt5_geometry(view)` walks every actual output triangle and validates
every corner and source-node static transform. Counts become available only
after complete successful preflight. It reports required draws, triangles and
expanded vertices, without allocating or writing output arrays.

`extract_mt5_geometry` first repeats that complete preflight, then checks caller
capacities, count/byte multiplication overflow and obvious aliasing before
writing. It checks draw-versus-vertex output overlap, both outputs against the
view object, and the exposed contiguous node workspace. Full underlying model
byte-range disjointness cannot be established through the public view API:
the caller must keep all input bytes, node workspace and view immutable and
disjoint from output arrays throughout both passes. Capacities describe actual
valid caller-owned arrays; null pointers with nonzero capacities are invalid.

All triangles and transforms are therefore checked before the first output
write under that contract. Only complete successful extraction sets `written`
counts. A second-pass unexpected error leaves outputs invalid with `written`
zero; complete preflight `required` counts remain available. Discard all output
on any error. The second pass still checks capacity and source statuses rather
than relying on earlier validation to justify unchecked writes.

Draw attribute spans continue to borrow immutable source bytes after return;
retain the input while using those spans. Copied vertex values own no source
pointers. Draws reference consecutive triangle-list ranges, so native upload can
use the stream without an additional fabricated index buffer. Backend upload,
material/texture binding and rendering remain the caller's explicit work.

No MT5 shared-source edits, CMake/build changes, runtime/parser execution,
rendering, tests or game asset copying occurred in this worker tranche. Lead
owns compilation and integration. This delivers supported geometry data to the
native boundary without claiming a visible frame, playable port or performance.
