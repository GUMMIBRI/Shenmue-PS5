# Completed MT5 geometry to GPU streams

`pack_mt5_gpu_vertices` converts an already completed expanded MT5 triangle list
to the shared 24-byte `GpuVertex` layout. The caller supplies the input count,
output buffer/capacity, explicit color policy and optional explicit fallback
color pointer. There is no mesh creation, default white, guessed material,
texture binding, rendering, upload, allocation, file/asset access or SDL call.
The header includes the exact lead-owned `mt5_geometry.hpp` and `gpu_types.hpp`;
neither was modified.

## Exact conversion

World-position XYZ and the geometry's actual translated UV values are copied
unchanged. Values must all be finite. Finite UVs outside 0–1 are retained; no
clamp, mirror, coordinate-system, projection, unit, pose or material transform
is invented. Any `has_uv=false` vertex is explicitly unsupported with its source
array index. A zero-filled default UV is never substituted.

Source color is explicitly BGRA, so bytes map to GPU RGBA as source indices
2,1,0,3. Alpha is preserved. No gamma, premultiplication, lighting or normal
conversion is applied. The original geometry's world/local positions, raw and
legacy normals, source indices and other metadata remain untouched. The unlit
GPU layout does not consume or reinterpret those normals.

Color policy is required as an argument, with no default:

- `require_source`: every vertex must have source color; the first missing color
  reports `missing_color` and its source-array index. Fallback is ignored and may
  be null.
- `source_else_explicit_rgba`: source color wins wherever present; missing colors
  use the exact four RGBA bytes supplied by the caller. A nonnull fallback pointer
  is mandatory even if this input happens to contain every source color. The
  function does not invent a fallback or infer material appearance from it.

## Complete preflight and storage

Count must be nonzero and divisible by three. Caller storage must contain that
many fully valid `Mt5GeometryVertex` objects, remain immutable throughout the
call, and provide an aligned mutable output range for its declared capacity.
Multiplication/address overflow and alignment are checked before access. The
entire declared output-capacity span must be disjoint from the entire source
span and, when used, the four fallback bytes. In-place packing is rejected.
The function cannot prove that arbitrary caller pointers actually reference
valid allocated storage; that remains the caller's obligation.

All source vertices and storage ranges are preflighted before any output write.
Missing UV, missing required color, non-finite values, invalid configuration,
overflow, overlap or insufficient capacity leave the output unchanged. Color
fallback can overlap immutable source storage but cannot overlap output.
No error-producing operation remains in the write pass under the caller's
immutable-storage contract; no partial output is represented as valid.

`GpuPackResult::required_vertices` is nonzero only after complete successful
source/range preflight. It is available on `insufficient_capacity`, while
`written_vertices` stays zero. A null output with capacity zero may be used for
that validated capacity query; there is no successful zero-vertex mesh. On full
success both counts equal the complete input count. Other preflight failures
report zero counts. `vertex_index` is the first offending source-array position
for per-vertex errors, or the `no_vertex` sentinel for general failures.

This packer does not impose the GPU upload facility's separate vertex-count
limit beyond overflow/caller capacity constraints. The application must respect
`GpuMesh::max_vertices` when uploading a packed range, and retain draw/texture
selection outside the flat vertex conversion. Draw records, material indices
and raw core geometry are never rewritten.

## Optional lighting normal stream

`pack_mt5_gpu_normals(model, source, count, output, capacity)` produces a separate
12-byte `GpuNormal` per expanded geometry vertex. The existing 24-byte vertex
layout and unlit conversion remain unchanged. This CPU operation does not enable
lighting or change the caller's shading policy.

MT5 uses row vectors. For world linear transform W, the normal is
`nWorld = nLocal * transpose(inverse(W))`, followed by unit normalization.
Consequently output X is the dot product with inverse row 0 (elements 0,1,2),
Y with row 1 (4,5,6), and Z with row 2 (8,9,10). Translation is excluded.
The input is `raw_local_normal`, and the inverse belongs to the vertex's actual
`source_node`. Linked geometry therefore uses its source node rather than the
strip/draw node. `source_legacy_transformed_normal` is never consumed.

The model must be parsed. Each source node must exist, own mesh vertices, and
contain the supplied local vertex index; its global base plus local index must
equal the geometry's global vertex index. These checks establish reference
consistency, not authenticity of caller-produced geometry. Raw normals and
inverse matrices must be finite. Missing inverse availability or an exactly
singular inverse linear matrix fails explicitly; no arbitrary near-singularity
threshold is imposed. Zero transformed normals fail rather than acquiring a
default direction. Singular nodes remain usable through the existing raw/unlit
facilities.

Products, determinant and normalization use double precision. Dividing the
transformed normal by its largest absolute component before computing length
avoids overflow/underflow in the sum of squares. The resulting float3 must also
be finite. Nonuniform and negative scales use the same inverse-transpose rule;
no winding, face orientation or normal sign adjustment is guessed.

Count is nonzero and divisible by three, matching the expanded triangle stream.
All vertices, references, matrix arithmetic, buffer sizes and capacity are
preflighted before writing. Output's entire declared capacity span must be
disjoint from source vertices, the model view object and its used contiguous
node workspace. The public view does not expose the borrowed model-byte span;
the caller must additionally keep that underlying storage disjoint. Input,
model bytes and node workspace must stay valid and immutable throughout both
passes. Arbitrary pointer allocation validity remains the caller's obligation.

The shared result/count and capacity-query contract above applies. Per-vertex
errors identify the first offending input vertex. Missing nodes, inconsistent
source references, unavailable/singular inverses and zero normals have distinct
errors. No partial output is valid; every failure under the immutable-storage
contract occurs before writes. No texture, color, UV or legacy-normal rule is
silently substituted for a lighting normal.

## Delivery boundary

Only `include/cedar/gpu_pack.hpp`, `src/gpu_pack.cpp` and this contract were added.
The lead owns target integration and compilation. This worker performed no
compilation, runtime test, GPU/SDL operation, asset read or console action.
