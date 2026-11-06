# Isolated dialogue-bank layout facts

Targeted structural fact check at brynnb/new-yokosuka revision
`b0df7f2720a7eecabce2e610fdedec35bf1243e3`. Under the owner's broader instruction
to borrow compatible components, Yoko limited this inspection to structural
bank-layout facts. The owner did not name this file or these fields. Root GPL version3 was already read
and is retained in `third_party/licenses/new-yokosuka-GPL3.txt`. This receipt
does not relicense generated data, establish ownership of game content or expand
restricted-source permission.

## Exact source and inspection boundary

[`play/data/dialogue/nativePredicateValues.generated.js`](http<LOCAL_REFERENCE>/nativePredicateValues.generated.js)
has Git blob `c8e51a9ca5ea723c1a65b531deb535eb46de4173`, length8,648 bytes and
UTF-8 SHA256
`9082cb9e9a40d3949349e819dccd49ff78b6bbcebc44a3f44b959d87ed3e64a8`.
The exact pinned file was fetched into memory for hashing and structural
selection; no whole-file content was copied into the project.

Its leading comments identify the generator as
`tools/scripting/generate_native_dialogue_predicate_data.py`, say not to edit by
hand, and describe regeneration from verified predicate evidence. There is no
separate license grant or conflicting notice in that inspected header. The
generator was not opened.

Inspection selected only `stateBanks.banks` fields bank, storage, capacity,
byteLength and saveOffset. Other property names were encountered while checking
the shape, but their values—including initialValue, runtime addresses, reader,
writer and reset routines—were not displayed, used or transcribed. Runtime
values, actor IDs, spatial records, story corpus and native bytecode were not
inspected. No external/SEGA C, restricted engine or private game files were read.

## Exact structural entries

| Bank ID | Declared storage | Logical capacity | Byte length | Save offset decimal | Hex |
|---|---|---:|---:|---:|---:|
| 2 | bitfield | 1024 | 128 | 304 | 0x130 |
| 3 | bitfield | 64 | 8 | 296 | 0x128 |
| 4 | byte array | 32 | 32 | 560 | 0x230 |

Each declared bitfield length matches ceil(capacity/8); bank4 has one byte per
entry. Combined with the separately reviewed authored `NativeDialogueState.js`,
bitfields use LSB-first bit numbering. Those authored functions use saveOffset
and byteLength for bounded import/export. This proves what that pinned tool's
schema declares, rather than proving that any particular owned save has the
same profile.

Targeted scalar-label selection returned schema identifiers
`new-yokosuka-native-dialogue-predicate-data-v2` and
`new-yokosuka-dialogue-predicate-values-v6`. The targeted edition/profile/platform/
region label selection returned none. These schema versions identify authored
tool data, not a proven Dreamcast/PC/PS5 edition, region or save revision.
Uninspected evidence fields or external materials are not inferred from them.

## Result and remaining profile uncertainty

The former statement that the permitted authored sources did not establish
fixed bank definitions remains accurate for that earlier two-file audit. This
new narrow authorization supplies the scalar schema facts above. It does not
establish native NewGame values, reset policy, actor/runtime bindings, save
checksums, headers, containers, edition conversion or current Steam save
compatibility. In particular, the save offsets must not be automatically
applied to arbitrary caller bytes without an independently selected compatible
profile and its bounds checks.

No code was changed. Any follow-up profile helper should explicitly name this
authored schema/revision, continue to require real caller-provided initialized
state/knownness, and avoid an implied original-game default or universal save
compatibility claim. Existing generic `GameState` remains descriptor-driven.
No compilation, tests, runtime or game-data access was performed.
