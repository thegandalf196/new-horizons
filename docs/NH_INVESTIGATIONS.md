# Deferred New Horizons investigations

## Adventure-map movement feels smoother than another VCMI installation

Status: USER-REPORTED; deliberately deferred at the user's request. Do not restart
the closed architecture experiment to investigate this now.

Observation: the user reports visibly smoother adventure-map movement in New
Horizons than in their other VCMI installation. No causal explanation established.

Compare later:
- Exact upstream/fork revisions and actual process/thread selection in each run.
- Same map/save, hero route, animation and camera settings, resolution, renderer,
  VSync/FPS limit, display scaling and hardware/driver conditions.
- Frame-time distribution and camera/hero motion pacing; correlate command/state
  timing without equating ACK with visible response.
- Controlled in-process versus separate-process comparison only if available,
  holding other settings/version constant. Do not infer either configuration.

The fork enforces upstream's existing in-process single-player route. That is a
candidate explanation, not established cause. Earlier queued-typed formation tests
showed no meaningful end-to-end benefit and did not test this user's observation.
No superiority/performance claim until the controlled comparison supplies evidence.

## Battle WAIT latency — diagnostic boundaries

Status: instrumentation implemented; runtime latency remains under investigation.
Set `NH_RENDER_LOAD_TIMINGS=1` with global and AI logging at DEBUG or TRACE.
`NH_WAIT_CALLBACK_TIMING` records a naturally selected direct-unit WAIT's
scope entry, decision-ready-at-submission, request-call entry/exit and diagnostic
epilogue. Captured battle/player/physical-side/stack/round and callback ordinal identify
the event without reading a destroyed AI owner. Return, throw and unwinding
flags distinguish completed request calls from exceptions; return is not an
authoritative acceptance or presentation acknowledgement.

`NH_RENDER_LOAD_TIMING_LIMIT` bounds AI records per battle/player timing state:
default 1024, maximum 16384, with one overflow notice. Missing records after the
cap do not establish absence of WAIT. Hero-spell and other early-return paths
are not certified by this direct-WAIT record.

With animation logging at DEBUG or TRACE, the existing `NH_WAIT_END_TIMING`
also records absolute entry/exit times alongside its disjoint UI stage spans.
This UI record retains its existing external run/log bounds, independently of
the AI record cap. Compare steady-clock epochs only within one process. Match
events only when battle/side/stack/round and ordering are unambiguous; no AI
ordinal is invented for UI events; their side is the action's side, not a new
controller identity. Submission and UI intervals may overlap and
must not be added together. Epilogue times include aggregation/observer work
but exclude the final record's own output, not literal function-return latency.

Focused native checks pass in separate default/OFF (five), ON (five) and cap-one
(one) runs. They exercise actual selected WAITs, captured identity, monotonic
boundaries, logger disablement, rethrows and owner destruction. Seven natural
WAITs yield seven records normally or one record plus one notice at cap one.
These are callback/mock-submission checks, not graphical UI, CPU/GPU, physical
presentation or Windows performance acceptance. Follow the existing guarded
graphical-testing rules for runtime measurements; no speedup is established.

## Hero positive-proc presentation diagnostics

With the same render-timing opt-in and shared event budget, `NH_HERO_PROC`
correlates an accepted successful Hero Luck/Morale receipt with its queued stage,
stage execution, effect registration, first CPU draw return, midpoint and natural
finish. Scene, local action ordinal, source, side and effect identify the chain;
the existing animation ID joins registration to playback. One horseshoe still
represents simultaneous successful Luck perks; the eagle remains a separate stage.

`teardown` marks scene termination, not natural effect completion. Missing images
are reported without changing existing recovery behavior. Disabled or capped
records leave the chain incomplete; absence is not proof of a failed proc or draw.
The first-draw record follows `Canvas::draw` returning, not GPU presentation or
visible pixels. Pure flag/once-only-marker tests do not establish rendered
acceptance. Observe actual successful procs through the guarded graphical path;
do not force gameplay probabilities or infer successful rendering from receipts.
