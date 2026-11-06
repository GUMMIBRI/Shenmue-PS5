# SDL3 global event queue ownership

`SdlEventPump` is an original bounded adapter over existing SDL3 public APIs.
It has no automatic game routes, focus policy, pause/resume, window closing,
session teardown, `SDL_Quit`, launch or simulation behavior.

## Exclusive owner contract

Initialize SDL/video on the application main thread first. Supply an explicit
nonzero target `SDL_WindowID`, a nonnull `noexcept` sink, its context, and
`EventQueueOwnership::exclusive_application` to `attach`. The caller obtains
the correct ID for its window and keeps the window lifetime valid; this adapter
does not create a window or infer an ID. Its lifecycle and polling must remain
on that same video/main thread. Actual `SDL_IsMainThread` and `SDL_WasInit`
checks reject polling without main-thread/event initialization.

SDL's event queue is process-global, including events for other windows and
unknown/custom event types. The application must designate this as the sole
queue consumer: no competing `SDL_PollEvent`, wait/peep consumer, other event
pump, or SDL main-callback event delivery. A main-thread ownership guard refuses
another simultaneously attached Cedar pump in this linked implementation. It
cannot detect foreign queue consumers or independently loaded Cedar copies.
This is a caller contract rather than a process-wide SDL lock.

`detach` releases the Cedar claim but does not flush/consume events or change
SDL initialization. Destruction also releases the claim without calling SDL.
Both belong on the main thread. The sink must not destroy the pump, call a
foreign queue consumer, shut down SDL, or destroy required resources during the
callback. Recursive poll and attach/detach during delivery return invalid state;
destruction/lifetime violations cannot be safely recovered by a C++ callback.

## Complete delivery and bounded work

Each `poll(max_events)` accepts a budget from 1 through 4096. Every event actually
retrieved is passed synchronously, exactly once, by const reference to the sink.
Nothing is filtered out based on type/window, including quit requests, another
window's close requests, or unknown event types. The sink decides all routing.
It must not throw. Event references last only through the callback. Copy needed
data while observing each SDL event type's payload ownership/lifetime rules;
copying a union with pointer fields does not grant ownership of pointed data.

The budget counts retrieved events, not wall-clock time. The sink can take time,
and SDL documents that event polling itself can block on some window operations.
No scheduler, sleep, autonomous route or guaranteed frame duration is implied.

`cap_reached` means exactly that the delivery budget was exhausted. The adapter
does not perform a lookahead poll/peep, discard an extra event, or claim the queue
is empty/full. `poll_returned_empty` means a real `SDL_PollEvent` call returned
false. Even that observation is not a permanent drained-queue guarantee: new
events can arrive, and SDL controls its poll-cycle behavior.

The selected public SDL3 header specifies true for an event, false for none
available. There is no separate documented polling error code. The adapter does
not misinterpret stale `SDL_GetError` content as a poll failure. Invalid budget,
thread, initialization, ownership and recursive use are reported via `Status`
without consuming an event.

## Quit is a request handed to the application

Each batch records whether it actually received global `SDL_EVENT_QUIT` or
`SDL_EVENT_WINDOW_CLOSE_REQUESTED` for the exact specified window ID. A close
request for another window is still delivered to the sink, but does not set the
target quit flag. Both recognized requests set a sticky `quit_requested()` flag,
also included in later batch results, until a new successful attachment.
Remaining events are delivered up to the budget even after a quit request.

The application decides whether/when to forward this to
`Ps5Session::request_quit` or its own orchestration and when to shut down. The
adapter never closes the window or stops a game itself. Focus loss and other
events are delivered unchanged without being silently reinterpreted as pause.

## Integration and primary API evidence

Compile `src/sdl_events.cpp` with the existing selected SDL3 headers and link the
matching SDL3 library. Because `sdl_events.hpp` exposes the actual `SDL_Event`
type, the target must propagate the SDL3 include directory publicly to consumers.
Its direct SDL imports are `SDL_IsMainThread`, `SDL_WasInit`, and `SDL_PollEvent`.
No existing CMake, application, session or presentation files were changed.

The implementation was checked against the existing licensed public header at
`tools/pe-ps5-sdl3/20261029T010754Z-68fad8/src/include/SDL3/SDL_events.h`
(poll contract and event types) and adjacent `SDL_init.h` (thread/init queries).
No SDL implementation source was incorporated. This worker performed no
compilation, runtime, fixture or console action.
