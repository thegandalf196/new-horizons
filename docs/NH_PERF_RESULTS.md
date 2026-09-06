# New Horizons performance experiment — measured results

## Instrumentation-off baseline (2026-09-05)

**Five independent real-game starts completed**,22:29:19–22:38:13 UTC, during
Build's exclusive no-local-build/benchmark window. Each run used a fresh private
profile and identical copy of the preserved accepted save. All clients quit
normally; private guarded Xvfb stopped and timing lane explicitly released.
No physical display, original executable, Windows or architecture-alternative
performance claim follows from these measurements.

### Frozen inputs and conditions

- Immutable manual-preview source: `e6ce8c8e2327bcc846b1e590d6fd49be6bb9529f`.
  This exact snapshot had native-test acceptance and previous ordinary-gameplay
  ancestry, not a newly completed full graphical MVP gate on these bytes.
- Client SHA-256:
  `f6d5e3eee96c1c0af9f0604696c5ae0ce9e2d16fd14192d7b5e7d1d32513dee3`.
- Library SHA-256:
  `47990a1bc7d5b6ba2dd6fcb2c1cf5af22c8bcde99bf14b719d3fa24ca564e210`.
- Starting save SHA-256:
  `43461dd60926975294da8b5224a3a70a49cc69efd8693644476485084a4ca00d`.
  Original **A Warm and Familiar Place**, Week2Day1: Ash XP166,4 Imps/6 Gogs,
  mana5/10, movement319/1560, gold19450; wood23/mercury16/ore15 and10 each
  sulfur/crystal/gems. Loaded saved state/RNG; no new scenario seed selected.
- AMD Ryzen5 5500X3D,6 cores/12 logical CPUs, boost enabled, ordinary scheduler,
  no affinity/governor changes. Background desktop/agent services remain noise;
  no claim of an isolated bare-metal benchmark machine.
- Private Xvfb1280×800, client window1280×720 at0,0. SDL2 **OpenGL renderer**
  confirmed in logs; physical scanout and underlying GPU/software rasterizer not
  independently identified. Requested vsync=true, targetfps60; achieved frame rate
  unavailable without instrumentation. Hero/enemy movement150ms; object/terrain
  animation=true. Dummy audio; music/sound88. Existing normal logging retained.
- Logs identify one green **Nullkiller2** opponent and **BattleAI**. Shipping
  performance instrumentation absent; `NH_PERF_TRACE` explicitly unset.

### Protocol and measurement definition

Each fresh process loaded the same save, waited at least5s, and opened Ash's
hero window. A bounded XTest click closed that window; a32×32 static hero-panel
region was observed becoming the uncovered map. Then ordinary End Turn opened
its movement-reminder confirmation; a second measured click confirmed the turn.
A165×14 date-label region was observed changing from Day1 to Day2. This is a
**game-state visual response**, not an ACK, and **not an exact complete-AI-turn
boundary**. After observation, a1s wait and normal hero-window opening confirmed
human interaction/state; no gameplay input was injected during the AI interval.

`tools/tests/nh-perf-pixel-probe.py` uses ctypes XGetImage on the private root,
not subprocess screenshots in the timed loop. Before injection it requires25
unchanged ROI samples, approximately250ms. It records Python `monotonic_ns`,
injection begin/flush-return, each capture begin/end/hash, and bracketed wall-clock
mapping. Nominal10ms sleeps yield approximately10ms uncertainty. Bounds below
span the prior unchanged capture's start to first changed capture's completion,
relative to injection begin. They are **interval-censored external observations**,
not exact presentation timestamps or guaranteed observation of every transient.
Unrelated animation is checked by the stable precondition and separate visual
inspection; raw status deliberately stays `changed_needs_attribution`.

### Raw per-run results (milliseconds)

