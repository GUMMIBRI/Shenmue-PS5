# Explicit SDL3 GPU triangle-list mesh resource

`GpuMesh` uploads application-supplied vertices and optional actual normals to
SDL3 GPU vertex buffers. It creates no triangle, default normal, game actor, asset decoder, shader,
pipeline, texture, device, window, frame or game boot. This is original public-API
composition code; no SDL implementation or restricted game source is copied.

## Renderer consumption contract

The shared `GpuVertex` remains exactly 24 bytes: float3 position at offset 0,
float2 UV at 12 and RGBA8 bytes at 20. Source assertions enforce these offsets;
the original shared vertex type is unchanged. Renderer shader/pipeline attributes
must agree with that layout and interpret the color bytes deliberately.

Call `upload(device,vertices,count)` only while the mesh phase is empty. The
device is explicitly borrowed. Count must be nonzero, divisible by three, and no
larger than 1,048,576 vertices. The largest count under that cap divisible by
three is 1,048,575. Positions and UVs must all be finite; NaN/infinity are
rejected, while finite UVs outside 0–1 are preserved for the renderer's sampling
policy. Color bytes have their full original 8-bit range. No triangle topology,
winding, coordinate system or game meaning is inferred from the caller data.

The upload's success means **uploading**, not ready. Poll or wait for the actual
upload fence before using the resource. `buffer()`, `vertex_count()` and
`device()` expose values only while ready; otherwise they return null/zero.
The renderer must verify ready state and that `mesh.device()` matches its frame
device, bind the buffer as a vertex buffer with this 24-byte stride, and draw the
reported non-indexed triangle-list vertex count. No index buffer or instancing
scheme is fabricated. Shader/pipeline/draw-command ownership remains outside
the mesh.

## Optional real normal stream

`GpuNormal` adds a separate packed float3/12-byte type without extending or
changing the 24-byte `GpuVertex`. `upload_with_normals(device,vertices,normals,
count)` requires exactly one caller normal per triangle-list vertex. All
position/UV validation still applies. Both source arrays must be aligned,
nonwrapping and outside this resource object's storage, and every normal's XYZ
must be finite with at least one nonzero component. Every input is checked
before any GPU allocation. No normalization, face-normal construction,
inverse-transpose, pose, legacy translated-normal substitution or light guess
occurs here: the upstream caller/normal packer must supply correctly transformed
unit normals for its actual lighting model. Acceptance does not prove unit
length or source fidelity.

The normal buffer uses VERTEX usage and exactly 12 bytes per vertex. Its future
lit-renderer contract is vertex buffer slot **1**, stride **12**, FLOAT3 at
attribute location **3**, offset **0**, per-vertex rate. Existing position/UV/
color remains slot 0 at its original offsets and stride. `has_normals()` and
`normal_buffer()` are both ready-only: false/null while uploading, empty or
failed, and on a successfully uploaded vertex-only mesh. The unlit `upload`
entry point still allocates/copies only the original vertex buffer; it does not
invent a normal stream or enable lighting.

## Actual upload sequence

The implementation creates a `SDL_GPU_BUFFERUSAGE_VERTEX` buffer and an upload
transfer buffer sized to the entire bounded input. It maps the transfer buffer,
copies the original caller bytes, unmaps, acquires a command buffer, begins a
copy pass, records `SDL_UploadToGPUBuffer`, ends the pass, and calls
`SDL_SubmitGPUCommandBufferAndAcquireFence`. Source CPU storage is not borrowed
after `upload` returns, on either success or failure. There are no heap jobs or
callbacks; SDL owns its real native allocations.

With normals, one transfer buffer contains vertex bytes followed immediately by
normal bytes. Two actual upload copies target the separate vertex buffers in
the **same** copy pass, command submission and fence. The second transfer offset
is the vertex byte count (always at least four-byte aligned). Only that shared
fence completing makes both streams ready. Source normal bytes, like vertices,
are no longer borrowed when the upload call returns.

The unchanged one-million-vertex cap bounds vertex data at 24 MiB, optional
normal data at 12 MiB and their combined transfer at 36 MiB; all lengths/offsets
fit the actual SDL Uint32 API by static bound. Valid triangle counts reach
1,048,575 vertices. These are logical per-upload bounds, not a total driver/GPU
memory cap including backing/deferred resources, old meshes or other targets.

Submission invalidates its command buffer even if no fence is returned; the
implementation never tries to cancel/reuse it afterward. A failure before
submission cancels an acquired command buffer when appropriate; a cancellation
failure is retained separately in `cleanup_error()`. The copy-only command
never acquires a swapchain texture. SDL's upload/unmap/copy-end/release APIs
return void, so no fabricated per-command hardware-success result is reported.

`poll(ready)` returns success with `ready=false` while the real fence remains
unsignaled. Public SDL documents query false as unsignaled, not a separate error
code; stale `SDL_GetError` text is not interpreted as a query failure.
Query true releases the fence/staging transfer and sets ready. `wait_ready()`
blocks in `SDL_WaitForGPUFences`; only an actual successful wait sets ready.
Wait failure sets failed, preserves a native error string, hides the mesh from
renderers, and retains handles until explicit cleanup. No wait timeout or
guaranteed upload latency is claimed.

## Replacement, reset and lifetimes

Upload while uploading, ready or failed is explicitly rejected. The existing
resource is not silently discarded for replacement. Call `reset()` before a
new upload; this is deliberate destructive replacement, not a transactional
preserve-old-on-new-allocation-failure policy. Input validation rejected while
empty creates no native resources and can be corrected without cleanup.

Reset/destruction releases the owned fence, transfer, vertex and optional normal buffers through
SDL release APIs. SDL explicitly defers buffer/transfer freeing until safe.
Reset sets empty and hides the handles; it does not claim that GPU work became
idle or that an upload succeeded. Last error/cancellation text remains available
through reset and is cleared when a validated new native upload begins.
Void release calls have no cleanup result to report.

All operations belong to one owning thread; there is no thread synchronization
or background queue. The borrowed device must remain valid through object
operations and all pending SDL work, including deferred releases after reset or
destruction. The device owner must drain/resolve pending work before device
teardown. Stop using borrowed buffer handles immediately on reset; do not retain
them for future commands. Resource release is not a device-lifetime substitute.

## Integration evidence and limits

API signatures/return contracts were read from the existing public
`tools/pe-ps5-sdl3/20261029T010754Z-68fad8/src/include/SDL3/SDL_gpu.h` and adjacent
`SDL_error.h`. Compile `src/gpu_mesh.cpp` with the same SDL3 public include target
used by the other GPU facilities and link the real selected SDL GPU backend.
The lighting extension changes only `include/cedar/gpu_types.hpp` (adding
GpuNormal), `include/cedar/gpu_mesh.hpp`, `src/gpu_mesh.cpp` and this contract.
No CMake, renderer, shader or other shared file was edited. The lead owns the
matching normal packer/lit pipeline, integration and compilation.

This worker performed no compilation, upload execution, runtime fixture,
graphics test, asset read, console action, device launch or performance
measurement. Fence confirmation represents upload completion, not renderer
correctness, game geometry fidelity or a playable scene.
