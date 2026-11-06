# Explicit New Yokosuka predicate layout storage

`NewYokosukaPredicateLayout::predicate_data_v2_b0df7f` names one pinned authored
schema, not a universal Shenmue, Steam, Dreamcast or PS5 save profile.
`ReferencePredicateStorage` owns exactly these raw bytes and per-entry knownness:

| Bank | Representation | Logical entries | Raw bytes | Knownness bytes |
|---|---|---:|---:|---:|
| 2 | LSB-first bitfield | 1024 | 128 | 128 |
| 3 | LSB-first bitfield | 64 | 8 | 8 |
| 4 | Unsigned bytes | 32 | 32 | 4 |

Default construction initializes raw storage to zero and all knownness to
unknown. Those zeros are not readable as initialized state through `GameState`.
No original NewGame/default/reset value is asserted or imported.

`bind_reference_predicate_state(state,storage,writable)` builds the exact three
descriptors and calls the existing transactional `GameState::bind`. It changes
no storage or knownness and propagates its status. Writable permission controls
subsequent writes through `GameState`; the owner can still explicitly load its
own storage. All `GameState` lifetime, alias, status and synchronization contracts
apply. The owned storage deletes copy/move operations; it must stay at its actual
address and alive while any `GameState` borrows it. Unbind/destroy every borrowing
state before releasing storage. No destructor dereferences another owner.

## Explicit payload admission

`load_bank_payloads(storage,bank2,bank3,bank4)` requires three actual caller byte
spans of exactly128,8,32 bytes. They are bank payloads already selected by the
caller, not whole saves automatically interpreted at guessed offsets. The helper
performs no file read or profile detection. All spans must be valid/immutable
during the call and disjoint from the entire storage object. Input spans can
overlap each other because they are read-only. Null spans, incorrect sizes,
address overflow and overlap fail before any destination changes.

After full preflight the three real payloads are copied and every copied entry
is marked known. All knownness publishes after the copies; no failing operation
remains under the valid immutable-storage contract. The caller must serialize
loading with all bound readers/writers, so intermediate copy state is never
observed concurrently. Supplied bytes must be actual initialized values with a
real caller-selected meaning; this helper cannot authenticate their provenance.
Raw C++ pointer allocation validity remains the caller's responsibility.

`forget_reference_predicate_state(storage)` explicitly clears all knownness and
preserves raw bytes. This is invalidation, not an original reset. No implicit
forget/reset occurs during bind. Individual `GameState` writes later make only
their actually written entries known. There are no rates, clocks, actor IDs,
runtime values, story/native-command effects, serialization or native-save IO.

## Exact source and license

New header/source are GPL-3.0-only, retaining source-informed storage behavior
from authored `NativeDialogueState.js` and isolated structural facts from
[`play/data/dialogue/nativePredicateValues.generated.js`](http<LOCAL_REFERENCE>/nativePredicateValues.generated.js)
at revision `b0df7f2720a7eecabce2e610fdedec35bf1243e3`:

- Structural file blob `c8e51a9ca5ea723c1a65b531deb535eb46de4173`, length8648,
  UTF-8 SHA256 `9082cb9e9a40d3949349e819dccd49ff78b6bbcebc44a3f44b959d87ed3e64a8`.
- Authored state implementation blob `5b017d1486da11ff06c03413e04ae1aa2208484c`,
  UTF-8 SHA256 `668ff301e3c5fe751d4e3165e06d69dafc7b1107eb71baba9bbd27860e434527`.

Root GPL version3 was read first; inspected file headers had no conflicting
notice. Retain `third_party/licenses/new-yokosuka-GPL3.txt`,
`STATE-BANK-LAYOUT-EVIDENCE.md` and `GAME-STATE.md`. This transcribes only bank
IDs, kinds, capacities and byte lengths. No whole generated file, runtime or
initial values, actor IDs, spatial tables, story corpus, native bytecode,
external SEGA C or restricted engine material was incorporated.

The source's `new-yokosuka-native-dialogue-predicate-data-v2` and
`new-yokosuka-dialogue-predicate-values-v6` are authored schema labels. Edition,
region and current Steam save compatibility remain unproven. Save offsets are
recorded only in the separate evidence receipt and are never applied by this
helper. The layout tag is an explicit caller choice, not evidence of a matching
original save or a newly granted right to game content.

Only these new helper files/contract were changed. Yoko owns integration and
compilation. No compilation, tests, runtime, private game data or console action
was performed by this worker.
