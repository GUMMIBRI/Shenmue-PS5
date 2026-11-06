// Original producer-driven GPU-frame ownership adapter, not a game engine.
#pragma once
#include "cedar/gpu_context.hpp"
#include "cedar/post_processor.hpp"

namespace cedar {
struct GpuProducedFrame {
    PostSource source{};
    MovieSourceReadiness readiness{MovieSourceReadiness::not_ready};
    // Adapter supplies immutable actual swapchain shape. Producer supplies
    // viewport/load/clear only; changing shape/device/texture/format is rejected.
    PostTarget target{};
};
using GpuFrameProducer = Status (*)(void*, const GpuFrame&, GpuProducedFrame&) noexcept;
using GpuFrameNativeUi = Status (*)(void*, const GpuFrame&) noexcept;
struct GpuFrameHooks {
    void* producer_context{};
    GpuFrameProducer produce{}; // Required actual engine/source producer.
    void* ui_context{};
    GpuFrameNativeUi native_ui{}; // Null explicitly requests no caller-native UI.
};
enum class GpuFrameStage : std::uint8_t {
    none, preflight, acquire, producer, producer_metadata, post, native_ui, release, complete, not_drawable
};
struct GpuFrameRecord {
    GpuFrameStage stage{GpuFrameStage::none};
    Status status{Status::ok}; // First primary error, or release failure if none.
    Status producer_status{Status::unavailable}, post_status{Status::unavailable};
    Status ui_status{Status::unavailable}, release_status{Status::unavailable};
    PostProcessorRecord post{};
    bool drawable{}, producer_called{}, ui_called{};
    bool submission_attempted{}, submission_accepted{};
    bool cancellation_attempted{}, cancellation_accepted{};
    bool release_after_failure{}, frame_pending{};
    char primary_message[256]{}, release_message[256]{};
};

// Borrowed live context/post processor; all calls use the context's owning
// GPU/main thread. No device/window/session, input/audio, engine or movie owner.
// Frame member stays stable at the address validated by GpuContext.
// Destruction is legal only after frame_pending()==false and no active callback.
class GpuFramePresenter final {
public:
    GpuFramePresenter(GpuContext& context, PostProcessor& post) noexcept;
    ~GpuFramePresenter() = default; // No implicit submit/cancel/present/destruction.
    GpuFramePresenter(const GpuFramePresenter&) = delete;
    GpuFramePresenter& operator=(const GpuFramePresenter&) = delete;
    // Acquires actual target -> real producer -> post -> optional native UI ->
    // release. An acquired swapchain MUST submit even after recording failure;
    // only no-swapchain commands may cancel. Every primary/release error retained.
    Status present(const GpuFrameHooks& hooks, GpuFrameRecord& record) noexcept;
    // Explicit retry if context validation/thread failure prevented consumption.
    // Returns release status; record.status still preserves the original fault.
    Status finish_pending(GpuFrameRecord& record) noexcept;
    bool frame_pending() const noexcept { return frame_.commands != nullptr; }
    const GpuFrameRecord& last_record() const noexcept { return last_; }
private:
    void primary(GpuFrameStage stage, Status status, const char* message) noexcept;
    Status release_current() noexcept;
    GpuContext& context_;
    PostProcessor& post_;
    GpuFrame frame_{};
    GpuFrameRecord last_{};
    bool presenting_{};
};
} // namespace cedar
