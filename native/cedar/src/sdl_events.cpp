#include "cedar/sdl_events.hpp"
#include <SDL3/SDL_init.h>

namespace cedar {
namespace {
// Main-thread-only ownership guard for this linked Cedar implementation. It
// cannot observe queue consumers in unrelated code or independently loaded copies.
SdlEventPump* active_owner{};
bool events_ready() noexcept {
    return SDL_IsMainThread() && (SDL_WasInit(SDL_INIT_EVENTS) & SDL_INIT_EVENTS) != 0;
}
} // namespace

SdlEventPump::~SdlEventPump() {
    // No SDL shutdown, queue drain, callback or window action during destruction.
    // Destruction from inside one's sink is forbidden by the lifetime contract.
    if (active_owner == this) active_owner = nullptr;
}

Status SdlEventPump::attach(SDL_WindowID window, SdlEventSink sink, void* context,
                          EventQueueOwnership ownership) noexcept {
    if (!window || !sink || ownership != EventQueueOwnership::exclusive_application)
        return Status::invalid_argument;
    if (!events_ready() || polling_ || attached() || active_owner)
        return Status::invalid_state;
    window_ = window;
    sink_ = sink;
    context_ = context;
    quit_requested_ = false;
    active_owner = this;
    return Status::ok;
}

Status SdlEventPump::detach() noexcept {
    if (!SDL_IsMainThread() || polling_) return Status::invalid_state;
    if (active_owner == this) active_owner = nullptr;
    sink_ = nullptr;
    context_ = nullptr;
    window_ = 0;
    return Status::ok;
}

EventBatch SdlEventPump::poll(std::size_t budget) noexcept {
    EventBatch batch{};
    batch.quit_requested = quit_requested_;
    if (!budget || budget > max_events_per_call) {
        batch.status = Status::invalid_argument;
        return batch;
    }
    if (!events_ready() || !attached() || active_owner != this || polling_) {
        batch.status = Status::invalid_state;
        return batch;
    }
    polling_ = true;
    while (batch.delivered < budget) {
        SDL_Event event{};
        // Public SDL3 returns false when no event is available. It does not
        // document a separate PollEvent failure code; stale SDL_GetError text
        // must not be used to invent one here.
        if (!SDL_PollEvent(&event)) {
            batch.poll_returned_empty = true;
            break;
        }
        const bool quit = event.type == SDL_EVENT_QUIT;
        const bool close = event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                           event.window.windowID == window_;
        batch.global_quit_event |= quit;
        batch.target_window_close_event |= close;
        quit_requested_ |= quit || close;
        // Every retrieved event reaches the owner, even an unknown type,
        // another window's close request, or an event after a quit request.
        sink_(context_, event);
        ++batch.delivered;
    }
    batch.cap_reached = batch.delivered == budget;
    batch.quit_requested = quit_requested_;
    polling_ = false;
    return batch;
}
} // namespace cedar
