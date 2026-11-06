# Provenance and permissions

Core interfaces, lifecycle, ring, filesystem wrappers and presentation are authored
locally without reading restricted upstream implementation. Build scripts are
authored locally. PS5 input/audio ABI declarations are derived from licensed
public SDL PS5 headers at ee4c47dc0d617b3bc8f35108f9956baf228a1322, using retained
SDL3 adaptations under <LOCAL_REFERENCE>/src.
Their complete zlib notices and altered-source attribution are retained directly
in ps5_input.cpp and ps5_audio.cpp. They are not original Cedar ABI discoveries.
Archive parsing derives from separately licensed ShenmueDK format references;
see docs/TAD-PROVENANCE.md and third_party/licenses/shenmuedk-MIT.txt when delivered.
No restricted game implementation or proprietary assets are vendored.
Gzip uses the separately MIT-licensed miniz inflate-only dependency at
293d4db1b7d0ffee9756d035b9ac6f7431ef8492, with full license/source provenance under
third_party/miniz. DDS parsing/BC decoding is original code from published
Microsoft formats, documented in docs/DDS-SCOPE.md. These are asset/platform
facilities, not a license to reuse the restricted game's implementation.
The independently authored additions in this exported subtree have the scoped MIT grant in LICENSE;
existing source-informed and third-party notices are retained without relicensing.

Restricted research: LemonHaze420/shenmue at
d710dd3111059c246db310d01e99e8eaf1e2834d. LICENSE.md copyright 2026
LemonHaze (Dylan), all rights reserved. Existing notices stay untouched.
Its prohibition on unauthorized incorporation is an active integration blocker.
SEGA game/IP permissions are separate. Public availability is not a reuse grant.

External public tools are read-only dependencies, not copied into the project:
- Existing LLVM in PE1 GPU platform deps: Apache-2.0 WITH LLVM-exception; retained
  LICENSE.TXT is authoritative for the installed distribution.
- Existing public PS5 Payload SDK in the adjacent deps: upstream repository
  source license GPL-3.0; installed artifact identity and source checkout may
  differ. Do not assert a matching source pin without an actual receipt.
- Existing CMake and Ninja are invoked from <LOCAL_REFERENCE>/tools. Retain their bundled
  notices; no binaries are repackaged here.

Static archive compilation is not a linked SDK game distribution. Before future
packaging, enumerate actual linked components and preserve applicable notices.
No Sony proprietary SDK, credentials, game binaries or private saves are included.

## PE1 graphics transfer, November 5, 2026

Build18 reference root:
<LOCAL_REFERENCE>/.
Source-informed FSR1/display/movie adaptations retain MPL-2.0 attribution; full
platform MPL notice is third_party/licenses/PE1-platform-MPL2.txt. Do not infer
blanket MIT permission from individual old project notices. AMD algorithm headers
remain MIT under third_party/fsr1/LICENSE.txt. No PE1 game implementation, art,
private atlas/reference data, PS1 VRAM protocol or restricted Shenmue source was
incorporated. See third_party/fsr1/PROVENANCE.md and graphics comparison matrix.

## Authored dialogue state compatibility (GPL-3.0-only)

include/cedar/dialogue_progress.hpp and src/dialogue_progress.cpp adapt brynnb /
New Yokosuka's NativeDialogueProgressState.js at commit
b0df7f2720a7eecabce2e610fdedec35bf1243e3, source blob
6aa48f6564f9676b17e5920b7065989a3efa852e, SHA256
f0ddaf69e064edd33e3ad9959a1fe98c1bef4ff0de44f08c172a57c3d449a7aa.
Complete GPL3: third_party/licenses/new-yokosuka-GPL3.txt. No later-version
permission established. These two files are GPL-3.0-only, not original permissive
scaffold code. Future combined distribution must preserve applicable source and
license obligations; this tranche performs compilation/shared linking only.

Only authored state operations with caller-provided identifiers/routes are
adapted. Generated actor indices, game/story data and external recovered source
examples are excluded. Published repository metadata does not certify complete
independent lineage; no clean-room or original-engine-equivalence claim is made.
See docs/ENGINE-BASIS-NEW-YOKOSUKA.md and docs/DIALOGUE-PROGRESS.md.

## AFS and SRF resource profile (GPL-3.0-only)

AFS header/table indexing and SRF subtitle/timing decoding adapt the authored
parse_afs / parse_srf / parse_srf_timing functions in New Yokosuka
 tools/scripting/extract_dialogue_inventory.py at the same pin above.
Extractor Git blob7029f4b7ec34cdf4cb8d8f12bc25a582dd589649,17186bytes,
SHA256526deac8f4a3cb8050dd3c4bda8787c7a344976a2c8dd89de16e66683747fa7f.
The new afs and srf headers/implementations retain GPL-3.0-only notices and the
complete license. Only authored format rules are adapted; no generated tables,
original dialogue text, voice/archive assets or external recovered C is copied.
See docs/AFS-SCOPE.md and docs/SRF-SCOPE.md for bounded supported profiles.

