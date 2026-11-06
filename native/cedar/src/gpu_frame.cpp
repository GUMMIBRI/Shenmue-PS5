#include "cedar/gpu_frame.hpp"
#include <cmath>

namespace cedar {
namespace {
void message(char* target, std::size_t capacity, const char* text) noexcept {
    std::size_t i = 0;
    if (text) for (; i + 1 < capacity && text[i]; ++i) target[i] = text[i];
    target[i] = '\0';
}
bool same_frame(const GpuFrame& a, const GpuFrame& b) noexcept {
    return a.device == b.device && a.commands == b.commands && a.swapchain == b.swapchain &&
        a.color_format == b.color_format && a.width == b.width && a.height == b.height;
}
bool target_matches(const PostTarget& target, const GpuFrame& frame) noexcept {
    return target.device == frame.device && target.texture == frame.swapchain &&
        target.format == frame.color_format && target.width == frame.width && target.height == frame.height;
}
bool valid_viewport(const PostTarget& target) noexcept {
    const auto& rect = target.viewport;
    return rect.width && rect.height && rect.x <= target.width && rect.y <= target.height &&
        rect.width <= target.width - rect.x && rect.height <= target.height - rect.y &&
        (target.load_op == SDL_GPU_LOADOP_LOAD || target.load_op == SDL_GPU_LOADOP_CLEAR) &&
        (target.load_op != SDL_GPU_LOADOP_CLEAR ||
         (std::isfinite(target.clear_color.r) && std::isfinite(target.clear_color.g) &&
          std::isfinite(target.clear_color.b) && std::isfinite(target.clear_color.a)));
}
} // namespace

GpuFramePresenter::GpuFramePresenter(GpuContext& context, PostProcessor& post) noexcept
    : context_(context), post_(post) {}

void GpuFramePresenter::primary(GpuFrameStage stage, Status status, const char* text) noexcept {
    if (last_.status != Status::ok) return;
    last_.stage = stage;
    last_.status = status;
    message(last_.primary_message, sizeof(last_.primary_message), text);
}

Status GpuFramePresenter::release_current() noexcept {
    if (!frame_.commands) {
        last_.frame_pending = false;
        return Status::ok;
    }
    last_.release_after_failure |= last_.status != Status::ok;
    if (frame_.swapchain) {
        last_.submission_attempted = true;
        last_.release_status = context_.submit(frame_);
        last_.submission_accepted = last_.release_status == Status::ok;
    } else {
        last_.cancellation_attempted = true;
        last_.release_status = context_.cancel(frame_);
        last_.cancellation_accepted = last_.release_status == Status::ok;
    }
    last_.frame_pending = frame_.commands != nullptr;
    if (last_.release_status != Status::ok) {
        message(last_.release_message, sizeof(last_.release_message), context_.last_error());
        primary(GpuFrameStage::release, last_.release_status, last_.release_message);
    }
    return last_.release_status;
}

Status GpuFramePresenter::present(const GpuFrameHooks& hooks, GpuFrameRecord& record) noexcept {
    record = {};
    if (presenting_ || frame_pending() || context_.frame_outstanding()) {
        record.stage = GpuFrameStage::preflight;
        record.status = Status::invalid_state;
        record.frame_pending = frame_pending();
        message(record.primary_message, sizeof(record.primary_message), "Resolve the owned frame; no nested or foreign outstanding frame allowed");
        return record.status; // Never clobber an active outer call's diagnostics.
    }
    last_ = {};
    if (!hooks.produce) {
        primary(GpuFrameStage::preflight, Status::unavailable, "Actual engine/source producer callback required");
        record = last_;
        return last_.status;
    }
    if (!context_.initialized()) {
        primary(GpuFrameStage::preflight, Status::invalid_state, "GPU context must already be initialized");
        record = last_;
        return last_.status;
    }
    presenting_ = true;
    bool drawable = false;
    const auto acquired = context_.begin_frame(frame_, drawable);
    last_.drawable = drawable;
    if (acquired != Status::ok) {
        primary(GpuFrameStage::acquire, acquired, context_.last_error());
    } else if (!drawable) {
        // GpuContext consumed its permitted no-swapchain command internally.
        last_.stage = GpuFrameStage::not_drawable;
    } else {
        const auto actual_frame = frame_;
        GpuProducedFrame produced{};
        produced.target.device = frame_.device;
        produced.target.texture = frame_.swapchain;
        produced.target.format = frame_.color_format;
        produced.target.width = frame_.width;
        produced.target.height = frame_.height;
        produced.target.load_op = SDL_GPU_LOADOP_DONT_CARE; // Must explicitly choose LOAD/CLEAR.
        last_.producer_called = true;
        last_.producer_status = hooks.produce(hooks.producer_context, frame_, produced);
        if (!same_frame(frame_, actual_frame)) {
            frame_ = actual_frame; // Restore our address-stable exact context reference.
            primary(GpuFrameStage::producer_metadata, Status::invalid_argument, "Producer changed immutable acquired frame metadata");
        } else if (last_.producer_status != Status::ok) {
            primary(GpuFrameStage::producer, last_.producer_status, "Actual source producer failed; no post/UI result claimed");
        } else if (!target_matches(produced.target, actual_frame) || !valid_viewport(produced.target)) {
            primary(GpuFrameStage::producer_metadata, Status::invalid_argument,
                    "Producer may supply valid viewport/load/clear, not replace acquired target shape/ownership");
        } else {
            // PostProcessor owns game/movie classification, readiness and active
            // movie-serial rules. Do not impose a different game-only policy.
            last_.post_status = post_.record(frame_.commands, produced.source, produced.target,
                                             produced.readiness, last_.post);
            if (!same_frame(frame_, actual_frame)) {
                frame_ = actual_frame;
                primary(GpuFrameStage::post, Status::invalid_argument, "Post callback changed immutable frame ownership");
            } else if (last_.post_status != Status::ok) {
                primary(GpuFrameStage::post, last_.post_status, post_.last_error());
            } else if (!last_.post.presentation_recorded) {
                primary(GpuFrameStage::post, Status::backend_failure, "Post returned without an actual recorded presentation");
            } else if (hooks.native_ui) {
                last_.ui_called = true;
                last_.ui_status = hooks.native_ui(hooks.ui_context, frame_);
                if (!same_frame(frame_, actual_frame)) {
                    frame_ = actual_frame;
                    primary(GpuFrameStage::native_ui, Status::invalid_argument, "Native UI changed immutable frame ownership");
                } else if (last_.ui_status != Status::ok) {
                    primary(GpuFrameStage::native_ui, last_.ui_status, "Native UI callback failed after presentation recording");
                }
            }
        }
    }
    // SDL requires submission after successful swapchain acquisition, including
    // producer/post/UI failures. This may present partial content: retain that
    // primary failure and never label the frame successful just for releasing it.
    (void)release_current();
    if (last_.status == Status::ok && drawable) last_.stage = GpuFrameStage::complete;
    last_.frame_pending = frame_pending();
    presenting_ = false;
    record = last_;
    return last_.status;
}

Status GpuFramePresenter::finish_pending(GpuFrameRecord& record) noexcept {
    if (presenting_ || !frame_pending()) {
        record = last_;
        record.frame_pending = frame_pending();
        return Status::invalid_state;
    }
    presenting_ = true;
    const auto released = release_current();
    presenting_ = false;
    record = last_;
    return released; // Original primary failure is still in record.status.
}
} // namespace cedar
