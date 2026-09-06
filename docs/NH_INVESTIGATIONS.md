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
