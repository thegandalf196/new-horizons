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

## Combat evaluation regression investigation

A reported 45.829-second adventure-AI turn spent approximately 45.739 seconds
between battle-AI creation and post-battle result processing. State-update
durations were only 0–4 milliseconds. Trace gaps repeatedly surround movement
and spell-candidate evaluation; this is not evidence of transport delay.

The movement evaluator calls damage estimation while exploring enemy approach
positions. Physical melee estimates query several Offense perks.
`CGHeroInstance::hasActivePerk` currently calls `PerkState::project`, which calls
full registry validation and repeats it through selected-perk lookups. Even an
absent perk query pays that cost. A native positive-speed movement-evaluator
reproduction must establish baseline timing and verify unchanged decisions before
optimizing this path. Preserve strict validation at authored-state initialization,
selection, and save loading; do not replace the problem with per-frame polling.

A separate Leadership rejection occurred during post-battle result processing,
after the long evaluation interval. Necromancy's slot-only destination preflight
is a candidate defect, not yet a confirmed attribution of that rejection.

### Focused native result

`NewHorizonsMagicAITest.RepeatedMovementEvaluationWithSavedPerksIsStableAndReadOnly`
now exercises positive-speed melee movement against two unreachable enemies with
saved selected Offense perks. Three evaluator samples before optimization were
102425, 102411, and 102394 microseconds; after the direct selected-perk lookup they
were 1651, 1540, and 1520 microseconds. This is a small synthetic diagnostic,
not a full-game speedup estimate or a tail-latency measurement. Repeated decisions
were stable within each run and authoritative state bytes remained unchanged.
The combined 66-test Mana-capacity, perk-state and magic-AI suite passed afterward.
The optimized query reads the saved registry and current rank without a cache;
strict validation remains at initialization, selection and loading boundaries.
Replaying the originally reported match remains unverified.

### Post-battle correctness follow-up

Necromancy's former destination check accepted a matching stack without checking
its remaining Leadership capacity. The server then rejected the increase, even
when another slot could hold the award. The replacement preflights the complete
award across matching stacks and empty slots, using the same per-slot capacity
rule as server validation. Dark Conversion previews and post-query delivery use
the same planner. If the complete result cannot fit, no creatures or Black
Harvest mana are awarded; existing troops are unchanged. This preserves the
resolver's all-or-nothing policy rather than inventing partial rewards.

A separate full-flow native test exposed a defeated hero retaining a borrowed
battle pointer after moving into the hero pool. Battle destruction could no
longer find that hero through its map ID, and subsequent mana reconciliation
dereferenced the stale pointer. Removal now clears the link before pool transfer,
after the active-battle guard and bonus detachment. The regression checks pooled
hero rule and mana access after battle finalization. This is a lifecycle repair,
not a return to per-update mana polling.

Validation: the combined 116-test native Necromancy, Spell Point state/capacity/
reward/presentation, perk-state and combat-magic AI run passes. Five full-flow
admission cases cover a capped matching stack with spare space, no remaining
capacity, room spread over duplicate stacks, mixed-output choice filtering, and
capacity lost while a conversion query is pending. Client and test targets build;
the Necromancy summary UI source check also passes. No graphical run or replay of
the original slow match is included in this result.

### Remaining combat cost after the route correction

A bounded headless `All for One` new-game check of commit `d9774fb54`, with
seed `1284510375`, completed 43 AI turns in a 35-second observation window.
It used a separate temporary profile, dummy media drivers, a 200% CPU quota,
and no autosaves. The final in-progress turn was stopped by the planned timeout.
No checked crash, failed command, Leadership rejection or capability-load error
appeared. Path-node allocation warnings were still present. This is not a replay
of the original slow match, a graphical test, or a controlled before/after pair.

The longest completed turn was blue's day 4, at 5313 milliseconds. Its battle
began about 94 milliseconds after the turn began and ended shortly before the
turn finished. Timestamped `activeStack` entry/exit pairs account for 5153
milliseconds across 72 decisions, with a maximum of 247 milliseconds per
decision. Post-battle state updates were around 6–8 milliseconds. Thus this
outlier is cumulative combat evaluation, not one multi-second adventure-path
update or evidence of a transport stall.

Movement checks against unreachable enemies dominate the visible trace in the
slower neutral decisions. The next diagnostic should cover a dense mixed-melee
battle and preserve complete action choices and authoritative state. Repeated
bonus-list copying in unmodified hypothetical stacks is a source-level candidate,
not yet a measured bottleneck. Neither reducing search depth nor dropping
mechanics is an acceptable substitute for measuring and removing redundant work.

### Dense decision diagnostic and rejected bonus-list experiment

`DenseBattleEvaluationWithSavedPerksIsStableAndReadOnly` constructs ten mixed
melee stacks, selects a defender Pikeman's action against seven enemy stacks,
and records three fresh evaluator-construction/stack-action samples. It compares
every current action field and ordered target, as well as serialized live state
and unit snapshots. Commands are disabled: this is stack-action evaluation, not
complete hero-spell selection or whole-combat timing. The fixture is synthetic,
not a reconstruction of the original match.

Baseline samples were 20344, 21637 and 21282 microseconds. An experimental early
return from `StackWithBonuses::getAllBonuses` for completely untouched overlays
gave 19517, 21247 and 21093 microseconds. The complete recorded action signature
matched exactly (WAIT), and all 15 focused diagnostic/timed-effect tests passed
with and without the experiment. The sparse movement diagnostic was likewise
essentially unchanged: 1666/1528/1493 versus 1661/1525/1512 microseconds.

This small, unpaired sample does not establish a meaningful improvement. The
production experiment was therefore removed; the dense diagnostic and nested
bonus-list lifetime/invalidation/overlay-isolation tests remain. No new cache,
reduced search depth or altered battle rules were introduced. The remaining
combat cost still needs narrower attribution, especially in later crowded
positions rather than only separated opening formations.

The wider battle-AI validation passed all 66 tests across five suites after
making `HypotheticCloneTest` explicitly use legacy magic rules. Its existing
Clone lifecycle tests had inherited the New Horizons roster, where `core:clone`
is intentionally inactive, so their setup casts were correctly rejected. This
fixture correction preserves all lifecycle assertions and does not enable Clone
in New Horizons or loosen authoritative spell validation. The diagnostic-only
changes do not replace the currently promoted playable snapshot.
