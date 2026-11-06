// Original Cedar adapter using public SDL3 events; no SDL implementation copied.
#pragma once
#include "cedar/platform.hpp"
#include <SDL3/SDL_events.h>
#include <cstddef>

namespace cedar {
using SdlEventSink = void (*)(void* context, const SDL_Event& event) noexcept;
enum class EventQueueOwnership : std::uint8_t { exclusive_application };
struct EventBatch {
    Status status{Status::ok};
    std::size_t delivered{};
    bool global_quit_event{}, target_window_close_event{};
    bool quit_requested{}; // Sticky pump request, including earlier batches.
    bool cap_reached{}; // Budget exhausted; remaining queue state is unknown.
    bool poll_returned_empty{}; // Observed SDL_PollEvent(false), not a drain guarantee.
};

// All lifecycle calls/polls belong to the SDL video/main thread. The application
// must give this adapter exclusive ownership of SDL's GLOBAL event queue.
// Another Cedar instance cannot attach concurrently; foreign polling cannot be
// detected. Do not combine with SDL main callbacks or another poll/wait consumer.
class SdlEventPump final {
public:
    static constexpr std::size_t max_events_per_call = 4096;
    SdlEventPump() noexcept = default;
    ~SdlEventPump();
    SdlEventPump(const SdlEventPump&) = delete;
    SdlEventPump& operator=(const SdlEventPump&) = delete;
    SdlEventPump(SdlEventPump&&) = delete;
    SdlEventPump& operator=(SdlEventPump&&) = delete;
    // Nonzero explicit window ID and nonnull sink required. No SDL initialization
    // is owned. A successful attach resets the sticky quit flag.
    Status attach(SDL_WindowID target_window, SdlEventSink sink, void* context,
                  EventQueueOwnership ownership) noexcept;
    Status detach() noexcept;
    EventBatch poll(std::size_t max_events) noexcept;
    bool attached() const noexcept { return sink_ != nullptr; }
    bool quit_requested() const noexcept { return quit_requested_; }
    SDL_WindowID target_window() const noexcept { return window_; }
private:
    SdlEventSink sink_{};
    void* context_{};
    SDL_WindowID window_{};
    bool polling_{}, quit_requested_{};
};
} // namespace cedar
