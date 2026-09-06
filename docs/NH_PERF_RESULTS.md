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

## Frozen experimental checkpoint and actual microbenchmark

Tester executed the frozen snapshot `perf-preview-fd5abac02` on2026-09-06:
source **fd5abac02a216e51b18a5a581f6e13bf978d39a0**, client SHA-256
`1f001505a802d99de6c61970c2105836241e80263262a40e06108b82d003b1fe`, library
`fdb3490eb91abbe13cff0923b01142729df9cbf6c960c9c49b7e137e13b73671`, vcmitest
`469d6edfde1c158c685939caf64d8c803a60ca3d4591c73f8cc1f8013a73d46c`.
Build reported post-commit build0,66 native PASS/5 expected SKIP and3 explicitly
activated correctness tests PASS. Tester independently ran the actual sweep,
not the mutable test binary, with its frozen library and private native XDG.

**00:39:50–00:40:23 UTC, timeout300s, EXIT0**, XML1 test/zero failures,
31.961s test duration. Command filter:
`NH_RUN_TRANSPORT_BENCHMARK=1 .../vcmitest --gtest_filter=InternalConnectionBenchmark.PayloadBurstSweep`.
No GUI, concurrent benchmark or local build during this exclusive window.

Exactly **960 records**,4 variants ×4 payloads ×3 bursts ×10 repetitions ×2
trace modes,256 measured operations/32 warmups per session:245760 measured and
30720 warmup operations. All recorded correctness flags true/dropped counts zero;
480 ON records retain raw per-operation RTTs. Runtime independently analyzed the
same raw records; its CSVs retain all repetitions and paired trace costs.

Median OFF batch elapsed per completed command (**microseconds**, not RTT):

| Payload/burst | Threaded bytes | Same-thread queued bytes | Direct bytes harness | Direct typed harness |
|---|---:|---:|---:|---:|
|64B/1|14.875|1.223|0.945|0.110|
|4KiB/16|19.656|31.311|30.649|3.176|
|64KiB/64|271.475|517.995|482.626|50.206|

**Negative finding:** the existing threaded byte pipeline wins large-burst
throughput here;64KiB/64 is1.91× faster than same-thread queued bytes. Small
isolated requests benefit from handoff removal, but there is no universal
same-thread win. Typed mode removes **both directions' serialization** in a
synthetic validated operation, not full gameplay or a request-only optimization.

For64B/1 threaded ON, pooled2560 RTTs p50/p95/p99/max:
10.269/12.343/14.728/34.224us. Mean phases in us: encode0.521,
request delivery4.678, decode0.339, validated work0.097, response encode0.179,
response delivery4.757, response decode0.127. Delivery includes copy/post/wait/
listener dispatch—**pure queue wait is not isolated**. OFF batch cost and ON RTT
have different endpoints; do not interpret their difference as negative overhead.

Ten-run OFF CV reaches15.9% threaded,29.1% queued and30% direct bytes across
cells.64B/1 ON/OFF median batch-cost ratios are1.02/1.16/1.20/2.29 respectively;
empty typed work reaches5.5×, showing instrumentation can dominate tiny operations.
Per-variant OFF-before-ON ordering remains a microbenchmark limitation. Direct
re-entry reaches depth2 versus queued depth1: matching synthetic state does **not**
establish callback/re-entry equivalence. Queued-close cancellation was tested;
no safe synchronous whole-game replacement is established.

Local evidence: `testing/perf-micro-fd5/` below the Linux build root, including
raw stdout/stderr, exit, XML, manifests/final hashes, Runtime's
`runtime-findings.md`, `runtime-repetitions.csv` (960 rows),
`runtime-variant-summary.csv` (96 ten-run cells), and `runtime-comparisons.csv`.

## Same-binary paired graphical OFF/ON execution

**00:43:30–00:52:09 UTC**, same enabled fd5 snapshot,5 fresh-save pairs in order
OFF/ON, ON/OFF, OFF/ON, ON/OFF, OFF/ON. No local compilation or concurrent timing.
Each launch had a600s safety bound. Options → Quit was requested and all10
client PIDs disappeared; all5 ON traces flushed with **zero dropped records/
metadata evictions**. Exact client/launcher exit statuses were **not captured**
for these10 runs (nor the older5 graphical baseline runs). Historical
`normal_exit_confirmed_ns` fields establish process disappearance after the quit
request, not exit0; the ON trace additionally establishes shutdown flushing.
Only the separate microbenchmark has an actual recorded EXIT0. Later helper
hardening records the bounded launcher's real status via an argv-only supervisor
and requires zero, including treating timeout124 as failure; that is not retroactive
evidence for these completed graphical runs. Private Xvfb stopped,
quiet lane released; frozen binaries, original save and10 input clones unchanged.
This compares environment trace OFF/ON on the same build, not different binaries.

