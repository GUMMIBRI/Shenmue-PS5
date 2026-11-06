# Native application integration contract

Application is orchestration code, not a process entry point or supplied engine.
Missing required engine hooks return unavailable before any device initialization.
No demo frame, default scene, game-success stub or implicit asset search exists.

The future process loader must first make the public native imports available.
One Ps5ProcessApis instance calls actual user-service, pad and audio initializers;
its ready() becomes true only after successful stages. Only the one public-SDL
already-initialized user-service code is recognized. No account enumeration or
module loader is invented. Use readiness in explicit Ps5SessionConfig alongside
the selected user, device configuration and optional app-owned/read-only roots.

Application start then opens the session and initializes the supplied engine.
The engine receives borrowed assets/saves and the actual PCM format. Each step
uses native input/time, engine update/render/audio, checked surface upload,
queued grain output and presentation. There is no game-session timer or auto-loop.
The caller owns SDL events: use SdlEventPump with session.window_id() if desired,
route every event to an explicit sink, and choose whether to request quit.

Engine shutdown MUST join all resource workers and release its session borrows.
It returns Status: a failure keeps the entire session mounted/open for retry.
This prevents unmounting archives still referenced by an AssetStream worker.
Stop must succeed before destroying Application, engine context or caller buffers.
Destructor best effort cannot repair a violated lifetime precondition. No worker
is detached or force-killed, and a failed native close remains diagnostically
visible. Partial engine initialization follows the same cleanup contract.

No runtime or console action has verified device behavior. Static compilation and
shared-library linkage are recorded separately; neither establishes game boot.
