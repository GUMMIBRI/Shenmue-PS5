# Bounded asynchronous raw archive reads

**Explicit-close lifetime requirement:** once a worker is created, obtain a
successful `stop` before destroying `AssetStream` or releasing the bound archive
set/backing objects. **A failed `start` can still have created that worker:** its
final mutex unlock can fail after creation. Always inspect `join_pending()` even
when `start` returns an error; a true value retains the complete object/archive
borrow lifetime. Never treat that error as proof that creation was rolled back.
Keep the stream object itself alive on failed join or
unrecoverable synchronization failure. Destruction is not recovery from a
violated lifetime contract: it neither detaches, kills, terminates nor retries
forever. Serialize lifecycle calls and destruction with all other APIs as
described below. No worker is launched by this documentation or source delivery.

## Ownership and real work

`start(const ArchiveSet&)` creates exactly one joinable native pthread using the
actual existing public SDK `pthread.h` declarations. It borrows an already-bound
archive set, its immutable ordered pointer list, all readers, their immutable TAD
buffers, mounted asset objects and root paths. None may be rebound, moved,
unmounted or destroyed until `stop` successfully joins the worker. This preserves
the existing external-file mutation/authentication limitations; it does not
authenticate content or make mutable backing files stable.

Each accepted request carries an explicit two-word key, entry-relative offset,
caller buffer and byte length. There are 32 total slots, shared by queued,
running and unread-completed jobs; no slot is reused until the owner retrieves
its completion. Pending requests execute FIFO, with no guessed game priorities,
directory scan, extraction, decompression, SDL operation or worker callback.
Caller buffers must be valid for their entire requested length, remain writable
and exclusively accessible to the worker until completion is polled (or worker
join establishes completion), and must not alias other active job buffers.

The worker performs actual `ArchiveSet::read_hash` calls in chunks no larger than
1 MiB. It advances entry offset and destination pointer only by confirmed copied
bytes. Errors retain the aggregate prefix from successful earlier chunks plus
the failing chunk's reported prefix. Completion includes the last nested archive
status, whether any archive call occurred, and the selected/error archive index
inside that status. No fake zero-fill or success occurs on short reads.
A zero-length job still performs one real key/range validation unless cancelled
beforehand. A request is not an atomic snapshot across chunks. Full-range errors
can occur after earlier chunks have already changed the output buffer.

## Tokens, completion and cancellation

Tokens are nonzero monotonically increasing 64-bit values, never reused in the
same stream object, including across normal stop/restart. Wrap exhausts token
issuance explicitly. `submit` clears the output token first. A nonzero output
token means accepted even if a subsequent mutex-unlock failure is returned;
retain that buffer and the stream's lifetime in that failure case. A failed
condition signal retracts the still-mutex-protected new queue entry before
publishing a token. Its numeric token is consumed but not reused.

`poll_completion(record,available)` retrieves at most one FIFO completion and
releases its slot. Always examine `available`: a valid record may accompany a
separate latched worker/synchronization failure status. With `available=false`,
the cleared record is not a job completion. A completion can report success,
cancelled, archive read failure, or worker failure; check its exact copied prefix
before using a failed job's buffer. A backend that contradicts its copied-byte
contract is reported as failure, retaining only the previously reliable prefix.

`cancel(token)` only requests cancellation under the queue mutex. Queued jobs
remain in FIFO order and receive their own records. Running jobs check requests
between synchronous archive calls; an in-progress call is never forcefully
interrupted. A final read already completed successfully remains successful even
if cancellation arrived during it. `already_completed` never changes a completed
record. `not_found` includes unknown and already-polled tokens; no unbounded
token history is retained.

## Synchronization and shutdown

Normal queue state, token assignment, slot ownership, cancellation requests and
completion records are protected by a native pthread mutex/condition pair.
Worker reads occur outside the queue mutex, allowing the owner to submit, cancel
or poll during synchronous I/O. Only lifecycle/fatal-reporting latches use
atomics, so a broken mutex does not require unsafe shared-state access merely to
report failure. `counts` snapshots bounded queued/running/completed counts under
the mutex; `state`, `join_pending`, and `fatal_pthread_error` are latch queries.
There is no allocated job heap, detached worker or C++ `std::thread` wrapper.

Serialize `start`/`stop` on one owning thread. Do not call them concurrently with
each other; other request/cancel/poll calls are synchronized while a started
stream is running or stopping. Initial start, final release and destruction
require exclusion from every other API call. Stop from the worker is rejected
using real `pthread_equal(pthread_self(),worker)` rather than attempting self-join.

`stop` marks stopping under the mutex, broadcasts, then joins. New submissions
are rejected; queued jobs and an unfinished running job become cancelled between
reads, while a successfully finished final read remains successful. Completed
records remain available after join. Stop can block on an in-progress filesystem
read and thread exit; the chunk limit is not a wall-clock latency guarantee.
Joining succeeds before the borrowed archive pointer is cleared. Normal repeated
stop calls are idempotent. Drain all records before restarting so no old slot or
caller-buffer ownership is silently discarded.

Every fallible pthread call in public operations is checked. An actual
`pthread_create` failure starts no worker; that is distinct from a later unlock
failure returned by `start` after successful creation. A faulted state and the
unrecoverable-sync contract also remain authoritative when the start result is
failure. Signal/broadcast/join failures are reported with their actual
pthread return code; the first fatal code remains latched. A failed broadcast
does not proceed to a speculative join that might wait on a lost wakeup. Failed
join retains `join_pending` and all borrows for explicit retry. No join success
is inferred from an error code or stale state.

If mutex lock/unlock or condition wait itself fails, the synchronization contract
is treated as unrecoverable. No speculative repair, unsafe queue access,
blocking-lock retry, detached thread, forced kill or safe-destruction claim is
made. Keep object/backing lifetimes valid and report the actual fault; this class
cannot promise recovery after the operating system's synchronization primitive
has broken. A nonzero fatal latch prevents new starts. It is not erased to make
a damaged primitive appear healthy.

After normal stop and completion draining, optional `release()` explicitly
destroys the pthread condition/mutex and reports their actual destroy errors;
partial destruction is retryable without re-destroying successful components.
It must be serialized with every other API call. Destruction after legal stop
performs ordinary best-effort primitive destruction whose errors cannot be
observed after the C++ object's lifetime; use explicit `release` when those
errors must be reported. Destruction with an active or synchronization-broken
worker remains a caller lifetime violation, not a supported cleanup strategy.

## Integration status

Only `include/cedar/asset_stream.hpp`, `src/asset_stream.cpp` and this contract
were authored. The lead owns target wiring and compilation. Required native
operations are pthread mutex/condition init/destroy/lock/unlock/wait/signal/
broadcast, create/join/self/equal plus existing archive/core imports. Their
executable providers must be linked from the selected compatible public runtime.
The worker performed no compilation, thread execution, fixture, runtime test or
console action for this source-only batch.