Kept prior save/settings/renderer and UI/day probes. After Day2 human turn returned,
selected one legal south tile and measured its execution click. Move ROI8×8 at
(526,360), execution click(544,369), outside the path-marker/flag region. The
camera follows movement: the changed scene can include camera motion, so this is
**move-caused visible scene response**, not a guarantee the first changed pixel
is exclusively the hero sprite. Initial path selection is outside the timed probe.
Trial1 first selected a longer path without executing it, then replaced it with
the verified single-step path; subsequent trials used the corrected coordinates.

All30 stable-precondition ROI observations succeeded. Manual inspection of the
first pair and final run, plus exact hero-panel/resource crop fingerprints for
runs3–10, verified Day2/XP166/4 Imps6 Gogs/mana6/gold19950 and movement1460 after
the one-tile step. Source saves were never overwritten. The first two launches
included manual inspection delays; warmup was at least5s in every run, not an
identical process-age interval. Later runs used the bounded repeatable launcher/
action helpers. No failure was silently retried or excluded.

Per-run externally observed intervals, milliseconds:

| Run/mode | UI close | Day2 label | One-tile move scene |
|---|---:|---:|---:|
|1 OFF|[10.504,20.859]|[166.529,176.879]|[20.792,31.160]|
|2 ON|[10.576,20.942]|[167.495,177.869]|[20.806,31.194]|
|3 ON|[10.448,20.794]|[156.454,167.279]|[20.824,31.166]|
|4 OFF|[10.474,20.834]|[155.233,165.661]|[20.763,31.095]|
|5 OFF|[10.534,20.871]|[165.576,176.051]|[20.762,31.117]|
|6 ON|[10.436,20.768]|[155.425,165.764]|[20.771,31.078]|
|7 ON|[10.591,20.960]|[155.458,166.242]|[20.779,31.109]|
|8 OFF|[0.196,10.544]|[155.321,165.705]|[20.894,31.261]|
|9 OFF|[10.570,20.958]|[165.806,176.167]|[31.268,41.594]|
|10 ON|[10.443,20.803]|[155.888,166.219]|[10.488,20.826]|

Median intervals OFF versus ON: UI[10.504,20.859] vs[10.448,20.803];
day[165.576,176.051] vs[155.888,166.242]; move[20.792,31.160] vs[20.779,31.109].
These observations do **not** demonstrate that tracing accelerates gameplay or
that overhead is zero. Most paired differences span zero; one UI pair is slower
ON and the last move pair faster ON. Poll quantization, frame phase, short runs
and only5 pairs prevent a reliable tail or small-overhead conclusion.

Day-probe aggregate CPU samples: OFF340–350ms, ON330–350ms; scoped existing
NK2 timers: OFF127–135ms, ON129–134ms. Post-move RSS samples:
OFF311.746–312.738MiB, ON312.191–313.617MiB (not peaks/maximum trace-buffer cost).
Do not attribute the older shipping snapshot's higher RSS to instrumentation:
that comparison changes binaries and is not paired.

Local evidence: `testing/perf-paired-fd5/` under the Linux build root,
`manifest.json`, `external-summary.json`,30 ROI records,5 client traces,
per-run correctness/identity records and logs, frozen hashes and two named
move screenshots. `nh-perf-launch-trial.py` and `nh-perf-paired-trial.py` supply
repeatable normal-input setup, bounds, state-fingerprint and trace-loss checks.

### Actual AI/service trace findings

Frontend independently analyzed the5 loss-free ON traces; these are
**client-observed green-player notification intervals**, not isolated AI CPU time:

| ON run | Green turn interval ms | Max interior poll gap ms | Max interior present gap ms | Max frame-lock wait ms |
|---|---:|---:|---:|---:|
|2|131.973|16.316|16.316|0.348|
|3|131.496|16.909|16.908|0.083|
|6|134.592|28.968|28.968|0.155|
|7|135.809|28.469|28.469|0.231|
|10|136.989|16.412|16.412|0.122|

Each interval contains8 poll,8 presentation and8 input-service records,457
client-state applies and81 command submits. These maxima cover consecutive
records **inside** the interval, not edge-straddling gaps or entire-run maxima.
No `input_captured`/`input_intentionally_drained` marker occurred in these intervals.
No p95/p99 claim is made from these short five-turn observations. OFF has no
comparable internal frame/poll trace; this is not an OFF/ON stall comparison.

