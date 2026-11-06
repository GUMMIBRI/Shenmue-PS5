# Required archive bytes to borrowed scene placements

`StartupScene` is a concrete synchronous consumer of `StartupResources`: it
acquires a tracked lease, selects the caller's exact required-resource index,
reads that binding's raw byte range into caller storage, optionally performs the
explicit gzip transform, and parses `SceneResourceView`. Ready requires a
successful parse with at least one actual placement. It does not choose an
archive, scene, filename, first entry, NewGame profile or game entrypoint.

The new pair is GPL-3.0-only composition of the authored recognition profile in
`scene_resource.hpp/.cpp`; retain `SCENE-RESOURCE.md` and
`third_party/licenses/new-yokosuka-GPL3.txt`. No new external source was read.
The existing gzip decoder uses its retained miniz MIT implementation and notices.

## Explicit inputs and validation

Call `open(resources, requirement_index, encoding, buffers)` on an empty scene.
The startup owner must already be ready. `requirement_index` addresses its
previously verified required bindings, not an archive index or TAD entry number.
No matching resource label/hash is guessed. The actual selected hash, range,
archive index and TAD metadata are copied from the live lease-bound binding.

Supply separate caller buffers for stored bytes and placement records. Record
capacity must be 1..16384, stored capacity 1..64MiB and large enough for the whole
required range. `required_stored_bytes` reports the needed size after binding
lookup. The encoding is mandatory:

- `plain_scene`: parse exactly the bytes read. Decoded buffer/capacity and gzip
  workspace must be absent.
- `gzip_scene`: require separate nonempty decoded storage of at most 64MiB and
  a real aligned `GzipWorkspace`. Call the existing `decode_gzip`, including its
  member CRC/size checks and rejection of unsupported/trailing data, then parse
  exactly its successful output length.

This encoding is a caller assertion about the actual resource profile, not a
filename/suffix/magic heuristic or an inferred TAD `unknown` field. Unspecified
or other transforms are rejected. No raw-deflate/zlib or implicit decompression
fallback exists. Signature checks are structural and gzip CRC is not
authentication. A stored-byte requirement alone does not establish scene type;
the actual scene parser still must succeed.

Before acquiring or writing, all full spans are checked for null/alignment,
size multiplication/address overflow and pairwise overlap: scene owner (including
its view), startup owner, input buffer descriptor, stored/decoded buffers, gzip
workspace and placement workspace. `StartupResources::storage_disjoint` also
guards the full retained external index capacities and assets object. The input
descriptor may not lie in mutable output storage. Open on an existing scene or
pending release is rejected before changing its view or buffers, protecting an
already borrowed view. Allocation validity beyond these span checks remains the
caller's responsibility.

After acquisition, the selected entry is re-statted and compared field-by-field
with the verified binding, including archive priority index. Its range is checked
again before one exact raw read. Changed selection/metadata is an explicit
`binding_changed` failure. The caller's optional exact startup signature is
rechecked on these actual bytes. Parsing uses only the selected range (or the
explicit decoder's output), never unrelated entry padding or whole-TAC bytes.

## Results, borrowed view and rollback

Results distinguish binding/resource, read, signature, gzip, parse and zero-
placement failures, retaining nested error details. `stored_bytes_read` reports
the exact raw-read prefix, including read failure. `decoded_bytes` is bytes
produced by gzip, including untrusted output on decoder failure; the nested
gzip status is authoritative. Parser diagnostics retain offset and required
record count. Modified buffers are not a valid scene after any failure.

On read/transform/parse failure the view is cleared and the startup lease is
released. Original failure remains primary; cleanup errors are retained separately
in `release`. If release fails, the token/owner remain retained and
`release_pending()` is true: keep the owner alive and retry explicit `close`.
No new open can overwrite that pending obligation. There is no fake rollback of
caller bytes already written and no automatic destructor call through a borrowed
startup owner.

On success `view()` returns the actual borrowed `SceneResourceView` only while
the lease still matches. Its records and strings borrow the caller workspaces;
keep all supplied buffers alive and immutable until close. Copies of records or
view pointers are also borrowed, not independently owned. The generation getter
identifies the startup generation for downstream scene consumers. Startup close
is blocked while this lease exists. Root/assets/index storage must also retain
their existing lifetime and synchronization contracts.

`close` first clears this view/readiness, then releases the lease. Success
invalidates all previously returned scene records/string views and leaves the
object empty. Empty close succeeds. Explicitly close before buffer reuse,
destruction, or destroying/remounting the startup owner/root. Serialize open,
view usage and close; there are no callbacks, hidden threads, network or GPU
operations. A caller retaining an invalidated copy cannot be detected.

The underlying provider reopens files; even re-stat, selected-index and signature
checks do not prevent same-size file replacement/content mutation or provide a
stable-file/authentication guarantee. A successfully parsed nonempty recognition
profile is resource readiness, not proof of a correct original starting scene,
story behavior, actor spawn, collision, rendering or playable game.

Yoko must integrate `src/startup_scene.cpp` and supply the actual required binding,
encoding and consumer. Only these three new files were authored. No CMake,
runtime, tests, Git, console actions or game-data reads were performed during QA.
