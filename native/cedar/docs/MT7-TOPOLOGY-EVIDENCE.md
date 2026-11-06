# MT7/XB01 topology: bounded investigation, still unproved

November 5, 2026. Reviewed two primary repository branches only. No topology
helper was implemented; existing `mt7.hpp` / `mt7.cpp` were not modified. No
runtime checks, binary disassembly, game-file reads/copies or recovered game
logic were used. This stops a specific unsupported model-rendering path rather
than substituting guessed triangles.

## Branch 1: independent ShenmueDKSharp implementation

Repository: Shenmue-Mods/ShenmueDKSharp.
Exact reviewed revision: `41db0cb49c2a9cee692a71ac691b96686aeaa04d`.
[MIT license](http<LOCAL_REFERENCE>/LICENSE)
was read before source. Its copyright line is literally `Copyright (c) 2018`
with no author name. No conflicting file-level license appears in the reviewed
files. No code from this branch was incorporated.

Primary relevant source:
[XB01.cs](http<LOCAL_REFERENCE>/XB01.cs).
Additional bounded context was read in
[MT7.cs](http<LOCAL_REFERENCE>/MT7.cs)
and the TODO
[FACE.cs](http<LOCAL_REFERENCE>/FACE.cs).

### Concrete implementation evidence

- The reader seeks XB01 vertices at `meshStart + 28 + Size * 4` and its initial
  command group at `meshStart + (FirstEntryOffset + 6) * 4`.
- Group metadata occupies 20 bytes: byte ID, little-endian 16-bit size, one
  skipped byte and eight 16-bit values. Zero words are skipped; on reaching the
  current group boundary, a new group is read.
- A `0x10` entry reads type at byte 0, 16-bit size at bytes 1–2, skips byte 3,
  then reads a 32-bit primitive/unknown word and a 32-bit vertex-index count.
  Indices are 16-bit values starting at byte 12. Its next command is sought at
  entry start plus `size * 4`.
- The high-level `0x10` branch unconditionally constructs a face whose type is
  `Triangles`, then copies those indices in their stored order to position and
  applicable normal/UV index arrays. It performs no strip expansion or winding
  change in that branch.

### Why this is insufficient for the requested topology proof

The actual `0x10` primitive word is called `Unknown`. Its only interpretation is
a speculative comment suggesting a strip type and associating value 5 with an
OpenGL token. The implementation never checks that value, never selects topology
from it, and gives no value table or supported-profile restriction. Unconditional
triangle-list export demonstrates this tool's behavior; it does not establish
that every MT7 `0x10` record is a triangle list or that any particular primitive
word selects that behavior. It supplies no explanation of winding/restarts or
other primitive values.

The node command reader also defaults unknown entries to low-nibble size skips;
that is not enough to assign their semantics. The source is useful evidence for
checked byte consumption but does not remove the rendering blocker. No mapping
of `5` to triangle lists has been added to native code.

## Branch 2: original community knowledge repository

Repository: Shenmue-Mods/Knowledgebase.
Exact inspected revision: `30ea1cf7b965db63a2d78e382f0a3f11a85b4ee5`.
Only commit/tree/license metadata was inspected. GitHub's license endpoint
returned 404 and the complete tree had no license file or MT7/XB01-named format
specification. No repository document body or source-code body was read or
reused. In particular, listed recovered-source directories were not opened.
This branch supplied no compatible licensed topology evidence.

## Precise remaining input

Need a primary author specification or independently compatible-licensed working
implementation that explicitly relates the `0x10` primitive word/profile to
triangle-list/strip/fan semantics, establishes stored-order winding and any
restart rules, and documents command transitions for that supported profile.
A source implementation that merely forces every unknown primitive into
triangles is insufficient. Existing validated vertex/index access can be reused
once that missing rule is established; it must not advertise invented geometry
in the meantime. No further research loop was started in this tranche.
