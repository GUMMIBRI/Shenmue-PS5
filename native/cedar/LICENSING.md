# Component licensing and publication boundary

Independently authored abeerbeen contributions in this subtree are offered under the MIT grant in LICENSE, following the owner preference for a flexible license and authorization to submit this source. Existing licensed/source-informed contributions retain their own licenses; this is not a blanket MIT relicensing. Do not infer MIT permission from third-party dependencies or treat a public export as a grant over SEGA content. No original game binaries/assets/data or restricted upstream implementation are included.

Retain all file-level notices and full component license texts:

- GPL-3.0-only: source-informed New Yokosuka dialogue/state/resource/scene/actor adaptations and their marked compositions. Exact pin, file hashes and exclusions are in PROVENANCE.md and technical docs. Full GPL: third_party/licenses/new-yokosuka-GPL3.txt. Generated story/actor corpus and separately restricted external C examples are excluded.
- MPL-2.0: marked source-informed PE1 non-game FSR/display/movie policies and shaders. Full retained platform notice: third_party/licenses/PE1-platform-MPL2.txt. File-level MPL notices remain; AMD's MIT headers do not relicense these files.
- MIT: AMD FSR1 headers under third_party/fsr1/LICENSE.txt; miniz under third_party/miniz/LICENSE; compatible ShenmueDK-derived components with third_party/licenses/shenmuedk-MIT.txt; adapted world clock with third_party/licenses/new-yokosuka-server-world-clock-MIT.txt. Exact components/pins are documented, not a claim that every Cedar file is MIT.
- ISC: marked vgmstream-informed Yamaha/AICA/SPSD components; full notice third_party/licenses/vgmstream-yamaha-ISC.txt.
- zlib: adapted SDL public PS5 input/audio declarations retain complete license/altered-source notices directly in their source files. SDL3 itself is external, not vendored here.
- glslang license text is retained at third_party/licenses/glslang-LICENSE.txt as tool provenance; no glslang executable is shipped.

No hooking/injection, emulator CPU/kernel, SDK/tool binary or proprietary game payload is part of this export. Exact external-source lineage limitations remain candidly documented. A combined distribution must comply with its applicable component obligations and preserve each existing file/component license. The subtree grant does not apply to parent repository material.

