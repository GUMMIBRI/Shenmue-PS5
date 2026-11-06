# Bounded MT7 hierarchy and raw geometry subset

Files: `include/cedar/mt7.hpp`, `src/mt7.cpp`. Original bounds/view code derived
from independently MIT ShenmueDK revision
`1dae443814b3755281d207a13f25571c0e51a5d1`, Copyright(c)2019 Phil. License was
read before new source; preserve `third_party/licenses/shenmuedk-MIT.txt`.
No restricted recovered game source or game assets were inspected or copied.

## Source boundary and actual incompleteness

The compatible MT7 reader contains useful real vertex/index layouts but is not
a complete model renderer. Its face construction/material assignment is commented
out. Its primitive field is explicitly unknown; even its primitive comment is
not a reliable topology specification. Color-bearing strides40/48 are named but
their extra bytes are not consumed correctly by the reader. Unknown commands
are skipped through a low-nibble length heuristic; a zero command may not advance.
Texture-entry code writes four bytes into ushort width/height fields, and its
later ID loop operates on copies. None of these bugs are reproduced as behavior.

This implementation delivers real bounded positions, normals, UVs and explicit
raw index records. It generates **no triangles**, default vertices, primitive
interpretation, material binding, fabricated mesh completion or unknown-command
skip-success. An accepted hierarchy alone does not establish valid mesh streams.

## Header, textures and hierarchy

`Mt7View::parse(bytes,size,workspace,capacity)` handles the source's standalone
base-zero profile. The16-byte header accepts MDCX/MDC7, MDPX/MDP7, MDOX/MDO7,
MDLX/MDL7 signatures. The declared size stays opaque; caller extent supplies the
bound. Texture-count records consume20 bytes each followed by a separate8-byte
ID per entry. `texture()` returns both as raw bytes, avoiding the unsafe typed
width/height copy and without interpreting texture-ID/hash semantics.

If the texture section does not reach the first node, a bounded count-prefixed
uint32 node-offset table is exposed as raw values. No meaning is invented for
unused gaps, unknown fields or table entries. Node headers preserve real finite
position/scale values, raw rotation words and mesh/child/sibling/parent offsets.
Rotation storage disagrees between the compatible header and node implementation,
so it is not converted to angles or a transform matrix.

Child/sibling traversal uses caller workspace without recursion or heap. It
rejects repeated offsets, cycles and shared-node DAGs. Derived `parent_index`
reflects this traversal. Raw parent offsets are retained, not followed or forced
to equal the derived relation; the compatible code also builds parent links from
its traversal argument. Subnodes, animation/skinning matrices and transforms are
not evaluated. Workspace must remain alive and immutable after successful parse.
Failed parse invalidates the view and can leave a reported workspace prefix.

## Exact XB01 geometry subset

`geometry(node_index,view)` reads the real referenced XB01 stream. Its32-byte
header retains unknown words, center and radius. The compatible reader locates
the vertex header at `mesh+28+size_words*4` and commands at
`mesh+24+first_entry_words*4`; the source's command-loop limit is eight bytes
before the vertex header. These equations and every resulting extent are checked.
The initial20-byte group record is returned as raw group metadata. Its declared
size is not repurposed into an unproven general command/submesh walker.

The16-byte vertex header yields stride and byte count. Only stride12 (position),
24 (position+normal), or32 (position+normal+UV) is supported. Bytes must divide
the stride exactly; all decoded float fields must be finite binary32 values.
`vertex()` exposes actual values with explicit optional-field flags. There is no
inferred color, bone weight, normal normalization or default missing attribute.

`raw_commands()` exposes a bounded opaque span including its first group header.
`raw_indices_at(exact_file_offset,view)` parses only an explicitly selected0x10
record after that header. It validates the little-endian unaligned16-bit word
count,12-byte prefix, uint32 index count and every uint16 index against the real
vertex count. Word counts above32767 are unsupported because the source reads
through signed16-bit storage. Trailing record bytes and primitive word remain
opaque. The caller must supply a known record boundary; this accessor is not
proof that an arbitrary matching byte inside an unknown command is a record.

No topology, triangle winding, submesh boundaries or material texture references
are established. A downstream renderer must not treat these raw indices as a
triangle list or strip without separately proven primitive semantics.

## Work limits and verification status

Limits:256 MiB file,4096 nodes,65536 texture/table records, one million vertices
and one million indices per explicit stream. Traversal's duplicate scan is
bounded by the node cap. All offsets/counts are checked before reads. Float/index
validation is finite; source/workspace ranges are checked for wrap and overlap.
Source, view object and caller workspace are required to be distinct live storage.

No allocations, file access, extraction, renderer, runtime/fixture checks or game
assets were used. Lead owns batch compilation. This is source implementation of
a proven partial reader, not evidence of full ShenmueII scene/gameplay support.

## Pinned primary references

- [MIT license](http<LOCAL_REFERENCE>/LICENSE)
- [MT7 header and signatures](http<LOCAL_REFERENCE>/mt7.h)
- [Container/texture-table reader](http<LOCAL_REFERENCE>/mt7.cpp)
- [Node interface](http<LOCAL_REFERENCE>/mt7_node.h)
- [Hierarchy, XB01 vertices and raw index records](http<LOCAL_REFERENCE>/mt7_node.cpp)
