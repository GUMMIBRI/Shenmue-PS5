# Original bounded static/linked MT5 geometry subset

Files: `include/cedar/mt5.hpp`, `src/mt5.cpp`. This is substantive geometry access,
not a model-name inventory: supported streams yield actual local positions,
normals, strips, vertex indices, texture-index references, UVs and optional
vertex-color bytes. No game logic, executable or proprietary data is included.

## Compatible primary source and notice

The independent Shenmue-Mods/ShenmueDK MIT license was read again before this
source tranche. Exact revision: `1dae443814b3755281d207a13f25571c0e51a5d1`.
Copyright (c) 2019 Phil; complete notice remains in
`third_party/licenses/shenmuedk-MIT.txt`. No conflicting file-level license was
present in reviewed model files. No restricted LemonHaze code was inspected,
translated, copied or incorporated.

Reviewed primary sources:

- [MT5 definition](http<LOCAL_REFERENCE>/mt5.h) and [reader](http<LOCAL_REFERENCE>/mt5.cpp)
- [64-byte node definition](http<LOCAL_REFERENCE>/mt5_node.h) and [hierarchy/local transform reader](http<LOCAL_REFERENCE>/mt5_node.cpp)
- [32-byte mesh definition](http<LOCAL_REFERENCE>/mt5_mesh.h) and [position/normal/command reader](http<LOCAL_REFERENCE>/mt5_mesh.cpp)
- [Command definitions](http<LOCAL_REFERENCE>/mt5_instruction.h) and [strip/UV/color/material-reference reader](http<LOCAL_REFERENCE>/mt5_instruction.cpp)
- [Index mapping, transform composition and strip winding](http<LOCAL_REFERENCE>/model.cpp)
- [Matrix conventions, multiplication and axis rotation](http<LOCAL_REFERENCE>/matrix.cpp)
- [Row-vector position transformation](http<LOCAL_REFERENCE>/vector.cpp)
- [Rotation-unit conversion](http<LOCAL_REFERENCE>/math.h)

The Cedar parser is original allocation-free code, with explicit little-endian
loads rather than source stream structs, heap allocation or recursive traversal.

## Source-established layout and exposed geometry

Input begins at the MT5 file origin. The 12-byte HMDM header gives the end of the
geometry region and first node offset. Node mesh/child/sibling offsets are
model-relative. The compatible mesh reader seeks vertex/face offsets from file
origin; this implementation uses the same origin and does not guess relocation.
All consumed node, mesh, vertex and command bytes must lie in that geometry
region. Appended texture/other nodes are exposed only as raw bytes.

Each node exposes its exact fixed four-byte name, ID, opaque unknown word,
hierarchy and local SRT. Rotation words remain raw; degrees use their low sixteen
bits divided by 65,535 times 360, matching the primary helper's conversion.
Center/radius and opaque polygon-type metadata are preserved without inferred
render behavior. Raw vertex access remains local-space geometry.

Node metadata now also exposes the source-established static transform: row-major
matrix storage and row-vector multiplication, `S * Rx * Ry * Rz * T`, followed by
`local * parentWorld`. Axis rotations use `cos(-angle)` and `sin(-angle)` as in
the compatible source; translation occupies row 3. These are model hierarchy
transforms, not a guessed game coordinate conversion or runtime animation pose.
Intermediate/final nonfinite matrices fail parsing explicitly.

Vertex data is six binary32 values: local position followed by local normal.
No normal normalization, vertex welding, degenerate removal, runtime skinning,
scene reconstruction or coordinate-system conversion is guessed.

`static_vertex(sourceNode, index)` separately returns the raw local vertex, its
forward-transformed world position, and an explicitly named
`source_legacy_transformed_normal`. The compatible mesh reader applies
`transformPosition` to its normal, including translation; that exact legacy
result is preserved without calling it a valid lighting normal. No inverse-
transpose or alternate normal treatment is silently substituted.

Supported command subset:

- `0000`, `FFFF`: basic two-byte commands.
- `0002` through `0007`: length-prefixed attribute bytes, retained raw; known UVH
  and mirror-U/V bits are exposed. Other attribute semantics are not asserted.
- `0009`: raw material texture index; binding to external texture tables is not
  implemented. `000B`: UV divisor state.
- `0010`–`0014`, `0018`–`001C`: source-defined position/normal strip families,
  including their exact optional UV/color fields. Opaque strip words remain raw.