**Important negative finding:** small recorded frame-lock waits do not exclude
GUI locking delays. In runs6/7, SDL user-event receipt to main-callback entry was
27.061/26.360ms, while callback bodies were about1us. SDL_USEREVENT processing
acquires `interfaceMutex` **before** the callback-enter marker. The observed delay
therefore includes uninstrumented locking/scheduling; it is not a pure mutex-wait
measurement or evidence the callback body/transport consumed27ms. Observed maximum
apply/request/main-callback nesting depths were1/2/1, not proof of re-entry absence.

### Actual move correlation and clock limits

All5 ON trials have unique receipt/dispatch event543 (SDL button-down1025), real
command request90, semantic move result apply547, and matching draw/present frame
890/888/889/889/886 respectively. The `matched_request_id` is a semantic candidate,
not an ID present in the result packet. State checkpoints and the move-caused ROI
change corroborate the controlled single-step action; camera motion prevents
claiming a uniquely identified first hero-sprite pixel.

Same-C++-clock stage durations, milliseconds except receipt→dispatch:

| ON run | Receipt→dispatch us | Receipt→submit | Submit→state | State→draw | Draw→present return | Receipt→present return | Submit→ACK |
|---|---:|---:|---:|---:|---:|---:|---:|
|2|10.530|0.984|0.790|14.941|14.447|31.162|161.219|
|3|9.869|0.871|0.806|14.789|15.326|31.793|161.551|
|6|9.638|0.864|0.782|15.809|14.468|31.923|160.601|
|7|11.181|0.939|0.773|14.951|15.572|32.235|161.548|
|10|16.471|0.937|0.777|14.871|13.597|30.182|160.172|

The transport-submit call scope is only12.644–14.017us: **not end-to-end transport**.
State-applied→after-visitor return159.343–160.724ms is consistent with declared
150ms animation plus frame pacing, not proved entirely animation work. ACK arrives
about130ms **after** the affected-present candidate. It is emphatically not the
visual-response endpoint. Authoritative server application and pure queue-wait
stages remain absent in these full-game traces; client apply is not a substitute.

Python/C++ epoch equality was **not assumed**. For unique move receipt C and
external injection/capture P, offset Csteady−Pmono is bounded by
`[C_receipt−P_changed_capture_end, C_receipt−P_injection_begin]`:
run2[-25.608,5.585],3[-21.841,9.326],6[-21.508,9.570],
7[-26.078,5.031],10[-19.354,1.472]ms. Tester independently recomputed these from
raw traces/probe files. A **conditional constant-offset** intersection across the
five runs is[-19.354,1.472]ms; containing zero does not prove clock equality.
No exact cross-clock input→present latency is derived from this broad anchor.
The renderer draw→present-return span is CPU/API timing, not scanout. Root pixels
may already have changed before the API returns; do not force capture intervals
to intersect the return instant or invent a timing contradiction. Frontend's
local `analysis-client.json` retains source hashes, raw joined records, all clock
bounds/lock spans and limitations; `analysis-client.csv` has the5 concise rows.
Tester read the CSV and independently recalculated the causal clock bounds.

### Recommendation

Do **not** switch the shipping architecture based on these results. The benchmark
establishes microsecond-scale small-packet overhead but synthetic large bursts can
favor the existing pipeline; full-game AI work and rendering have different scopes.
Direct re-entry is observably different. The final narrowly validated formation comparison below completed that remaining
bounded action. No whole-game same-thread variant or default change was accepted.

## Final formation-only A/B — experiment CLOSED

At the user's stop-loss decision, Tester completed **only** the already-in-progress
formation comparison:2026-09-06 **01:58:39–02:05:56 UTC**, exclusive quiet lane,
5 fresh alternating byte/typed pairs,2 real formation changes per trial.
No AI-turn extension, cooperative scheduler or same-thread engine was introduced.

Frozen source `187604d2daf2687850ca4fa603548d5e4e032b7d`, snapshot
`perf-formation-preview-187604d2d`:

- Client SHA-256:
  `2e953f105d89b0f04182751cba78be8e20a7012089870691f2bb6fa6fec99036`.
- Library:
  `374610d8e207ef1e7e68f7f7193da534a7c5536ea7e476bd7aa9b40a3ba39d38`.
- Test:
  `6ab02dd8b20c5cc5f0656efd02b9a00d50787af691e65c4da08a67cff7130d7f`.

Build reported post-commit build0,11/11 native proofs including12 real validation
arms,66 broader regression passes, opt-in skips when unset. Graphical A/B used
traceON **both** sides; `NH_PERF_QUEUED_SET_FORMATION` unset versus1. This is
**queued typed request delivery**, preserving the authoritative executor and
response-byte path—not a same-thread result.

### Actual state and route proof