## Isolated permissive audio codec

include/cedar/aica_adpcm.hpp and src/aica_adpcm.cpp adapt only the explicitly named
filtered-vgmstream AICA nibble math at vgmstream pin
6802153425adde134e49394fc653455166d7d2f2, src/coding/yamaha_decoder.c.
Complete permissive notice: third_party/licenses/vgmstream-yamaha-ISC.txt,
SHA256fcc26d35c4c4365e31f21734f846e4ee362ff0b57ba696085605a7b4768d8763.
Source identity and exact selected behavior: docs/AICA-ADPCM.md and
AICA-CODEC-EVIDENCE.md. No Flycast implementation branch, CPU/DSP/memory map or
emulator machine layer incorporated. Filtered codec is not claimed bit-exact to
Flycast's unfiltered profile or automatically correct for an unknown STR stream.

## Native lighting source

The normal packing/upload and ambient-directional lighting additions are authored
locally from explicit matrix/lighting requirements. No Shenmue game lighting,
emulator machine layer or PE1 title-specific light placement was copied. Existing
format/source notices remain in force. Project-authored licensing/publication
remains Cinder/owner's decision; no third-party GPL/MPL material is relabeled.

## Dialogue body/expression execution and voice profiles

GPL-3.0-only dialogue_body, dialogue_machine and dialogue_expression files adapt
authored New Yokosuka Body/Selector/Predicate behavior at the previously retained
pin; exact source blobs/hashes in docs/DIALOGUE-BODY.md and
DIALOGUE-EXPRESSION.md. Generated actor/message/spatial tables, external SEGA C,
restricted engine and original story corpus remain excluded. Host acknowledges
actual external work; missing state never becomes an automatic predicate value.

ISC SPSD view and SPSD-AICA producer adapt official vgmstream spsd.c and retained
AICA codec/dispatch/allocator rules, with exact receipt in docs/SPSD-SCOPE.md and
SPSD-AICA.md. They select an explicitly named filtered-vgmstream compatibility
profile, not universal hardware or PC-edition equivalence. No emulator machine
layer, loop playback, asset redistribution or original actor association added.

The SDL voice bridge is locally authored API composition, not copied SDL engine
implementation. It links the retained public SDL dependency and supported
vgmstream-compatible resource decoder; SDL's existing zlib notices remain in
its distribution. It never opens a playback device or imports machine emulation.

## World-state and actual event composition

world_clock is a modified MIT server-clock adaptation with verbatim notice at
third_party/licenses/new-yokosuka-server-world-clock-MIT.txt and exact source pin/
hash in WORLD-CLOCK.md. Its policy is explicit, not original story defaults.
GameState, GameEvents, ActorWorld, GameContext and numeric DialogueDispatch/Host
retain GPL-3.0-only authored New Yokosuka provenance in their component docs.
ActorRenderPoses and renderer rigid/normal rotation are local explicit math/data
composition, not borrowed game actors/assets or machine emulation.

reference_game_state uses only isolated scalar structural schema facts from
nativePredicateValues.generated.js at pinned b0df7f..., exacthash/scope in
STATE-BANK-LAYOUT-EVIDENCE.md and REFERENCE-GAME-STATE.md. No whole file, original
state/reset values, actor/story/spatial tables or native code were incorporated;
save offsets recorded as evidence are not applied to any file/parser. Edition/
platform/native-save compatibility remains unproved. The narrow inspection was
Yoko's interpretation of the owner's broad compatible-borrowing instruction,
not a separate explicit file-specific human grant.

SceneResourceView derives bounded authored CHRS/STRG recognition from NewYokosuka
GPL3 pinb0df7f2720a7eecabce2e610fdedec35bf1243e3 tools/lib/mapinfo_character_placements.js,
blob054dceb53dc8b7606a2c3e1afd86dba640a3c660, SHA256
004b3eeedd50d43eeac68b590a54b61cfdf204a2fde32da074b4f2619a161d75.
Retained GPL notice applies; no generated placements/defaults/external C included.
Exact scope and rejected heuristic source documented in docs/SCENE-RESOURCE.md.
StartupResources and scene_draws are original glue using existing format/GPU APIs.

StartupScene and SceneActors native composition retain GPL-3.0-only dependency
notices; no new upstream bodies or original data copied. See STARTUP-SCENE.md,
SCENE-ACTORS.md. Bounded independent mapping/decomp search provenance is recorded
in STARTUP-MAPPING-EVIDENCE.md and PREVIOUS-DECOMP-SEARCH.md; no restricted fork
implementation or unlicensed HDRE source inspected/incorporated.