- `8000`: mandatory mesh command termination.

Signed strip lengths are converted to magnitude as in the primary reader, with
the sign preserved as metadata. Each corner preserves its raw signed index and
exposes an explicit global vertex index, source node and source-local index.
Positive indices must be inside the current mesh. Negative vertex counts are
unsupported. Unknown command families,
including source commands `0008`, `000A`, `000E`, `000F` whose semantics are
unknown, return `unsupported_command` with offset and command word.

### Proven static linked-index mapping

`NodeMesh` initializes its base from the global vertex-buffer length before
reading that mesh's vertices. This view records the same cumulative DFS base.
For a negative raw index, the compatible source requires a parent mesh and
computes `parentBase + parentCount + index`. A still-negative result is passed
to that parent's own mapping; a nonnegative result is a global-buffer index.
The bounded implementation follows those equations without unsigned wrap or
recursion, then maps the result to an existing node's exact vertex interval.
Monotone DFS intervals allow bounded binary search, including zero-count nodes.

This can identify an earlier nonancestor mesh: the source uses global-buffer
indices rather than enforcing that the result belongs to the immediate parent.
That behavior is preserved explicitly through `source_node`. Consumers must
fetch `vertex(corner.source_node, corner.vertex_index)` (or `static_vertex`) and
must not assume that a linked corner belongs to the current mesh. Missing parent
meshes, root-negative fallback cases and unmapped indices return
`unresolved_parent_index`; the source's fallback vertex zero is not fabricated.
This is static inherited/linked geometry, not completed runtime skinning or an
inferred weight/joint model.

UVs retain both raw words and the primary reader's normalized values. UVH uses
division by 65,536. Other UVs below 61,440 use the current nonzero UV-size state;
the primary reader's unusual high-word branch divides by 2^32 and is preserved
explicitly. Missing divisor state that would produce an undefined UV fails
`unsupported_uv_state`. Optional color data remains exact BGRA bytes. Triangle
expansion follows the compatible implementation: even strip triangles use
`[i,i+2,i+1]`, odd ones `[i,i+1,i+2]`, without invented geometry or cleanup.

## Memory, hierarchy and failure contract

`Mt5View::parse` requires caller-owned node records and traversal tasks. No
recursion or heap is used. Depth-first traversal visits children before siblings.
The view and node workspace become usable only on complete success; failures
may leave partial workspace records, which must not be consumed. The immutable
input AND node workspace must remain valid and unchanged throughout view use,
including all cursors and strips. Do not resume a cursor after an error.

Limits are explicit implementation limits: 64MiB input, 4,096 nodes, depth 256,
and one million aggregate vertices, corners, strips and commands. Separate
counters bound repeated command streams and empty strips. Caller capacities can
be smaller and return `workspace_too_small`. Source, output-node and task storage
overlap is rejected. All range arithmetic is checked before use. Local SRT,
mesh bounds, positions and normals must be finite.

Node metadata includes an optional affine world inverse. Determinant and inverse
arithmetic use double intermediates; singular/nonfinite determinants and inverse
values unrepresentable as finite binary32 or a rounded-to-singular inverse mark
the inverse unavailable. No
arbitrary identity inverse is substituted. Zero-scale/singular nodes remain
valid for raw geometry and forward transforms. Only the requested inverse-
dependent `world_to_local` operation returns `noninvertible_transform` when
unavailable. Forward/inverse vertex helpers zero outputs on failure and reject
nonfinite results. Successful inversion does not promise numerical conditioning
or cryptographic/format authentication.

Duplicate/cyclic node offsets and overlapping node records are rejected.
Declared parent offsets must equal the discovered parent (or zero at roots).
The compatible reader ignores that declared field, so models requiring looser
parent-link handling remain outside this deliberately narrow supported profile.
Shared-node DAGs are rejected rather than interpreted as a hierarchy. Distinct
nodes may still reference the same bounded mesh data; no unsupported uniqueness
rule is invented for vertex or mesh storage.

No file traversal, game-byte inspection/copying, parser execution, runtime tests,
rendering, CMake changes or compilation occurred in this worker tranche. Lead
owns compilation and integration. Animation, runtime skinning, unknown
command semantics, material completion and actual native scene rendering remain
explicitly incomplete; this parser does not establish playable game progress.
