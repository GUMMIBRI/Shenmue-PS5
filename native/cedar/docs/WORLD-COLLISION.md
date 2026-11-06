# Caller-geometry planar collision movement

`step_world_collision` performs an actual bounded planar movement decision
against caller-supplied finite segments. A successful allowed step returns the
requested end X/Z and unchanged foot Y; a blocked step returns the actual
unchanged original position, a block reason and segment index. Invalid input
returns an error and leaves output unchanged. This is neither an always-true
callback facade nor a complete gameplay controller.

## Exact compatible authored source

New files are GPL-3.0-only, adapted from authored
[`src/ThirdPersonControllerRules.js`](http<LOCAL_REFERENCE>/ThirdPersonControllerRules.js)
in brynnb/new-yokosuka, revision
`b0df7f2720a7eecabce2e610fdedec35bf1243e3`, Git blob
`9a691bb4ce9d41035fd430e46a7ea0830ba9b0ac`, UTF-8 length24889 bytes.
Exact UTF-8 source SHA256:
`e8116f539a82d3774469eca2bade2c7e041424656b262e49509b091bd13e65f9`. Relevant
functions are `pointSegmentDistanceSquared2d`, `orientation2d`,
`segmentDistanceSquared2d`, `activeNativeCollisionSegments`,
`nativeCollisionDisplacementAllowed`, `nativeCollisionEscapeAllowed` and
`terrainTransitionAllowed`.

Root GPL version3 was read before authored-source inspection; this file has no
separate conflicting notice. Retain
`third_party/licenses/new-yokosuka-GPL3.txt`. The implementation is a modified
native C++ adaptation dated2026-11-05, not an unchanged original-engine routine.
It does not incorporate Babylon code, generated world/actor tables, collision
adjustment data, original defaults, game data or recovered/external C.

Targeted metadata located `play/world/WorldCollision.js` and
`play/world/NativeWorldCollision.js`; those authored adapters were inspected to
locate the real numeric collision boundary. Their filename/resource selection,
native/browser coordinate transforms, default dimensions, behavior codes,
generated JSON loading, door matching and Babylon mesh creation are excluded.
The standalone movement rules above supplied the implemented math; no other
engine/base survey was undertaken.

## Explicit caller inputs

Each segment supplies actual world X/Z endpoints, enabled/blocking flags and an
optional ordered finite vertical range. No runtime address, filename, behavior
code or scene identifier determines collision automatically. Caller must derive
these from real admitted geometry and actual state, apply moving-door/world
transforms beforehand, and explicitly establish the same axes/units as movement.
Degenerate segments are retained as point boundaries.

The move supplies actual foot X/Y/Z, requested X/Z displacement, positive player
radius/height, nonnegative radial epsilon less than radius, nonnegative vertical
tolerance and nonnegative segment-degeneracy squared epsilon. No numeric field
has a native/controller default. These values are chosen in caller world units;
the squared epsilon is in squared units. The source's demonstrated numeric
defaults are not silently selected. The native policy rejects invalid values
rather than coerce missing geometry or disable collision.

A boundary is active only when enabled and blocking. An explicit vertical range
must overlap `[footY-verticalTolerance, footY+height+verticalTolerance]` inclusively;
without a range, the segment is unbounded vertically. All segments are validated
even if disabled/nonblocking, so invalid latent geometry cannot become admitted
silently through a flag. The step is planar; there is no ignored requested Y
component, automatic gravity, grounding, staircase placement or fall behavior.

## Actual movement and escape rules

The source-established effective radial clearance is `radius-epsilon`. If the
start center is outside every active boundary's effective clearance, the entire
requested center segment must remain at least that far from every boundary.
This is swept movement, not an endpoint-only check that tunnels across a wall.

If the start penetrates any active boundary, escape is allowed only when every
currently penetrating contact becomes farther away by more than epsilon and
the center path stays farther than epsilon from that contacted boundary itself.
Every other active boundary still requires full effective swept clearance.
Thus escape cannot cross a contacted wall or enter a new wall/corner. No wall
is disabled for a frame and no penetration is automatically teleported away.
`escaping` records the start condition, including on a blocked escape attempt.
The first blocking segment in caller order is reported.

Point/segment distances clamp projection to the finite segment. The authored
segment-intersection helper has a concrete mathematical defect: its orientation
test reports every pair of collinear segments as intersecting, even disjoint
ones. This adaptation adds inclusive X/Z bounding-interval overlap before the
orientation intersection decision. Real crossing/touching/collinear-overlapping
segments retain zero distance; collinear-disjoint segments instead use actual
endpoint distances. This deliberate correction avoids false blocked movement
and is not a claim of byte-exact JavaScript behavior.

`world_terrain_transition` separately evaluates an actual supplied surface
height and actual face-normal Y against explicit maximum step-up/drop/slope.
Height delta must lie in `[-maxDrop,maxStepUp]`; normalY is clamped to[-1,1]
before acos and compared in degrees to the supplied limit0..180. Missing normals
are not replaced with Up. Callers must supply the Y component of a real normalized
face normal in their world convention. This pure eligibility result does not
perform a raycast or move the actor onto a surface.

## Numeric, ownership and output limits

All geometry, movement and policies use finite double arithmetic. Each supplied
numeric magnitude and accepted end X/Z must be at most1e100. This explicit
support bound rejects oversized coordinates before writes and keeps coordinate
differences, dot products, squared distances, orientations and policy arithmetic
well below double overflow. No value saturates or wraps. Tiny near-degenerate
geometry retains ordinary IEEE754 rounding/underflow behavior and the explicit
caller degeneracy threshold; no claim of exact real-number predicates is made.

At most65536 segments are accepted; count zero is an explicit collision-free
world selection, not inferred missing scene success. With nonzero count storage
must be valid/aligned. Full declared source span, move object and output span
have checked address arithmetic; output may not alias either input. All input
and geometry validation precedes result writes. Input/world state remain
immutable throughout the call, and all operations require caller synchronization.
Arbitrary C++ allocation/lifetime validity remains the caller's obligation.

On query error, `segment_index` identifies invalid geometry when relevant,
otherwise is the sentinel. On a valid blocked result the separate output index
identifies the first blocker; error remains none. No partial movement is ever
represented as accepted. This facility neither chooses sliding substeps nor
claims success of an external actor/world mutation. The real pose owner must
commit the returned accepted position and separately integrate animation,
rendering, dialogue-facing and authoritative world state.

Only new `world_collision.hpp/.cpp` and this contract were changed. Yoko owns
integration/compilation. No compilation, tests, runtime, assets, private files,
game data, CMake or Git operation was performed by this worker.
