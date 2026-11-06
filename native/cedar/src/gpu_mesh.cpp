#include "cedar/gpu_mesh.hpp"
#include <SDL3/SDL_error.h>
#include <cstring>
#include <limits>

namespace cedar {
namespace {
static_assert(offsetof(GpuVertex, x) == 0 && offsetof(GpuVertex, u) == 12 &&
              offsetof(GpuVertex, rgba) == 20, "GPU mesh and shader attribute offsets must agree");
static_assert(offsetof(GpuNormal, x) == 0 && offsetof(GpuNormal, y) == 4 &&
              offsetof(GpuNormal, z) == 8, "GPU normal stream offsets must agree with float3");
bool source_span(const void* source, std::size_t bytes, std::size_t alignment,
                 const void* owner, std::size_t owner_bytes) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(source);
    const auto object = reinterpret_cast<std::uintptr_t>(owner);
    if (!source || begin % alignment || bytes > std::numeric_limits<std::uintptr_t>::max() - begin ||
        owner_bytes > std::numeric_limits<std::uintptr_t>::max() - object) return false;
    return begin + bytes <= object || object + owner_bytes <= begin;
}
void copy_error(char* target, std::size_t capacity, const char* text) noexcept {
    std::size_t i = 0;
    if (text) for (; i + 1 < capacity && text[i]; ++i) target[i] = text[i];
    target[i] = '\0';
}
bool finite(float value) noexcept {
    constexpr auto largest = std::numeric_limits<float>::max();
    return value >= -largest && value <= largest; // Rejects NaN and +/- infinity.
}
} // namespace

GpuMesh::~GpuMesh() { reset(); }

Status GpuMesh::fail(Status status, const char* message) noexcept {
    copy_error(error_, sizeof(error_), message);
    return status;
}
Status GpuMesh::sdl_failure(const char* fallback) noexcept {
    const char* message = SDL_GetError();
    fail(Status::backend_failure, message && *message ? message : fallback);
    phase_ = GpuResourcePhase::failed;
    return Status::backend_failure;
}
void GpuMesh::release_resources() noexcept {
    if (fence_) SDL_ReleaseGPUFence(device_, fence_);
    if (transfer_) SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    if (buffer_) SDL_ReleaseGPUBuffer(device_, buffer_);
    if (normal_buffer_) SDL_ReleaseGPUBuffer(device_, normal_buffer_);
    fence_ = nullptr;
    transfer_ = nullptr;
    buffer_ = nullptr;
    normal_buffer_ = nullptr;
    vertices_ = 0;
}
void GpuMesh::reset() noexcept {
    release_resources();
    device_ = nullptr;
    phase_ = GpuResourcePhase::empty;
    // Keep the last operation/cancellation failure available through cleanup.
}
void GpuMesh::cancel_commands(SDL_GPUCommandBuffer* commands) noexcept {
    if (!SDL_CancelGPUCommandBuffer(commands)) {
        const char* message = SDL_GetError();
        copy_error(cleanup_error_, sizeof(cleanup_error_),
                   message && *message ? message : "GPU command cancellation failed");
    }
    // SDL forbids further command-buffer references after cancel, even on error.
}

