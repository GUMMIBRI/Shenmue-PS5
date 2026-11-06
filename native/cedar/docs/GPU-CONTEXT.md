# Native SDL3 GPU context and frame ownership

Original `gpu_context.hpp` / `gpu_context.cpp` implement actual public SDL3 GPU
window/device/command-buffer operations. They consume the lead-owned shared
`GpuFrame` contract in `gpu_types.hpp`, without modifying it. No shader, mesh,
scene, camera, game frame, rendering demo or executable boot is supplied.

## Selected public API evidence

The existing read-only SDL source/header root is
`<LOCAL_REFERENCE>/src`.
Its `LICENSE.txt` contains the SDL zlib-style license and copyright notice; no
SDL implementation bodies are copied into this adapter. The implementation was
checked against that root's `include/SDL3/SDL_gpu.h`, `SDL_init.h`,
`SDL_video.h` and `SDL_thread.h`. The selected public `src/gpu/SDL_gpu.c` was
consulted only to confirm acquisition/cancellation ownership behavior, including
that a nonnull swapchain handle marks the command as having acquired a swapchain.
This is separate from restricted game-source research.

Required actual GPU APIs: `SDL_CreateGPUDevice`, `SDL_GetGPUDeviceDriver`,
`SDL_GetGPUShaderFormats`, `SDL_ClaimWindowForGPUDevice`,
`SDL_GetGPUSwapchainTextureFormat`, `SDL_AcquireGPUCommandBuffer`,
`SDL_WaitAndAcquireGPUSwapchainTexture`, `SDL_SubmitGPUCommandBuffer`,
`SDL_CancelGPUCommandBuffer`, `SDL_WaitForGPUIdle`,
`SDL_ReleaseWindowFromGPUDevice` and `SDL_DestroyGPUDevice`. Other imports are
the public SDL video init/query/quit, window create/destroy/ID, error retrieval,
main-thread query and current-thread-ID APIs. Lead owns compilation/provider
linkage; source/API availability does not prove a functioning runtime driver.

## Initialization and video lifetime

`GpuContextConfig` supplies dimensions, title, fullscreen/resizable/debug flags
and an explicit video ownership mode. The title is copied, must terminate within
255 bytes, and cannot be silently truncated. Dimensions must be positive.

`acquire_reference` obtains exactly one SDL video initialization reference and
releases exactly that reference on successful shutdown. `require_initialized`
checks that video is already initialized and never quits the caller's reference;
the caller retains it through context shutdown. No global `SDL_Quit`, event
route, timer or automatic process lifecycle is used.

Initialization creates its SDL window, requests a Vulkan GPU device with SPIR-V
support, verifies the selected driver/format capability, claims the window and
checks its real swapchain color format. It does not silently choose a software
renderer, another shader format or another GPU backend. SDL's claimed-window
default SDR/VSYNC policy is retained rather than guessing another composition.

`swapchain_format()` queries the actual claimed SDL swapchain format before frame
acquisition, allowing the external renderer to precreate its matching pipeline.
It returns INVALID when the context is unready or queried from another thread.
The borrowed `device()` is for explicit child-resource operations; external code
must not destroy it or manage this context's window/swapchain behind its back.

Initialization attempts paired rollback on failure. If rollback cannot establish
GPU idle, it retains all owned resources. `shutdown_required()` then remains
true even though initialization failed. The original cause stays in `last_error`
and the rollback cause is copied to `last_cleanup_error`. The caller must retain
the object/resources and explicitly retry close; a failed initialize return is
not proof that everything was released.

## Frame contract

All operations belong to the application's SDL main/video thread and the thread
that created this window. The application chooses its true main thread before
initial SDL initialization; SDL cannot infer application intent beforehand.
Creating-thread IDs and SDL main-thread checks are enforced for native operations.
The class is not concurrently callable. This includes shutdown/destruction and
queries of its borrowed ownership state.

`begin_frame(frame, drawable)` requires an empty caller-owned `GpuFrame`. Exactly
one frame may be outstanding. It acquires a real command buffer, then waits for
and acquires the window swapchain. A successful null swapchain (for example a
minimized window) cancels the unpresentable command using the permitted native
cancel path and returns `drawable=false` with an empty frame. No replacement
texture, fabricated dimensions or successful visible frame is invented.

An acquisition error can leave a real outstanding command; ownership is published
to the frame immediately after command acquisition. Inspect `frame_outstanding`
even when begin returns an error. If its swapchain is null, explicit cancel is
permitted. If nonnull, SDL's swapchain submission rule applies. Invalid acquired
dimensions/format also remain an explicit owned-frame error, not a fake null
acquisition. Such backend faults block further frame acquisition until explicit
shutdown/reinitialization; successful idle alone does not pretend driver recovery.

Submit/cancel require the exact original frame object address and its unchanged
device, command buffer, swapchain, format and dimensions. Copies, foreign frames,
tampered handles and wrong-thread calls are rejected without consuming the
actual owned frame. The frame object must remain alive and unmoved until it is
consumed. Close every render/copy/compute pass before requesting either operation.

**SDL forbids cancellation after acquiring a swapchain texture.** Consequently
`cancel` rejects that case without consuming it and without implicitly presenting.
The caller must explicitly submit it. A renderer recording failure does not
authorize this class to cancel an acquired swapchain or hide submission. No
clear/draw is performed here, including on that error path.

Once a native submit or permitted cancel call is made, the command reference is
cleared from both the caller frame and context even if the native call reports
failure: the public SDL API forbids referencing it after the call. Failure is
preserved, the device remains owned/faulted, and no retry using that consumed
command pointer is attempted. Successful submit means native queue submission,
not proven presentation, completed rendering or GPU-idle qualification.

## Explicit close and external children

An outstanding frame must be explicitly consumed before idle/shutdown. External
meshes, textures, fences, pipelines, shaders and other device children remain
their owners' responsibility: finish their command usage and release them before
context shutdown. The context cannot infer those lifetimes from a borrowed
device pointer and does not force-release them.

`wait_idle` and `shutdown` call the real `SDL_WaitForGPUIdle`. On failure they
release no window/device/video ownership. The caller retains the complete
context/resource lifetime for explicit retry. On successful idle, shutdown
unclaims the window, destroys its device and window, and releases only its own
video reference. Successful shutdown is idempotent. The default destructor does
not perform hidden blocking GPU work or claim cleanup after an idle failure:
successful explicit shutdown before destruction is a precondition.

Only source/API review was performed here. No CMake changes, compile, driver
probe, window creation, GPU submission, shader/draw, runtime tests, game execution
or game-asset reads/copies occurred in this worker tranche. Lead owns target
integration and compilation. GPU runtime availability remains unqualified.