Same frozen43461dd6 save and prior settings. Loaded Ash's hero window, clicked
the other formation (tight), closed/reopened the window, then restored loose and
closed/reopened again. Two distinct inspected hero-panel fingerprints established
changed state and restoration to the exact initial panel in every trial. Day1,
XP166,4 Imps/6 Gogs, mana5, movement319 and resources remained unchanged; no
turn, movement or resource-spending action was added.

Each of10 traces has exactly2 SetFormation submits,2 ChangeFormation state applies
and2 successful ACKs, request IDs6/7 for player0. Every typed trial has exactly2
**actual `formation_typed_queued` markers** matching those request/player IDs;
every byte trial has zero. Variant classification is not inferred from environment
alone. All traces are loss-free. **All10 exact bounded-launcher exit statuses are0**,
not just PID disappearance, using the hardened supervisor. All starting saves,
original accepted save and frozen binaries remained hash-identical. Owned Xvfb
stopped and quiet lane released02:05:56Z.

### Measured result — no substantial benefit surfaced

20 total commands,10 per route, clustered as2 commands in each of5 independent
processes per route. Runtime independently analyzed the raw traces. Medians
**across the5 per-process means**, not20 independent samples:

| Observable | Byte route | Queued typed route |
|---|---:|---:|
|Caller send-call scope|14.803us|5.766us|
|Submit→client-state apply|597.716us|613.773us|
|Submit→ACK|633.650us|641.014us|

The5 paired caller-scope deltas are -9.403/-8.642/-9.429/-9.038/-7.655us,
mean **-8.833us**: a repeatable narrow caller-side saving. This is not an isolated
server/queue timer or a whole-operation win. Mean paired submit→state difference
is **+12.538us**, and submit→ACK **+13.428us**, typed minus bytes. ACK is not state
or visual proof by itself.

Descriptive pooled external control-ROI median intervals are byte
[10.450,20.826]ms versus typed[10.473,20.835]ms. Pooled command medians (retained
in `tester-summary.json`) differ from the per-process summaries above; pooling
must not inflate the independent sample count.

Per-pair mean submit→client-state times, microseconds:

| Pair/order | Byte | Typed | Typed minus byte |
|---|---:|---:|---:|
|1 byte/typed|651.494|672.002|+20.508|
|2 typed/byte|597.716|661.963|+64.247|
|3 byte/typed|576.461|586.250|+9.789|
|4 typed/byte|592.763|613.773|+21.010|
|5 byte/typed|632.893|580.029|-52.865|

There is a narrow caller-side saving, but no consistent whole-operation improvement
or substantial player-visible benefit. This small noisy comparison is not proof
of an intrinsic typed whole-operation penalty either.
No p95/p99 claim from5 pairs. Trace overhead is present in both arms; the first
byte run had longer manual inspection delays than subsequent automated runs,
retained rather than discarded. Initial/changed/restored screenshots establish
fingerprint meaning; remaining checks used the same exact cropped-state hashes.

The48×68 external ROI observes **optimistic formation-control selection**, not an
independently attributed authoritative-result frame. Close/reopen readback plus
ChangeFormation applies verifies eventual real state; ACK is not visual response.
Submit→client-state includes scheduling/response dispatch and is not isolated
request serialization/server validation. No new affected-render candidate exists
for formation, and no exact input→authoritative-present latency is claimed.

Independent Runtime analysis agrees: mean paired caller-scope saving8.8331us
(60.4% of that tiny call), but client-state/ACK means are12.5379/13.4284us later
for typed, with4/5 state pairs slower. This demonstrates only a narrow caller-side
saving, **not end-to-end or visible benefit**. Return-path PackageReceived timing
is not pure delivery; no authoritative queue/apply timestamps were measured.
Derived `runtime-findings.md`, `runtime-formation-commands.csv`,
`runtime-formation-analysis.json`, `runtime-formation-variation.json` and
`runtime-formation-phases.csv` preserve independent matching, phases and ranges.

Local evidence under `testing/perf-formation-187/` in the Linux build root:
manifest/final hashes,20 external toggle probes,10 traces,10 exact exit-status
files, per-trial correctness records, `tester-summary.json`, the Runtime files
above, and3 named state screenshots. `tools/tests/nh-perf-formation-trial.py` reproduces this fixed bounded
route with state/trace/variant/exit checks; no further run is authorized by the file.

**CLOSED:** preserve VCMI's existing architecture and shipping defaults. No
whole-game same-thread implementation, cooperative AI or further transport
experiment is recommended/continued. No substantial measured benefit arose to
escalate for a new architectural decision. Next priorities are the actual
downloadable Windows artifact and concrete gameplay defects; existing previews,
saves and unrelated MinGW work remain preserved.
