# Cedar native source libraries

This export contains native C++ libraries and GLSL sources for explicit resource decoding, dialogue/state, actor ownership and SDL GPU facilities. It has **no executable or game boot target** and is not a playable Shenmue port. Original game data, restricted upstream implementation, SDK/tool binaries and generated shader headers are not included. Source implementation, compilation and human gameplay evidence are separate; this export makes no runtime or quality claim.

## Portable core build

Requires CMake3.24+, a C++17 compiler and a build backend. From this directory:

```sh
cmake -S . -B build
cmake --build build --target cedar_platform cedar_assets
```

The default host configuration builds static core/asset libraries only. It does not fetch dependencies or launch a program. The vendored miniz dependency is built with the retained restricted inflate-only configuration.

Optional presentation uses an independently installed compatible SDL3 header tree via `-DCEDAR_SDL3_INCLUDE_DIR=/path/to/SDL3/include`. The embedding application must link its actual compatible SDL3 library. Optional GPU targets additionally require caller-generated SPIR-V header directory `CEDAR_GPU_SHADER_INCLUDE_DIR` containing `cedar_mesh_shaders.hpp`; optional post effects require `CEDAR_POST_SHADER_INCLUDE_DIR` containing `cedar_post_shaders.hpp`. Shader source/resource contracts are documented alongside the facilities; a portable shader-header generator is not included in this export. No optional GPU build is claimed out of the box.

`CEDAR_PS5` is an explicit platform integration option requiring a separately supplied compatible public toolchain/SDK and headers. No proprietary SDK or local toolchain path is shipped. The generic build above does not enable it.

See LICENSING.md and PROVENANCE.md before reuse. Files carry mixed component licenses; independently authored additions have the scoped MIT grant in LICENSE; GPL/MPL and other component licenses remain applicable. Documentation placeholders such as `<LOCAL_REFERENCE>` replace private machine paths while retaining primary source URLs, pins, hashes and notices. PUBLIC-MANIFEST.json inventories exported files; PUBLIC-REVIEW.json records the bounded source/privacy review.

