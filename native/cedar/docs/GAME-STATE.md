# Dialogue state-bank provider boundary

Historical authored-code audit: subsequent isolated structural facts and a
reference-only storage binding are documented in STATE-BANK-LAYOUT-EVIDENCE.md
and REFERENCE-GAME-STATE.md. Edition identity, native reset and full save
compatibility remain unproved; the source operations below remain applicable.

Finite two-file authored-source inspection at brynnb/new-yokosuka revision
`b0df7f2720a7eecabce2e610fdedec35bf1243e3`. Root GPL version3 license was
already read and is retained in `third_party/licenses/new-yokosuka-GPL3.txt`.
Neither inspected file has a separate conflicting file-level notice. No
generated table, external/reference C, story data or installed game bytes were
opened. No runtime, tests or compilation were performed.

## Actual dialogue-bank provider

[`play/dialogue/NativeDialogueState.js`](http<LOCAL_REFERENCE>/NativeDialogueState.js),
Git blob `5b017d1486da11ff06c03413e04ae1aa2208484c`, UTF-8 source SHA256
`668ff301e3c5fe751d4e3165e06d69dafc7b1107eb71baba9bbd27860e434527`.
Relevant functions are `definitionFor`, `bytesFrom`, constructor, `read`, `write`,
`predicateContext`, `toJSON`, `fromNativeSave`, and `writeNativeSave`.

The authored code establishes these conditional operations:

- A definition selects either bitfield or byte storage. Bitfield index i uses
  byte `i >> 3`, bit `i & 7` (LSB first). Read yields0/1; writes set/clear that
  bit according to numeric truth. Byte reads return an unsigned byte and byte
  writes mask numeric input to8 bits.
- Read/write indices must be integral and within the definition's capacity.
  The reference returns0 for an invalid read and false for an invalid write;
  unknown banks throw. An isolated host provider must distinguish unavailable
  or invalid reads instead of fabricating resolved zero.
- Construction copies supplied snapshots at exactly the definition's byte
  length. Missing snapshots are zero-filled by the JavaScript policy. This is
  not evidence that native NewGame state is all zero or that unavailable input
  may be treated as initialized state.
- `predicateContext` supplies `readStateBank` and `writeStateBank` adapters.
  It supplies no runtime-selector/calendar/yen/actor values.
- JSON snapshots use schema `new-yokosuka-native-dialogue-state-v1` with bank
  byte arrays. Native-save import/export copy each definition's byte range,
  bounded against save size. The authored code does not contain the ranges.

**Blocking fact:** bank IDs, bank2/3/4 storage modes, capacities, byte lengths
and native-save offsets all come from
`NATIVE_DIALOGUE_PREDICATE_DATA.stateBanks.banks` in the imported generated file
`play/data/dialogue/nativePredicateValues.generated.js`. That import was not
followed. No authored local constant establishes those per-bank definitions.
Implementing fixed banks or native-save compatibility from this review would
therefore guess essential layout rules. The dialogue-body bank2 set/clear
command establishes a requested logical mutation, not bank2 storage width or
capacity.

## Other exact metadata candidate

The first inspected metadata candidate,
[`play/events/NativeGameStateRuntime.js`](http<LOCAL_REFERENCE>/NativeGameStateRuntime.js),
blob `ad5f6bc540c4c5d356207e56599a62ca702eea4d`, source SHA256
`4dbe641917f456a197bd1794a0badbe683262d7ae10485e3b1f4082e317049d2`,
is a semantic-handler adapter for selected byte/word native field requests.
It delegates storage to read/write callbacks and does not establish the
dialogue banks. Its embedded mode/address/code mappings were not transplanted.
It is not a reason to create an emulated address space or fabricated world
state. Inspection stopped after these two authored files.

## Result and explicit next boundary

No `game_state.hpp/.cpp` was implemented: source-proven fixed bank capacities,
widths, reset values and persistence layout remain unavailable within the
permitted authored-code boundary.

A useful original caller-state facility could instead accept explicit
caller-provided bank descriptors (ID, byte/LSB-bitfield representation,
capacity, actual storage and initialization/knownness), plus explicit supplied
runtime values. Missing/invalid data would remain unresolved, writes would
acknowledge only actual bounded storage mutations, and reset would require an
explicit policy rather than guess original defaults. No native-save offsets
or serialization compatibility would be implied. This is a proposal for Yoko's
scope decision, not an implemented or source-proven original-game layout.
