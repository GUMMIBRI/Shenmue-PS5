# New Yokosuka: bounded source-basis audit

Reviewed primary repository [brynnb/new-yokosuka](http<LOCAL_REFERENCE>/new-yokosuka) at exact main commit `b0df7f2720a7eecabce2e610fdedec35bf1243e3`. GitHub metadata reports a non-fork repository; commit author brynnb, 2026-10-10, “Verify splash uses a completed audio activation gesture”. Metadata and attribution support the published project identity, but do not certify every file's independent provenance.

## Grant and boundaries

The [root GPL v3 license](http<LOCAL_REFERENCE>/LICENSE) was read before implementation source. A complete copy is retained as `third_party/licenses/new-yokosuka-GPL3.txt` (SHA256 `5528ef9fa024cacb7625b0c5a0750767d8ce07fb2c38f4c7e641208e4a29f71c`). Conservatively use GPL-3.0-only: no later-version grant was established. This grant does not relicense SEGA data or every external reference.

The [external reference notice](http<LOCAL_REFERENCE>/README.md) identifies preserved original-game C examples and imported wiki material, with inconsistent attribution and rights separate from the root license. Those examples, source trees and game data were not opened or incorporated. The [README](http<LOCAL_REFERENCE>/README.md) credits Wudecon/ShenmueDKSharp through LemonHaze420/wudecon for findings. That credit does not itself establish use of the separately restricted LemonHaze420/shenmue repository. The bounded authored module below contains no direct import from restricted recovered code; entire-repository independent lineage remains unverified.

## What this project actually offers

README and [scripting architecture](http<LOCAL_REFERENCE>/architecture.md) describe a browser/Babylon client with a separate Go/Postgres service, exploration, dialogue/state, schedules, collision and motion systems. This is more than a static world viewer. README explicitly says story progression is unavailable; combat remains proof of concept, with broader event/camera/cutscene coverage incomplete. It is not evidence of a complete faithful playable engine.

Architecture distinguishes authored state/interpreter/presentation code from recovered evidence and generated dialogue/database content. [Scene-bytecode research](http<LOCAL_REFERENCE>/scene-script-bytecode.md) warns against treating a community opcode table as universal Dreamcast SCN3 compiled-SH4 semantics. No generic VM or unknown-command skip is justified by this audit. Default development consumes hosted game assets and install-time emulator components; neither install nor runtime nor those downloads were performed.

## Narrow permitted implementation candidate

[play/dialogue/NativeDialogueProgressState.js](http<LOCAL_REFERENCE>/NativeDialogueProgressState.js) provides a self-contained authored 325-record byte-state class. Source blob `6aa48f6564f9676b17e5920b7065989a3efa852e`; SHA256 `f0ddaf69e064edd33e3ad9959a1fe98c1bef4ff0de44f08c172a57c3d449a7aa`. Its file history exposes a brynnb bulk import at `5c76c81d460a47a4ec6899d063dabf06112b8af3` (“Bring New Yokosuka client up to date”, 2026-09-07), not finer origin evidence. No conflicting file-specific notice was found. Root GPL source grant and project attribution support this bounded adaptation, without certifying the original-game claims in its comments.

The generated index import is needed only by identity helpers/exports; those are excluded. Caller-provided numeric record identifiers, snapshots and routes suffice for the core. Neighbor `NativeDialogueState.js` depends on generated bank definitions and `NativeDialogueProgressIndex.js` on generated actor/dynamic identity data; neither is incorporated. The delivered `dialogue_progress.hpp/.cpp` retains only checked offset state, raw auxiliary snapshot bytes and the explicitly supplied-route completion calculation. See `DIALOGUE-PROGRESS.md` for the exact supported contract and stricter bounds.

This is a useful authored state primitive with clear license retention. It does not supply original dialogue, actor mappings, event triggers, story progression, native save compatibility, renderer bindings or a full game engine. No game data, assets, external C examples, restricted source, clone, runtime or tests were used. Further engine integration needs separately proven producers and semantics; this finite audit and module stop here.
