# Registered actor facing and actual pose-owner boundary

`ActorWorld` adapts the authored dialogue facing state and consumer from
[`play/characters/ScheduledActorRuntime.js`](http<LOCAL_REFERENCE>/ScheduledActorRuntime.js),
pin `b0df7f2720a7eecabce2e610fdedec35bf1243e3`, 88291 bytes, SHA256
`7b5ffc2c63efe5993fe204ed14e6a70ae17ec5d90f980f665206930eeebc6b72`.
Repository GPL3 was read before this authored file; it contains no separate file
license notice. The full retained license is
`third_party/licenses/new-yokosuka-GPL3.txt`. New header/source are GPL-3.0-only.
Only this authored file was inspected for the implementation; no imported math,
generated actor definitions/schedules, recovered C, placements or game assets were
followed. Repository licensing does not relicense original game data or resolve
all broader project lineage; existing engine-basis qualifications still apply.

## Actual source behavior

`setDialogueFacingTarget` (1393–1425) and `setDialogueFacingOffset` (1427–1461)
validate a nonempty instance, three finite values and an existing registered entry.
They replace target versus offset, copy source metadata and preserve activation
with `activate || previous.active`. `clearDialogueFacingTarget` (1463–1465) deletes
the stored request. These setters alone do not rotate a model.

The actual pose consumer (2416–2433) runs during actor update: an active offset is
added to current model root position, or the absolute target is used. The same
file's `scheduledActorFacingYaw` (322–338) computes `atan2(targetX-positionX,
targetZ-positionZ)`, returning no yaw for coincident horizontal positions. The
consumer sets the model root rotation to `(0,yaw,0)` and stores visual yaw.
Y position does not affect yaw. No smoothing, movement, collision or scheduling
is implied by this dialogue override.

## Native API and truthful partial state

Construct `ActorWorld` with explicit `ActorWorldAxes::y_up_yaw_z_toward_x` and
`ActorPoseServices`. This is the selected mathematical boundary, not proof that
all native resources use those axes. The caller must confirm/convert its real
positions, requests and pose consumer consistently. There is no automatic
Babylon X reflection, degree conversion, handedness inference or guessed model
forward adjustment.

Register only real caller-owned actors using a nonzero instance handle, four
non-NUL actor-code bytes, finite actual position and finite base yaw. Registration
copies state into a fixed 64-entry registry; it does not construct or discover an
actor. Duplicate live instance handles are rejected even with a different code.
All later lookup requires both handle and code. Missing actors return missing or
service-unavailable, never fake existence. No actor IDs or placements are supplied.

`facing_services()` returns actual `GameFacingServices` callbacks backed by this
registry for `GameEventFacing`. Direct requests are also validated: finite vector,
valid kind, actor-code match when present, and participant-code match for offsets.
Participant offsets activate as established by the enclosing staging contract;
other activation follows the request or previously active state.

An inactive request is genuinely **staged only**, matching the authored setter.
`has_facing`, `facing_active` and `pose_override_applied` distinguish stored state
from accepted real pose changes. A noncoincident active request calls the supplied
real `set_yaw` synchronously. Both set and clear callbacks are required before
installing any override; missing support returns unavailable without committing
the new registry state. Service `applied` must mean the actual model/rigid-pose
owner has accepted the override. A callback that merely stores another target or
acknowledges an absent actor violates this contract. No model or physics mutation
is claimed in the absence of that integration.

Coincident XZ requests keep the previous yaw and need no new pose call, just as
the source skips rotation. Their metadata may therefore be active with
`pose_override_applied == false`; success here means valid accepted facing state,
not a newly rotated model. Offset yaw is computed directly from the offset in
double precision, algebraically equivalent to position-plus-offset subtraction
without floating-point addition overflow. Absolute target differences and atan2
also use double. Source metadata is copied unchanged and never dereferenced.

`update_pose` takes the real owner-supplied position and base yaw, recomputing an
active absolute target or offset against that position. It does not move the real
actor; the enclosing engine controls position integration. An installed override
owns yaw until a replacement or clear; for coincident requests it retains the
prior override. This explicit override-ownership policy avoids guessing how the
native route/physics controller restores base rotation. Every committed pose
update records the supplied `base_yaw`, including while an override is active.
Once no override is installed, supplied base yaw also becomes the effective
registry yaw. `read` returns copied
state by value, with no writable internal aliases. Revisions begin at 1 and
increment on committed updates; overflow rejects before callbacks or mutation.

## Clear, failure and lifetime

`clear` on an existing actor with no request is an idempotent successful absence
check. A staged/unapplied request can be removed locally. An installed override
must first be removed by the real `clear_yaw`; failure/unavailability preserves
registry state for explicit recovery. Clear does not invent a reset yaw, move
position or restore an unknown route. On successful removal the real pose owner
must restore its latest explicit base yaw; the registry sets effective `yaw` to
its separately retained `base_yaw`. `unregister_actor` clears first and removes the
entry only on success. The destructor performs no callbacks; explicitly clear or
unregister while the real pose owner still exists.

All calls require serialized ownership, stable service context and live real
actors. Mutating callback reentry and reads during callbacks are rejected.
Callbacks must not destroy/reconstruct the registry, alter its storage, register
new callbacks, throw or invalidate their arguments. Callback arguments are valid
only synchronously; copy them if needed. Failure must mean no committed external
effect, or the actual pose service must reconcile its partially changed state
before retry. The registry commits its candidate only on success and cannot
roll back arbitrary external side effects or authenticate a callback's claim.

The remaining concrete engine link is a pose owner implementing set/clear yaw
overrides on actual registered actor rigid/model transforms, and invoking
`update_pose` after real position changes. This module does not insert callbacks
into the renderer, skinning, physics or schedule code. It supplies no game boot,
movement simulation, asset loading or actor instantiation. Lead integration must
add `src/actor_world.cpp` and provide that real owner. Delivery is source-only;
no runtime, tests, game assets or console actions were performed.