Status GpuMesh::upload(SDL_GPUDevice* device, const GpuVertex* vertices,
                       std::size_t count) noexcept {
    return upload_streams(device, vertices, nullptr, count, false);
}
Status GpuMesh::upload_with_normals(SDL_GPUDevice* device, const GpuVertex* vertices,
                                   const GpuNormal* normals, std::size_t count) noexcept {
    return upload_streams(device, vertices, normals, count, true);
}
Status GpuMesh::upload_streams(SDL_GPUDevice* device, const GpuVertex* vertices,
                              const GpuNormal* normals, std::size_t count, bool with_normals) noexcept {
    if (phase_ != GpuResourcePhase::empty)
        return fail(Status::invalid_state, "Reset mesh before upload/replacement");
    if (!device || !vertices || !count || count > max_vertices || count % 3 || (with_normals && !normals))
        return fail(Status::invalid_argument, "Mesh requires a device and bounded triangle-list vertices");
    if (!source_span(vertices, count * sizeof(GpuVertex), alignof(GpuVertex), this, sizeof(*this)) ||
        (with_normals && !source_span(normals, count * sizeof(GpuNormal), alignof(GpuNormal), this, sizeof(*this))))
        return fail(Status::invalid_argument, "Mesh source spans must be aligned, nonwrapping and outside this resource");
    for (std::size_t i = 0; i < count; ++i) {
        const auto& vertex = vertices[i];
        if (!finite(vertex.x) || !finite(vertex.y) || !finite(vertex.z) ||
            !finite(vertex.u) || !finite(vertex.v))
            return fail(Status::invalid_argument, "Mesh positions and UV coordinates must be finite");
        if (with_normals) {
            const auto& normal = normals[i];
            if (!finite(normal.x) || !finite(normal.y) || !finite(normal.z) ||
                (normal.x == 0 && normal.y == 0 && normal.z == 0))
                return fail(Status::invalid_argument, "Every supplied lighting normal must be finite and nonzero");
        }
    }
    // The explicit vertex cap bounds both buffers and their combined transfer.
    static_assert(max_vertices <= std::numeric_limits<std::uint32_t>::max() / (sizeof(GpuVertex) + sizeof(GpuNormal)),
                  "Combined GPU transfer byte size must fit SDL's Uint32 API");
    const auto bytes = static_cast<Uint32>(count * sizeof(GpuVertex));
    const auto normal_bytes = with_normals ? static_cast<Uint32>(count * sizeof(GpuNormal)) : Uint32{0};
    const auto transfer_bytes = bytes + normal_bytes;
    error_[0] = cleanup_error_[0] = '\0';
    device_ = device;
    const SDL_GPUBufferCreateInfo buffer_info{SDL_GPU_BUFFERUSAGE_VERTEX, bytes, 0};
    buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
    if (!buffer_) return sdl_failure("GPU vertex buffer allocation failed");
    if (with_normals) {
        const SDL_GPUBufferCreateInfo normal_info{SDL_GPU_BUFFERUSAGE_VERTEX, normal_bytes, 0};
        normal_buffer_ = SDL_CreateGPUBuffer(device_, &normal_info);
        if (!normal_buffer_) {
            const auto error = sdl_failure("GPU normal buffer allocation failed");
            release_resources();
            return error;
        }
    }
    const SDL_GPUTransferBufferCreateInfo transfer_info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, transfer_bytes, 0};
    transfer_ = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (!transfer_) {
        const auto error = sdl_failure("GPU upload transfer allocation failed");
        release_resources();
        return error;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer_, false);
    if (!mapped) {
        const auto error = sdl_failure("GPU transfer mapping failed");
        release_resources();
        return error;
    }
    std::memcpy(mapped, vertices, bytes);
    if (with_normals) std::memcpy(static_cast<std::uint8_t*>(mapped) + bytes, normals, normal_bytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer_);
    auto* commands = SDL_AcquireGPUCommandBuffer(device_);
    if (!commands) {
        const auto error = sdl_failure("GPU upload command acquisition failed");
        release_resources();
        return error;
    }
    auto* copy_pass = SDL_BeginGPUCopyPass(commands);
    if (!copy_pass) {
        const auto error = sdl_failure("GPU copy pass creation failed");
        cancel_commands(commands);
        release_resources();
        return error;
    }
    const SDL_GPUTransferBufferLocation source{transfer_, 0};
    const SDL_GPUBufferRegion destination{buffer_, 0, bytes};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    if (with_normals) {
        const SDL_GPUTransferBufferLocation normal_source{transfer_, bytes};
        const SDL_GPUBufferRegion normal_destination{normal_buffer_, 0, normal_bytes};
        SDL_UploadToGPUBuffer(copy_pass, &normal_source, &normal_destination, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    fence_ = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    // Submission invalidates commands on every return; never cancel/reuse it.
    if (!fence_) {
        const auto error = sdl_failure("GPU upload submission/fence acquisition failed");
        release_resources();
        return error;
    }
    vertices_ = static_cast<std::uint32_t>(count);
    phase_ = GpuResourcePhase::uploading;
    return Status::ok;
}

void GpuMesh::finish_upload() noexcept {
    // Called only after a real fence query/wait confirms upload completion.
    SDL_ReleaseGPUFence(device_, fence_);
    SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    fence_ = nullptr;
    transfer_ = nullptr;
    phase_ = GpuResourcePhase::ready;
}
Status GpuMesh::poll(bool& ready) noexcept {
    ready = false;
    if (phase_ == GpuResourcePhase::ready) { ready = true; return Status::ok; }
    if (phase_ != GpuResourcePhase::uploading)
        return fail(Status::invalid_state, "Mesh has no pending upload to query");
    // SDL documents false as unsignaled, without a separate error return.
    if (SDL_QueryGPUFence(device_, fence_)) {
        finish_upload();
        ready = true;
    }
    return Status::ok;
}
Status GpuMesh::wait_ready() noexcept {
    if (phase_ == GpuResourcePhase::ready) return Status::ok;
    if (phase_ != GpuResourcePhase::uploading)
        return fail(Status::invalid_state, "Mesh has no pending upload to wait for");
    SDL_GPUFence* fences[]{fence_};
    if (!SDL_WaitForGPUFences(device_, true, fences, 1))
        return sdl_failure("GPU upload fence wait failed"); // Retain handles for explicit reset.
    finish_upload();
    return Status::ok;
}
} // namespace cedar
