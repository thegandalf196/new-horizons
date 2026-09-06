# New Horizons client timing diagnostic (not a delivery mode)

## Enable and collect

Build owns `ENABLE_NH_PERF_EXPERIMENTS=ON` / `NH_PERF_EXPERIMENTS`. The shipping
option and every preset remain OFF. OFF uses inline no-op hooks. An enabled
build still requires `NH_PERF_TRACE=1` in the launched game's environment.

`NH_PERF_TRACE_FILE` selects a **disposable** JSONL output file (overwritten at
normal engine shutdown). Default: the configured user logs directory's
`nh-perf-client.jsonl`. `NH_PERF_TRACE_MAX_RECORDS` defaults to 250000, accepts
1024..1000000. Records are reserved once, bounded, and never written per event.
Normal `GameEngine` destruction flushes once. Kill/crash/power loss can lose the
buffer entirely; absent output is a failed/missing run, not a zero-latency run.
No assets, packet payloads, key text, mouse coordinates or machine paths are
recorded. Packet type is compiler RTTI, not a stable cross-build type ID.

First-line metadata gives `dropped` and `metadata_evicted`. A full buffer loses
new records, not old records. Metadata eviction includes unconsumed/coalesced
SDL events and bounded move/visual slots. Do not infer completeness from partial
traces. Allocation is once per traced process and retained until process exit
so explicit shutdown cannot use destroyed static storage/directory objects.

## Clock and joins

Uses Runtime's `lib/network/PerformanceTrace.h`: schema `nh-perf-v1`,
`steady_ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())`,
`emitter=client`, emitter-local `seq`, hashed `std::thread::id` as `thread`.
Known packet request IDs/players are exported only at submit and ACK. Manifest
must supply process/source/binary identity; the thread hash is NOT an OS TID.

This is the same-process steady clock, not OS injection time, physical display
scanout, or automatically Python `monotonic_ns`. The external tester must retain
injection/capture brackets and establish its clock relationship independently.
SDL timestamps are a separate domain in `value`: SDL2 milliseconds, SDL3
nanoseconds, identified by `event_poll_sdl2_ms` / `event_poll_sdl3_ns`.

- `event_received`: immediately after successful SDL polling, before preprocess.
  This is SDL queue receipt, NOT kernel/OS hardware receipt or XTest injection.
- `event_dispatch`: queued event handler entry. `event_id` joins a unique
  `(SDL type,timestamp)` to receipt. Millisecond collisions, coalescing, metadata
  eviction or absent receipt leave the ID missing. They must not be guessed.
- A thread-local event scope carries `event_id` into **synchronous** submits.
  It does not invent links across queued callbacks, timers or AI threads.
  SDL user-event IDs are callbacks, not necessarily physical/player input.
- `request_call_enter/return` surrounds CClient::sendRequest, including callback
  and optional wait. `command_submit` has the assigned request ID/player.
  `transport_submit_enter/return` brackets sendGamePack only; do not label it
  server execution time. Runtime owns transport/authoritative measurements.
- `client_apply_begin`, `client_state_applied`, `client_apply_end` share
  `apply_id`; they bracket before-visitor, game-state apply, and after-visitor.
  The after-visitor can wait for animation and return AFTER presentation.
  Lag-compensation interception/silent replay application is not covered by
  these hooks; missing stages are missing, not authoritative apply proof.
- `ack` records PackageApplied's existing request ID/player/result (detail).
  It is NEVER visual response or a first-present trigger.

## Render-supported candidates, not unqualified visual latency

Bounded normal-success MoveHero diagnostics match only a unique pending hero
and **first requested destination**, within 30 seconds. This is semantic
correlation, not a packet-provided request identity: export uses the deliberately
separate `matched_request_id`, never fabricates `request_id` on result packets.
Multi-step batches, ambiguous concurrent requests, failure/teleport/boat cases,
long waits, offscreen moves, eviction and unsupported commands remain missing.

After client state apply, MapViewController marks normal movement only once its
interpolated pixel position differs from the start (or for an instant visible
move). The hero image draw hook records `affected_object_draw_candidate` for the
current GUI frame. After backend SDL_RenderPresent returns,
`affected_present_candidate` identifies the first such frame. SDL3 unsuccessful
presentation emits `present_failed` instead. SDL2's void API cannot establish
success. ACK and arbitrary following frames do not arm these candidates.

**These are candidates, not proof of the first changed visible pixel.** The draw
can target a cached tile, be clipped, transparent internally, obscured or composed
under another layer. The tester must corroborate against its affected-region
capture-change brackets. Cached rendering without a new hero-image draw can miss
a response. Generic commands/battle results do not have affected-frame coverage.
Do not report end-to-end visual latency from these markers alone. Presentation
API return is not physical scanout, and Xvfb/software capture is not a monitor.

## Event/AI responsiveness and intentional input restrictions

`event_poll_*` intervals describe polling service gaps even if no SDL input is
pending. `event_received -> event_dispatch` describes queued residence when its
join is unique. `input_service` marks frame-time dispatch; `input_captured` marks
the existing capturedAllEvents restriction; `input_intentionally_drained` marks
the existing movement cancellation/input-discard path. These are distinct from
an unserviced UI. No commands are admitted through a blocked interface.

`frame_lock_wait -> frame_begin` bounds acquiring interfaceMutex. Consecutive
`present_return` times describe presentation intervals. `player_turn_start/end`
carry player number in detail and bracket client-observed turn notifications;
they are NOT isolated AI CPU measurements. `player_blocked` detail=player and
value=blockade start/end. Manifest must identify human/AI colors, animation
speed, vsync, renderer and resolution. Compare CPU/memory externally; separate
intentional pacing from lock/event stalls. Frame skipping and 1ms polling remain
unchanged.

## Callback/re-entry assessment

Current GUI update takes interfaceMutex. Network pack processing can invoke UI
callbacks and release that mutex while waiting for animations/dialogs; the GUI
must run to satisfy those waits. Main-thread dispatched callbacks execute from
SDL user events. sendRequest can call requestSent and synchronously wait for
request realization while releasing the game-state shared lock. Thus preserving
validation/serialization alone does NOT prove same-thread delivery safe: direct
re-entry can encounter held interface/state locks or a callback waiting on the
very frame/ACK its current stack would prevent.

`main_callback_enter/exit`, request-call scopes and apply scopes record nested
thread-local depth in detail. These reveal observed callback thread/re-entry,
not exhaustive absence of re-entry. Tracer locks are private and short; no game
callback, renderer, task dispatch or network send runs under the trace mutex.
No ownership, scheduling, packet format or transport policy is changed. No
same-thread mode is introduced or made default by this instrumentation.

## Required validation (pending runner evidence)

Content is sole graphical runner; Build serializes compilation and timing.
Run matched OFF/ON bounded repetitions from the same frozen state, warming up,
with no competing build/microbenchmark. Compare full-turn time, frame/event gaps,
CPU/memory and external pixel-change latency. Prefer an instrumentation-enabled
binary with environment OFF vs ON to isolate runtime overhead; retain the true
shipping-OFF baseline separately. ON adds a reserved buffer, clock/thread hash,
mutex and metadata searches; overhead is not assumed negligible. Report all
failures, truncations, sample counts and between-run variation. No compiled,
graphical, correctness or measured-overhead PASS is established by source review.
