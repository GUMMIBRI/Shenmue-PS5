# Native GPU texture upload facility

Original files: `include/cedar/gpu_texture.hpp`, `src/gpu_texture.cpp`. Uses the
existing licensed public SDL3 GPU API; no SDL implementation or game source is
copied. Lead owns `gpu_types.hpp`, device/window creation and integration.

## Actual upload contract

`GpuTexture::begin_upload(device,surface,encoding,sampling)` borrows a live
device. It requires an empty resource and an explicit UNORM or sRGB selection,
plus caller-controlled nearest/linear minification/magnification, explicit mip
filtering, anisotropy and repeat, mirror-repeat or clamp U/V addressing. A
supported sampler-only2D RGBA8 format
is required. No fallback texture, hidden transfer choice or inferred alpha mode.

Validated `SurfaceView` rows are copied into a native mapped upload transfer
buffer during the call. Pitched RGBA is tightly repacked; pitched BGRA is channel
reordered to RGBA without changing alpha or transfer values. The source remains
immutable and is no longer borrowed after return. Limits are dimension16384 and
64 MiB tightly packed upload bytes; source extent/address arithmetic is checked.
Mapped native transfer memory is driver-owned and never freed by CPU allocation
APIs. It is unmapped before any upload commands are encoded.

The original single-surface method delegates to the new mip-chain method with
exactly one supplied level. Its default nearest mip filtering,1x anisotropy and
LOD0 preserve previous behavior. Base width/height/format metadata is unchanged;
the new `mip_count()` reports the requested level count (zero when empty). Failed
native construction can retain requested metadata in failed phase, just as base
dimensions did; it does not expose a ready texture.

The facility creates a real GPU texture with exactly the supplied level count
and a sampler, acquires its own command buffer, records each real level upload
in the same copy pass, ends that pass and submits with one fence. Command buffer
acquisition/submission occur in the same call and
thread. A submission error does not reuse/cancel that now-invalid command buffer.
Before submission, a failed copy-pass creation cancels the command buffer.

Successful `begin_upload` means **uploading**, not ready. `poll()` is nonblocking:
`Status::unavailable` means the fence has not signaled; `Status::ok` means ready.
`wait()` explicitly blocks for only this fence and reports backend failure if SDL
cannot wait. A failed wait never marks readiness. Only a positively signaled
fence changes the phase to ready; then staging storage and fence are released.
`texture()` and `sampler()` return null unless ready. An empty instance has no
texture and never claims otherwise.

SDL's fence query only distinguishes signaled from unsignaled; this facility
does not infer a device-loss diagnostic from an unsignaled result. Native errors
from fallible operations are copied into the object's bounded error buffer.

## Ownership and disposal

All methods and destruction belong to the device's owning main/GPU thread.
Native operations enforce SDL's main-thread query and the resource-creating
thread ID. A wrong-thread shutdown retains owned handles and records an error;
destruction on that thread remains a caller lifetime violation, not recovery.
The caller must keep the device live through resource shutdown/destruction and
coordinate every use of returned handles. This object never destroys the device,
creates a window, pumps events or performs rendering. There is no generated mip
chain, comparison sampler, shader or pipeline.

`shutdown()` is nonblocking and never fabricates upload completion. Its fence
is released when no further completion observation is needed. Texture, sampler
and transfer buffer are disposed through SDL's documented APIs that free GPU
resources as soon as safe. Handles are cleared immediately and must not be
referenced afterward; no global device-idle wait or foreign resource cleanup.
Ready resources must be shut down explicitly before replacement. Failed native
construction/submission leaves a failed phase; shutdown makes it empty again.

UNORM versus sRGB controls the actual native texture format, so an explicit sRGB
choice permits the GPU's normal sRGB sampling behavior. There is no CPU color
conversion, unpremultiplication, alpha inference, vertical flip or display proof.

## Caller-supplied mip-chain upload

`begin_upload_mips(device, levels, count, encoding, sampling)` accepts an
immutable array of actual caller-supplied `SurfaceView` images. Count must be
1–15; every level must be nonzero, at most16384 per dimension, and use the same
RGBA8/BGRA8 source layout as level zero. Every next dimension is exactly
`max(1, previous / 2)`, including nonsquare/odd dimensions. A partial chain is
allowed; repeating1x1 or exceeding the natural chain is rejected. There is no
automatic downsampling or replacement art.

