// Original explicit SDL3 GPU upload resource; no mesh generation or game logic.
#pragma once
#include "cedar/gpu_types.hpp"
#include <cstddef>

namespace cedar {
// Single owning thread; caller's GPU device outlives all resources and pending
// submissions, including SDL-deferred releases after reset/destruction.
class GpuMesh final {
public:
    static constexpr std::size_t max_vertices = 1024u * 1024u;
    GpuMesh() noexcept = default;
    ~GpuMesh();
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&&) = delete;
    GpuMesh& operator=(GpuMesh&&) = delete;

    // Empty-only upload; explicit reset is required before replacement/retry.
    // At least one triangle; tightly packed shared GpuVertex layout only.
    // Source bytes are copied before this call returns and are never borrowed.
    // Success begins uploading, not ready. No fabricated/default vertices.
    Status upload(SDL_GPUDevice* device, const GpuVertex* vertices,
                  std::size_t vertex_count) noexcept;
    // Optional separate float3 stream, copied with vertices under one fence.
    // Every normal must be finite/nonzero; caller supplies correctly transformed
    // unit normals. This uploader never normalizes or invents a lighting normal.
    Status upload_with_normals(SDL_GPUDevice* device, const GpuVertex* vertices,
                               const GpuNormal* normals, std::size_t vertex_count) noexcept;
    Status poll(bool& ready) noexcept;
    Status wait_ready() noexcept;
    // Releases ownership through SDL's deferred-safe release APIs; does not
    // promise idle/completion. Device owner must drain pending work before destroy.
    void reset() noexcept;
    GpuResourcePhase phase() const noexcept { return phase_; }
    SDL_GPUBuffer* buffer() const noexcept {
        return phase_ == GpuResourcePhase::ready ? buffer_ : nullptr;
    }
    bool has_normals() const noexcept {
        return phase_ == GpuResourcePhase::ready && normal_buffer_ != nullptr;
    }
    SDL_GPUBuffer* normal_buffer() const noexcept {
        return phase_ == GpuResourcePhase::ready ? normal_buffer_ : nullptr;
    }
    std::uint32_t vertex_count() const noexcept {
        return phase_ == GpuResourcePhase::ready ? vertices_ : 0;
    }
    SDL_GPUDevice* device() const noexcept {
        return phase_ == GpuResourcePhase::ready ? device_ : nullptr;
    }
    const char* last_error() const noexcept { return error_; }
    const char* cleanup_error() const noexcept { return cleanup_error_; }
private:
    Status upload_streams(SDL_GPUDevice* device, const GpuVertex* vertices,
                          const GpuNormal* normals, std::size_t count, bool with_normals) noexcept;
    Status fail(Status status, const char* message) noexcept;
    Status sdl_failure(const char* fallback) noexcept;
    void release_resources() noexcept;
    void finish_upload() noexcept;
    void cancel_commands(SDL_GPUCommandBuffer* commands) noexcept;
    SDL_GPUDevice* device_{};
    SDL_GPUBuffer* buffer_{};
    SDL_GPUBuffer* normal_buffer_{};
    SDL_GPUTransferBuffer* transfer_{};
    SDL_GPUFence* fence_{};
    std::uint32_t vertices_{};
    GpuResourcePhase phase_{GpuResourcePhase::empty};
    char error_[256]{}, cleanup_error_[256]{};
};
} // namespace cedar
