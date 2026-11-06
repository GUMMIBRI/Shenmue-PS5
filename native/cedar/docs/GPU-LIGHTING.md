# Optional native Lambert lighting

Original facility, no borrowed game or emulator code. `GpuRendererConfig::lighting` defaults to `unlit`; existing shader, vertex layout, camera, depth, blend, draw methods and SceneTarget usage remain unchanged in that mode. Explicit `ambient_directional` selects the new shader pair. This is a caller-driven light equation, not reconstructed Shenmue lighting or material behavior.

Lit draws require a ready mesh with a real world-normal stream in buffer slot 1, stride 12, attribute location 3. Existing GpuVertex remains slot 0, stride 24, locations 0 position, 1 UV and 2 packed color. Positions and normals must already share world coordinates; caller supplies the real camera/projection. No model transform, normal generation, inverse-transpose or light direction is invented. Direction-to-light must be finite and nonzero; CPU normalization uses double intermediates. Interpolated normals normalize safely; a cancelled zero interpolation receives ambient only.

Each `GpuDraw::light` supplies linear-light ambient/diffuse RGB in [0,1], direction-to-light, explicit texture transfer and vertex transfer. The shader computes texture × vertex × (ambient + diffuse × max(N·L,0)), clamps RGB to [0,1], and preserves sampled alpha × vertex alpha. Ambient plus diffuse may exceed one; clipping is explicit SDR behavior. No HDR/PBR/shadows/point lights, energy conservation or quality/performance claim is made.

## Transfer and alpha contract

Known `linear_sdr` and `perceptual_sdr` choices are mandatory for lit input texture, vertex color and configured output. Perceptual means the sRGB transfer curve, not arbitrary engine gamma. Supported texture/target formats are RGBA8/BGRA8 UNORM and native SRGB variants only. Perceptual texture metadata requires an actual native SRGB format so hardware decodes before filtering. Linear texture metadata requires non-SRGB UNORM. Mismatches are rejected; no post-filter manual texture decoding is used. Perceptual vertex RGB decodes in the vertex shader before interpolation, keeping gradients linear-light. Alpha is never gamma converted.

Native SRGB targets encode the shader's linear output and require perceptual output metadata. UNORM targets store linear output directly or manually encode perceptual output as declared. Thus native and manual conversion never both apply. Unknown transfer, contradictory native SRGB tags and other/HDR formats are rejected before opening the render pass. Target format is the renderer's explicit contract; raw externally supplied texture identity/format is caller-owned because SDL exposes no texture-format query here.

Lit premultiplied-alpha mode is rejected: no producer premultiplication domain is inferred. Opaque and straight-alpha are supported, with one restriction: perceptual straight-alpha output requires a native SRGB target for linear-light blending; manually encoded UNORM perceptual output is opaque-only. Linear UNORM output supports straight-alpha directly. Source textures/colors for lit mode must be straight/unassociated RGB; caller owns that semantic assertion. Existing unlit premultiplied mode remains available.

## Shader and lifetime integration

Generated `cedar_mesh_shaders.hpp` must expose `cedar::shaders::lit_mesh_vert` and `lit_mesh_frag`. Vertex shader: two UBOs, a 64-byte camera at set1 binding0 and an 80-byte block at set1 binding1 (vec4 transfer flags followed by a column-major mat4 normal rotation). Flag x enables vertex RGB sRGB decoding; unset normal rotation uploads identity. CPU pushes both per draw. Fragment: sampler at set2 binding0 and one 64-byte std140 UBO at set3 binding0: four vec4 values direction, ambient, diffuse, flags. Fragment flags x/y/w are unused zero; z enables manual output encoding. No storage resources or additional uniforms.

This object still borrows the device and records into caller-owned commands; it does not submit, wait, cancel, acquire or present. The device and actual resources must outlive command use; shutdown uses SDL's deferred releases. No settings UI, automatic original-light binding or engine gameplay call site is added. Source review only; shader generation, batch compilation/link and human visual qualification belong to the lead. No runtime/tests/assets were used for this batch.

