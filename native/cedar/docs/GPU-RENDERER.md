# Explicit native GPU facility

Original GLSL and SDL3 GPU orchestration; no recovered game rendering logic,
default scene, camera, material, geometry, texture, timer or process entry point.
This is an unlit textured triangle facility, not complete Shenmue rendering.
Lighting/fog, animation, game cameras, original material interpretation, effects,
skinning and scene/event integration are missing.

GpuVertex is exactly24 bytes: float3 world position, float2 UV, RGBA8 normalized.
The original shader consumes an explicit column-major clip-from-world matrix
with Vulkan depth0..1 and one actual texture/sampler. UV/coordinates are not
flipped or inferred. GPU packing requires source UV and an explicit source-color
or caller-fallback policy, preserving absent-data failures rather than white
textures or made-up vertices.

GpuRenderer requires a real device/swapchain format, explicit blend/depth/cull
configuration, uploaded ready meshes/textures on that device and bounded draw
ranges. It preflights the full draw list, records a render pass and never submits
or presents it. Every pass clears caller-selected color and its own depth target.
There is one pipeline state per renderer/pass, not a completed multipass material
system. Depth can be disabled or use LESS/LESS_EQUAL with writes; no bogus
read-only depth mode over an unshared cleared texture is advertised.

GpuContext owns its independent window/device, separate from the earlier SDL
surface presenter. Borrowed child resources must be released before device
shutdown. Prepare shaders/uploads/pipelines before acquiring frames where
possible. SDL forbids cancelling after swapchain acquisition: a later failure
does not trigger an implicit submit, present or unsafe cancel. The caller retains
that frame and decides the documented resolution. No automatic error frame exists.

build-gpu-shaders.ps1 uses the existing read-only glslang16.6.0 tool, prior source
pin e1b562a8bed273a02f30b59b66a5d499793cede5. Full multi-license notice is retained
at third_party/licenses/glslang-LICENSE.txt. Generated header, source/SPIR-V hashes
and compiler metadata are recorded under build/shaders. No compiler binary is
repackaged. CMake enables cedar_gpu only with explicit SDL headers and the
generated header directory. Compilation/linkage and device behavior are separate.
