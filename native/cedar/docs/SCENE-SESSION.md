# Lease-bound scene and actual actor lifetime

`SceneSession` owns a `StartupScene` and `SceneActors`, connecting actual required
archive reads/parsing to selected registered actors and real draw poses. This is
GPL-3.0-only composition of their retained authored profiles; preserve
`STARTUP-SCENE.md`, `SCENE-ACTORS.md`, `ACTOR-WORLD.md` and the complete
`third_party/licenses/new-yokosuka-GPL3.txt`. No new external research was used.

Construct with an explicit `ActorWorldAxes` convention. `open` requires the real
ready `StartupResources`, exact requirement index and encoding, complete caller
scene buffers, 1..64 explicit `(placement_index, GameActorKey)` bindings, a finite
nonzero camera matrix and real conversion/model-resolution services. No default
profile, actor handle, placement, model, world basis or starting scene exists.
Actor code is the caller's real four-byte code; it is separate from the parsed
object tag and may not be inferred from that tag. Duplicate selected placement
indices and duplicate live instance handles are rejected before scene IO or any
actor mutation. Indices must also fit the actual parsed record count before the
first actor is added.

## Ownership and sequential preparation

The startup scene acquires the actual resource lease, reads/parses the chosen
resource and requires nonzero placement records. The session then calls
`SceneActors::add` for each explicit binding in order, forwarding the real camera
and guarded converter/resolver callbacks. Converter output must supply actual
position/reference-position/yaw; the resolver must return the exact model name
and real mutable draw array as required by `SceneActors`. No missing service is
turned into success or fabricated transform. All ready-bound actors have actually
passed both world registration and draw-pose registration. Scene readiness does
not imply every parsed placement was selected or instantiate unspecified actors.

Every input descriptor/array and full scene-workspace extent is validated for
alignment, null/address/multiplication overflow and overlap with the entire
session, startup owner and each other. Startup's retained-root/index extent guard
also protects the entire session, binding array, camera, services descriptor and
all buffers. This catches overlap with siblings of embedded components, not only
with the immediate `StartupScene` or `SceneActors` subobject. Opaque service-context
starting addresses are checked; their full extents and referenced objects remain
the caller's disjointness obligation.

Converter/resolver calls go through this session's private context and execute
only during actor preparation. The wrapper also rejects every returned mutable
draw-array span overlapping the whole session/startup owner, full stored/decoded
scene buffers, gzip workspace, full placement-record workspace or any original
input descriptor/array/camera. Draw arrays may not overlap the converter/resolver
context starting addresses. Existing SceneActors/ActorRenderPoses checks cover
model-name equality, finite poses, draw limits, inter-actor draw-array overlap and
actual registration semantics. No registered draw storage is hidden in or allowed
to corrupt the borrowed scene representation.

Successful open copies no asset payload beyond the caller's existing read/decode
buffers and creates no actors other than the explicit bindings. The session keeps
the startup lease until all actors are removed. Camera, binding array and input
descriptors are needed only through `open`; scalar camera/keys/placements are
copied by the existing actors facility. Scene buffers, original bytes/strings,
draw arrays and their real GPU dependencies must remain alive and appropriately
immutable until successful close. Resolver/converter contexts must stay valid
through open and cannot mutate the session, resources, source buffers, input
descriptors or other components through aliases. Callbacks must not throw, retain
temporary argument pointers, reenter lifecycle/services, destroy components or
invalidate previously resolved draws. The API cannot validate a callback's claim
of real actor/model provenance or roll back arbitrary callback side effects.

## Failure, staged teardown and guarded services

Any scene-open or actor-add failure preserves the primary error, nested details
and failing binding index, then attempts cleanup. Cleanup **always** closes actors
first, retaining their borrowed placements and startup lease throughout. Only
after `SceneActors::close` succeeds does it close `StartupScene` and release the
lease. If either stage fails, the session remains nonready with
`cleanup_pending()==true`, retaining the remaining staged state for explicit
`close` retry. Preserve every resource and draw/GPU lifetime during this condition;
no new open can overwrite it. Cleanup diagnostics are reported separately from
the original open failure. Real draw matrices already written during preparation
are not magically reverted by this wrapper; underlying teardown restores the
explicit base pose before releasing draw ownership.

`close` invalidates session readiness immediately, closes actors in the existing
reverse registration order, then invalidates/closes the scene and releases its
lease. Empty close succeeds. There is no destructor callback or automatic recovery
through potentially dead borrowed owners: explicitly close before destruction.
The caller must finish external draws/GPU work that still uses these resources
before freeing them after close; this CPU composition does not wait on GPU fences.

`facing_services()` returns callbacks whose context is this stable session, never
the embedded actor object. A saved callback pair can be obtained before open,
but calls return unavailable until ready and during build/teardown/failure.
Ready calls forward to the actual SceneActors facing service and propagate its
real result; they do not fabricate actor existence. `read` and `resolve_tag`
likewise expose only ready state, with reentry/unavailable diagnostics otherwise.
Tags use the actual parsed object tags and retain missing/ambiguous results;
they never silently choose the first ambiguous actor. Generation identifies the
held startup generation. All calls and external draw mutation require caller
synchronization. There is no main loop, game boot, scheduling, camera inference,
movement simulation or broad gameplay success claim.

Yoko owns the `cedar_scene` integration linking the existing GPU and PS5/resource
targets. Only the three new session files were authored for this task. No CMake,
shared sources, compilation, tests, runtime, game data, Git or console actions
were performed by this worker.
