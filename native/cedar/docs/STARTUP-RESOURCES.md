# Transactional startup resource ownership

`StartupResources` creates the concrete read-only resource-consumer path:
an already mounted `PosixReadOnlyAssets` → actual TAD reads into caller index
storage through `ArchiveMount` → an explicitly ordered `ArchiveSet` → required
hash/range bindings → tracked consumer leases. It does not boot a game, choose a
scene, discover files, normalize hashes or choose archive/patch precedence.
This is original composition of the existing facilities; retain their respective
format provenance and licenses, including `TAD-PROVENANCE.md`.

## Prepare and verification

Call `prepare` only on an empty owner. Supply 1..32 ordered archive plans, each
with explicit relative TAD/TAC paths and writable caller index storage. Earlier
archives have higher priority, exactly as `ArchiveSet` specifies. Each storage
capacity is nonzero and at most `TadIndex::header_bytes + max_entries * entry_bytes`
(33,554,492 bytes with the current profile). Actual TAD length must fit that
capacity; `ArchiveMount` performs the real file stat/read and binds its actual
TAC. This owner neither mounts nor unmounts the external root.

Provide 1..64 required resource descriptors. Each has an opaque unique caller
label, exact hash pair, nonempty raw entry range of at most 64MiB, optional exact
entry-size requirement and an explicit stored-byte verification kind:

- `raw_stored_bytes` verifies the selected entry metadata/range and actual
  readability of every byte in that required range. Signature size must be zero.
- `exact_signature` additionally compares 1..16 explicit bytes at the beginning
  of that range against caller-supplied bytes. This is a structural marker check,
  not a format decoder, decompression step, authentication or proof of game type.

Unspecified kinds are rejected. There are no inferred magic values, default
required resources, filenames, native structures, resource labels or NewGame
profiles. Even a recognizable signature is not reported as decoded gameplay.
Downstream consumers must use the actual bounded format/decoder module and
validate its complete input before claiming a loaded scene/audio/texture.

Before any write, preparation checks array bounds/alignment/address arithmetic,
all complete index-storage extents against each other, the entire owner, external
assets object, plan/requirement descriptors and every supplied path span. Caller
paths/descriptors need remain valid and immutable for the duration of prepare.
Index buffers may change once actual reads start, including after a failure.

Every required descriptor is resolved with fail-closed ordered `ArchiveSet`
lookup. Metadata and optional size are checked, then the full required range is
actually read in 4096-byte stack chunks. No payload is retained or extracted.
The selected archive is checked on each read. Errors identify the mount/archive
and/or requirement index, preserve nested backend status and report the actual
prefix read for a failed requirement. Signature mismatch reports bytes read,
not a successful decoded prefix. No user buffer is falsely described as valid
payload output from preparation.

Only after all mounts and requirements succeed are copied bindings, generation
and ready state published. The stable reader-pointer array resides inside the
noncopyable/nonmovable owner because `ArchiveSet` borrows that array. Bindings
retain the explicit requirement, selected archive index and raw TAD entry.
Any operational failure unbinds the set and closes every opened mount. These
mounts hold borrowed metadata, not persistent open TAC descriptors; closing does
not undo bytes already read into caller index storage. Preflight failure leaves
an empty owner empty. Prepare on an already ready owner is rejected unchanged.

## Consumer leases and teardown

`acquire` on ready state returns a lease containing this owner, a const pointer
to the real `ArchiveSet`, generation and unique nonzero lease ID. Up to 32 leases
may be outstanding. `binding(lease,index)` returns copied verified metadata only
for a live matching lease. `matches` lets scene consumers reject released/stale
plans. Successful prepare advances generation; lease IDs never repeat during
the owner's lifetime. Counter exhaustion reports overflow instead of wrapping.
An owner object's destruction/reconstruction begins a different lifetime; tokens
from that old lifetime must never be supplied to a new object at the same address.

The lease is an explicit lifetime protocol, not a capability or smart pointer.
Copying its public fields does not acquire another borrow. All users of a copied
lease share one borrow and must finish before its one successful `release`.
Double release, stale generation/ID and foreign owner/set pointers are rejected.
Do not retain archive/readers/binding-dependent views after release. A consumer
that starts an asynchronous worker must join/stop that worker before release;
this module creates no workers and cannot discover unregistered consumers.

`close` refuses with `borrowed` while any lease remains, preserving ready state
and mounts. After all releases it unbinds/closes and clears copied bindings;
closing an already empty owner succeeds. It never remounts or closes the borrowed
assets root. Explicit close is required before owner destruction and before
destroying/remounting external assets or changing/freeing index storage. There
is no destructor recovery for violated lifetimes.

Serialize all owner operations and synchronize them with consumer access. During
each lease, keep the owner, mounted assets and complete index storage alive,
unmoved and immutable; never direct a read output into those objects/buffers.
Read-only simultaneous downstream reads follow the underlying provider contract.
Raw pointer allocation validity and opaque label/resource provenance remain the
caller's responsibility. The POSIX provider reopens paths on each operation;
external file replacement/modification is not prevented by preparation or by a
lease. Matching length/signature is not stable-file or TAD/TAC identity proof.

Lead integration must add `src/startup_resources.cpp`, supply an actual startup
profile and pass acquired archives/bindings to real resource consumers. No CMake
or existing files were changed. No runtime, tests, assets reads, game boot or
console actions were performed during this source-only delivery.
