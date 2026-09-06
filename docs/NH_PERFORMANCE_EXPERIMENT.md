# New Horizons single-player execution experiment

## User objective

Measure command overhead, input latency and AI-turn responsiveness repeatedly,
stress the current implementation, and experiment with alternative execution
including same-thread calls. Present results before deciding whether architecture
changes are warranted. This authorizes bounded private-background product tests
and isolated experiments, not shipping a speculative architectural rewrite.
Keep the Windows download pipeline and frozen manual preview intact.

## User-approved stopping point

The user accepted the recommendation to finish only the in-progress bounded
formation-command comparison and then close this architectural investigation.
That experiment removes request serialization while retaining queued simulation-
thread execution; it is NOT a same-thread whole-game comparison. Complete its
native correctness checks and bounded graphical A/B measurement, document the
result and limits, and retain existing shipping execution defaults. Do not start
a cooperative-AI prototype, whole-game same-thread refactor or further expanding
transport experiments. Any substantial measured benefit can be brought to the
user for a separate decision, not treated as automatic expansion authority.

After this checkpoint, prioritize the downloadable Windows game and concrete
player-facing defects. The original broader experimental plan below is historical
scope and is superseded by this stopping point wherever it calls for more work.

## Questions and measurements

1. Transport: serialization, queue delay, dispatch/execution, response delay and
   total round-trip cost. Separate transport-only measurements from gameplay work.
2. Input: OS injection/event receipt, command submission, authoritative application,
   client application and first affected presented frame. Do not label event
   receipt or an ACK as visual response. Clock domains and correlation must be
   documented; missing stages remain missing, not fabricated.
3. AI turns: complete turn time, event-service gaps, frame intervals, longest stall,
   CPU use and memory. Separate AI compute from intentional animation pacing.
4. Correctness: equivalent accepted/rejected commands, results/state checkpoints,
   ordering, callback thread, cancellation, re-entry and shutdown. A faster variant
   that drops commands, skips validation/serialization unnoticed, changes game
   results or deadlocks is a failed variant, not an optimization.

## Variants and experimental isolation

- Baseline: current production internal connection and thread arrangement.
- Same-thread queued byte delivery: isolate thread handoff from message encoding.
- Same-thread direct byte delivery: preserve payload and processing work while
  removing the queue boundary in a bounded harness.
- Direct typed call: report serialization removal separately; use the same
  validated operation where feasible. Never equate a no-op call with full gameplay.
- Cooperative scheduling prototype only after basic thread/re-entry correctness
  is established. Service presentation safely; do not admit gameplay commands
  during partially applied state mutations.

Start with a benchmark using production connection/dispatch components. A fixture
or mock is explicitly a microbenchmark, not proof that a full engine thread mode
is safe. Full-game alternatives must remain experimental, disabled by default,
and must pass state/order checks before graphical stress execution. Never turn
InternalConnection delivery synchronous globally as an unreviewed shortcut.

## Method

Predeclare commands, map/save identities, AI players, settings, renderer, animation
speed, resolution, CPU/thread settings and seeds where controllable. Keep original
assets read-only, use disposable copies of test saves, and retain the accepted MVP
save originals. No cheats or direct mutations in normal-input acceptance runs;
synthetic fixtures are separate, labeled diagnostics.

Warm up before sampling. Microbenchmarks: at least 10 independent repetitions per
variant, bounded batches across representative payload sizes and bursts. Full-game
baseline: at least 5 independent bounded runs from the same frozen starting state
where practical; paired/interleaved alternatives after they are valid. Report
sample counts, p50/p95/p99 and max where supported, raw concise data, between-run
variation, instrumentation overhead, failures and timeouts. Do not compute
impressive tail claims from inadequate sample sizes or silently omit failed runs.
Use a small instrumentation-off/on comparison. Timing thresholds are observations
first, not invented pass criteria. Preserve a few useful screenshots, not video
by default. No unrelated local compilation/dependency build during timing runs.
Private Xvfb/software presentation is not physical-display/input latency or a
Windows/original-game benchmark. Audio-dummy limitations remain explicit.

## Exclusive ownership and execution

- Runtime: production-transport microbenchmark, server/lib/network experimental
  diagnostics and bounded same-thread alternatives; new benchmark/test sources.
  Pause local MinGW compilation at a safe checkpoint before timing runs.
- Frontend: client-side event/command/apply/present instrumentation and safe
  callback/re-entry assessment. Coordinate common trace schema with Runtime;
  do not modify its files. No game launches.
- Content/Tester: sole live graphical executor and independent measurement runner,
  fixture/run manifests and analysis under tools/tests and docs/NH_PERF_RESULTS.md.
  Private guarded Xvfb/XTest only; no host input/focus or original executable.
- Build/Integrator: CMake/build switches, serialized build and benchmark windows,
  review, scoped privacy-scanned commits/pushes, Windows CI/release continuation.
  Do not overwrite live binaries. Retain one assigned Linux build root; frozen
  experimental variants may be named inside it, not extra worktrees/toolchains.

Coordinate immediately through explicit tmux Enter messages. No further proposal
approval is required for these bounded implementations. Preserve shared dirty
MinGW scripts and unrelated work. Build must read real command exit statuses;
benchmark data and corresponding source/binary identities must agree. Do not
commit assets, saves, raw proprietary evidence or machine-private paths.

## Deliverable

One concise evidence-backed report: baseline breakdown, measured paired changes,
variance/tails, correctness results, limitations and recommendation. Include
negative findings and unsupported variants. No architectural switch becomes the
shipping default without the user's subsequent decision. If transport overhead
is negligible relative to AI/rendering/animation, say so plainly and identify the
measured bottleneck instead.
