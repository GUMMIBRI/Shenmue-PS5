# Explicit caller-owned dialogue state

`GameState` supplies bounded actual storage for the dialogue expression provider
and acknowledged bank2 set/clear requests. It deliberately has no fixed native
bank layout. The caller supplies up to three unique bank IDs2/3/4, explicit
byte/LSB-bitfield representation, logical capacity, actual mutable data span,
actual mutable knownness span and writable permission. Default construction
has no banks; descriptor kind defaults to unspecified and is rejected.

## Source and license

Header/source are GPL-3.0-only, source-informed by authored
[`NativeDialogueState.js`](http<LOCAL_REFERENCE>/NativeDialogueState.js)
at revision `b0df7f2720a7eecabce2e610fdedec35bf1243e3`, blob
`5b017d1486da11ff06c03413e04ae1aa2208484c`, UTF-8 SHA256
`668ff301e3c5fe751d4e3165e06d69dafc7b1107eb71baba9bbd27860e434527`.
Relevant authored behavior is `read`, `write`, `predicateContext` and copied
descriptor/storage validation requirements. Root GPL was read first, with no
conflicting file-level notice; retain
`third_party/licenses/new-yokosuka-GPL3.txt`. See `GAME-STATE.md` for the exact
missing generated-layout boundary. No generated definitions, story data,
external/recovered C or restricted material was opened/incorporated.

The explicit descriptor API, knownness tracking, strict failures and lack of
implicit initialization are this native facility's deliberate support profile.
They are not asserted to be original-game layout/reset/save semantics.

## Binding and lifetime

`bind(banks,count)` validates and copies descriptors, borrowing their storage.
The source descriptor array need not outlive successful binding. Capacity must
be1..4096: the maximum follows this adapter's existing unsigned12 expression
index domain, not a discovered native bank capacity. Data needs at least one
byte per byte entry or ceil(capacity/8) bytes for a bitfield. Knownness always
needs ceil(capacity/8) bytes, one LSB-first bit per logical entry. Larger spans
are allowed and their full declared extents participate in alias checks.

Every data/knownness span must be disjoint from all others, the descriptor array
and the `GameState` object. Pointer/address overflow, null storage, invalid
capacity/kind/ID and duplicate IDs are rejected before binding changes. Failed
binding preserves previous descriptors and never changes bytes. Successful
binding also never reads, initializes, zeroes or changes supplied storage.
`bind(nullptr,0)` or `unbind()` clears descriptors only. There is no implicit
reset, NewGame initialization or retained binding after explicit unbind.

Caller storage must remain allocated and appropriately accessible until unbind,
rebinding or destruction. All access requires caller synchronization; data and
knownness may not change during reads, writes or expression evaluation through
other aliases. Knownness storage is a real caller assertion: set bits mean the
corresponding stored value is initialized and available. This module cannot
validate that assertion's game provenance. Padding and excess bytes are retained.
Raw pointer allocation validity remains the caller's obligation.

## Bounded read/write behavior

`read(bank,index)` returns a typed status and unsigned-byte value. The value is
meaningful only for `ok`. Missing bank, invalid bank, out-of-range and unknown
entries are distinct statuses; none becomes knownzero. An unknown entry does
not read its data byte. A known bitfield value reads byte[index/8], bit[index%8],
yielding0/1. A known byte entry yields0..255.

`write(bank,index,value)` requires a bound in-range writable entry and value
0..1 for a bitfield or0..255 for bytes. Unlike the JavaScript wrapper, it rejects
out-of-range values rather than masking/coercing them or interpreting arbitrary
numeric truth. Only after all checks does it update the actual storage and mark
that entry known. Other entry bits and knownness bits are preserved. Failures
do not mutate bytes. No data or unknownness initialization is guessed, and
there is no API that silently resets all entries to zero.

`apply_bank2_write(instruction)` accepts only a decoded `state_bank_write`
descriptor with bank2, value0/1 and unsigned12 index. Its request object must
not overlap any bound data/knownness span. It applies the same validated write.
The caller passes its actual stable `DialogueMachine::pending()` instruction;
the facility cannot authenticate a fabricated C++ descriptor or pending token.
It never acknowledges the machine itself. The real host acknowledges the exact
pending token with success only after this method returns `ok`; failed writes
must leave that request pending. Repeating a valid set/clear request is
idempotent, but token ownership and other host effects remain caller-owned.

## Expression integration

`GameState::expression_provider(context,request)` accepts `context` pointing to
a live `GameState`. It handles state_bank requests with native value types2–4
only. Successful known reads return a resolved finite numeric double. Missing,
unknown, invalid or out-of-range reads and all other operand kinds return
unresolved, never resolvedzero. The typed `read` API remains available to expose
the underlying reason when the enclosing host wants diagnostics.

This adapter does not supply runtime/calendar/yen, actor or spatial values. An
enclosing real host provider can route state-bank requests here and other kinds
to separately proven state owners. During expression evaluation the object,
borrowed bytes and knownness remain read-only, with full opaque-context alias
obligations from the evaluator. No callbacks, clocks, event effects, serialized
save format, original native-save offsets or persistence IO are supplied.

Only the new API/source and this contract were written for implementation.
Yoko owns integration/compilation. No compilation, runtime, tests, game data or
console actions were performed by this worker.