| Run | Client PID | Hero-window close → changed ROI | Confirm End Turn → Day2 ROI | Existing NK2 scoped timer |
|---|---:|---:|---:|---:|
|1|823837|[10.490,20.864]|[165.606,176.006]|128|
|2|823977|[10.480,20.836]|[165.996,176.350]|128|
|3|824145|[10.472,20.870]|[175.975,186.359]|134|
|4|824243|[0.177,10.540]|[155.301,165.652]|125|
|5|824344|[0.196,10.541]|[156.253,167.049]|125|

- UI median observation interval: **[10.472,20.836]ms**; maximum-upper-bound
  sample **[10.472,20.870]ms** (run3). Day-label median interval:
  **[165.606,176.006]ms**; maximum sample **[175.975,186.359]ms**.
  Endpoint-wise extrema need not describe the same UI run. No midpoint-as-exact,
  population tail or p95/p99 claim from five samples.
- Each ROI capture+hash took at most0.121ms across these probes; largest observed
  between-poll start gap11.189ms. These are **observer costs/gaps**, not game frame
  intervals or event-service stalls. Polling still perturbs X server scheduling.
- Process CPU deltas during day probes:33/35/35/34/34 ticks at100Hz, i.e.
  aggregate330/350/350/340/340ms across threads—not AI-only time. Short UI deltas
  of1–2 ticks are too coarse for meaningful CPU percentages. Post-probe RSS:
  UI331.348–331.543MiB; day339.234–339.637MiB; these are samples, not peak memory.
- Existing production log `PERFORMANCE: NK2 makeTurn took` reports125–134ms,
  median128ms. `AIGateway::makeTurn` times only `nullkiller->makeTurn()` with
  `high_resolution_clock`, excluding its surrounding setup/finalization. This
  is useful **scoped AI work**, not the entire turn or a decomposition of transport,
  animation and presentation. Do not subtract it from pixel bounds as an exact
  overhead estimate. Internal trace-clock calibration remains pending.

### Correctness, failures and preserved evidence

All five initial/next-day screenshots were inspected. Final checkpoints matched:
Week2Day2, XP166,4 Imps/6 Gogs, mana6/10, movement1560/1560, gold19950 and
unchanged other resources. Town/position/fog remained consistent. Both timed
observations succeeded in every run; no probe timeout or unstable-precondition
rejection. All five clients exited normally. Snapshot hashes, preserved original
save and all five starting save clones remained unchanged.

One **setup failure before any client launch** is retained: placing disposable
profiles inside the repository/assets ancestry triggered the launcher's overlap
refusal. Profiles were moved outside that ancestry; this was not discarded as a
slow sample or treated as product gameplay failure. Every run also logged the
same existing warning: `AINodeStorage had 2716 nodeAllocationFailures in previous
pass`. No claim that all internal operations were warning-free; visible continuation
matched, and the warning is reported for Runtime review rather than silently ignored.

Local-only evidence: `build/new-horizons-linux/testing/perf-off/manifest.json`,
`summary.json`, `run{1..5}-{ui,day}.json`, client identity JSON/logs,
`final-hashes.txt`, `01-frozen-state.png`, `02-day2-state.png`. Raw files retain
clock brackets, sample hashes/capture durations and proc counters; proprietary
screenshots/logs/saves and machine-private profile paths are not committed.

## Remaining experiment and recommendation

Complete AI-turn/service-gap/frame-stall attribution, input receipt/submission/
application stages and affected-present correlation require the opt-in instrumented
candidate. Compare that **same binary envOFF versus envON**, with paired/interleaved
fresh-save repetitions; the older shipping-off snapshot alone cannot isolate
instrumentation overhead. Use the same ROI corroboration, and add a declared legal
hero-move action for renderer-supported affected-present matching. During longer
AI stress intervals only legal non-gameplay UI/cursor probes are appropriate.

Microbenchmark variants and full-game same-thread alternatives have not been run
by Tester yet. No architectural default change is justified by this baseline.
The present data establishes reproducible scoped observations and state continuity,
not that transport is a bottleneck or that a direct-call engine is safe/faster.