All levels, descriptor/address ranges, source pitches/byte bounds and combined
tightly packed byte budget are checked before native allocation and before any
transfer writes. The complete chain must fit64MiB, not64MiB per level. A late
invalid level cannot leave an earlier level copied into transfer storage.

Conservative source read spans include row-pitch gaps between the first and last
used rows. These spans must not overlap one another, the descriptor array,
sampling metadata or the texture object. Duplicate/overlapping level storage is
rejected rather than silently reused. Distinct level subviews can share a larger
allocation when their actual read spans are disjoint; unused declared trailing
capacity need not be disjoint. Every declared extent is still checked for
address overflow. Descriptors/sampling are snapshotted before native allocation.

After mapping, the entire mapped range is checked against ALL source and metadata
ranges before even the first row is written. Each level is then tightly repacked
at its checked transfer offset, preserving per-level alpha/origin/color values;
BGRA source layout receives only the same explicit channel reorder as before.
The transfer is unmapped before encoding any native copy commands. Each upload
uses its actual `SDL_GPUTextureRegion.mip_level` and dimensions plus the checked
`SDL_GPUTextureTransferInfo.offset`, pixels-per-row and rows-per-layer.

### Mandatory alignment evidence

The selected public `SDL_gpu.h` documentation immediately above
`SDL_UploadToGPUTexture` explicitly requires transfer data aligned to the texture
format's texel size. RGBA8 texels are four bytes. Level offsets start at zero and
advance only by tightly packed RGBA8 image lengths, which are multiples of four;
the implementation checks that alignment and the cumulative budget. No guessed
512-byte Vulkan requirement is introduced. The same header's transfer-info
256-byte row/512-byte offset warning describes optional D3D12 performance
repacking; it is not the mandatory texel-alignment rule used by this Vulkan path.

## Explicit sampling and anisotropy

`GpuTextureSampling.mip_filter` selects actual SDL nearest/linear mip sampling.
LOD bias remains neutral zero; min LOD is zero and max LOD is the last supplied
level, preventing reads from absent levels. A single-level upload keeps maxLOD0.
No sharpen, artificial negative LOD, synthetic mip selection or hidden texture
quality policy is applied.

`anisotropy` is an integer1–16.1x keeps native anisotropy disabled; greater values
set actual `SDL_GPUSamplerCreateInfo.enable_anisotropy` and `max_anisotropy`.
The selected header explicitly states that the device's creation feature
`SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN` must be enabled before
enabling the sampler field. Therefore >1x requires caller-supplied
`anisotropy_feature_confirmed=true`; otherwise the method returns unavailable
before GPU allocation. That confirmation must come from an actual device
creation record (such as the owning context's enabled-feature getter), not a UI
selection. Public device-properties documentation does not provide an enabled-
feature query, so none is invented.

Sampler creation errors are reported as actual backend failures. There is no
retry at1x, nearest filtering or another format to pretend the request worked.
An externally supplied device must honor the confirmed feature contract and its
real backend capabilities. This facility does not claim hardware filtering
quality or apply sampling over an equal/better existing engine feature; settings
ownership/capability resolution remains an explicit caller responsibility.

## API evidence and selected dependency

Read public header `SDL3/SDL_gpu.h` in the selected existing SDL tree:
`<LOCAL_REFERENCE>/include`. Header provenance is
SDL commit `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` plus the existing public PS5
overlay, with retained SDL zlib-style notices. Lead selected the read-only archive
`<LOCAL_REFERENCE>/libSDL3.a`.

The exact header contracts used are `SDL_GPUTextureSupportsFormat`, texture and
sampler creation, transfer-buffer creation/map/unmap, command-buffer acquisition,
copy-pass begin/upload/end, submit-and-acquire-fence, query/wait/release-fence,
cancel, and safe release of texture/sampler/transfer resources. No guessed native
graphics ABI or substitute rendering backend is introduced.

No runtime, tests, device calls, windows, assets, extraction or console operations
were executed by this task. Lead owns coherent compilation and strict linkage;
actual driver/device availability and image output require human qualification.
