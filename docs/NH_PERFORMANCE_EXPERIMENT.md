# Performance measurement and lifecycle guardrails

The transport architecture investigation is closed. Shipping execution remains queued, authoritative, in-process VCMI; no whole-game synchronous replacement or cooperative scheduler was accepted. The formation-only experiment did not establish a player-visible benefit. See [bounded findings](NH_PERF_RESULTS.md).

## Measurement contract

Pin source, client/facade, configuration, save/map, resource inventory, renderer, animation speed and workload. Preserve originals; use disposable profiles. Compare repeated matched/interleaved variants where practical, retaining failures/timeouts and action/state/RNG equivalence. Do not reduce forecast depth, legality or safeguards to obtain a faster result.

Separate adventure planning, combat callback work, server dispatch/completion waits, rendering and pacing. Wall-time sums are not CPU time; nested children must not be added twice to disjoint totals. ACK, CPU draw return and available images are not physical presentation. Clock domains and missing joins remain explicit. OS cache is not cold merely because a process is fresh.

BattleAI controller lifetime ends when its `battleEnd` callback runs, which can
occur after the human has dismissed the result window. Battle-end dispatch is
synchronous: a human receiver can wait for that dismissal before later AI
receivers receive their terminal callbacks. An adventure AI's `makeTurn` interval
can likewise include human decisions and result-window waits when it attacks a
human player. Neither interval alone measures AI computation or instant-combat
resolution. See [battle-end dispatch](../client/BattleEndDispatch.h) and
[player battle completion](../client/CPlayerInterface.cpp).

For instant combat, distinguish the accepted auto-resolve request, result receipt,
first visible result and result dismissal. Report observed bounds when captures
do not establish the exact transition. Keep callback totals separate: they omit
work outside those callbacks, and callbacks before auto-resolve began must not be
counted as its exclusive workload. Do not derive unmeasured resolution time by
subtracting callback totals or dialog dwell from controller lifetime.

The native `NewHorizonsExchangeReadParityTest.ShippingCommandNormalCallbackKeepsNaturalHeroAndUnitChoices`
fixture supports the test-only `NH_TEST_SHIPPING_TBB_LIMIT` selector: `default`,
`1`, `2` or `4`. Explicit selection seeds the fixture's game RNG identically and
adds its serialized state to the existing before/after exports. Numeric choices
bound TBB parallelism only during the existing callback/action loop; `default`
adds no bound. An unset selector preserves the original fixture behavior.
Use fresh isolated TEST profiles, diagnostics OFF and interleaved repetitions;
compare actions, exported state, both RNGs and nonclock counters before comparing
`callbackWallUs`. This is not a player setting or a production scheduling policy.
The shipping Hero-candidate loop now evaluates candidates serially in their
existing order, retaining candidate bodies, forecast depth and final selection.
The limit comparison above characterizes the former parallel loop; changing a
TBB limit does not make the current serial loop parallel. Preserve the test
selector's deterministic seed and state exports for subsequent comparisons.

Do not run concurrent builds/heavy tests during measurements. Graphical probes require the maintained silent guarded helper, isolated Xvfb, disposable saves and actual dummy-driver/process-identity checks before input. Owned absence is not parent reaping or exit-zero proof. Keep raw logs/captures/receipts private; do not change guard budgets to conceal an invalid run.

### Creature-wish forecast workload

`NewHorizonsGenieSupportAITest.SyntheticSevenVsThreeForecastMatchesCompleteOrderedPoolOracle`
checks the complete ordered wish pools, uniform means, selected recipient/action,
live state and both RNG domains in a synthetic seven-versus-three battle. Its
enumeration oracle shares the production spell-effect and pressure evaluators;
it is not an independent implementation of those mechanics or a saved-game replay.

Terminal creature-pressure forecasts use a fresh, parentless `DamageCache`.
When captured rules force dynamic damage queries, preparation retains obstacles
and the first pair's warmup, then omits unused remaining pair fills only if the
loaded calculator attests its canonical implementation. Parented or populated
caches, historical rules without Focus Fire, and unknown or failed capability
queries retain ordinary preparation. Calculation failures still propagate.
Never use this terminal cache as another cache's parent: child original-damage
queries could observe its partial pair snapshots. Regression coverage lives in
`CreaturePressureCachePreparationTest` and `DamageCacheObstacleVisibilityTest`;
the ordered-pool oracle above protects the consuming wish-search decisions.

For an isolated profiler workload, explicitly select
`NewHorizonsGenieSupportAITest.DISABLED_SyntheticSevenVsThreeForecastProfile`
with `--gtest_also_run_disabled_tests`. It executes one standalone wish search
and a second search within complete stack selection. The two reported intervals
exclude their evaluator construction; whole-process samples also include setup,
construction and assertions. Keep ordinary native-test resource/profile isolation.
The first search reports `FirstSearchDamage*` aggregate call counts and nanoseconds
for native payload preparation, bonus discovery and the inclusive Lua bridge.
The four native subregions partition payload time; do not add them to that parent.
This diagnostic binding ends before the second, complete stack selection. Its
clock overhead means the first interval is not an uninstrumented timing baseline.
An exit-zero target does not prove profiling succeeded: inspect the collector's
errors and actual sample stream before attributing CPU cost. Neither selector
establishes Windows performance or a speedup.

### Life Drain continuation reuse

Life Drain's Hero Action evaluator may reuse a completed continuation value within
one search. Every original enemy/friendly target pair still executes its cast and
accepted-action bookkeeping. Reuse requires the canonical loaded Shadow-only
effect and a native certificate that the friendly recipient's saved combat state,
form and Bloodrage bookkeeping stayed unchanged. Changed, unsupported or failed
projections retain ordinary evaluation. No entry survives the search. The
`LifeDrainCandidateValueReuseAITest` cases compare ordered original targets,
options, score bits, selected actions, live state and both RNGs with reuse disabled
and enabled; positive-hit assertions distinguish reuse from an all-fallback pass.
Elapsed-time comparisons remain separate from those semantic checks.

### Regeneration baseline reuse

Regeneration's Hero Action search may reuse completed before-cast healing
observations within that search. The key includes the environment, immutable
before-cast callback, ordered activation queue, side, player and an owning snapshot
of the complete parent DamageCache chain. Each candidate still performs its own
cast and after-cast forecast; target-specific scores are never cached.

Unknown or failing scoring-script capability checks retain ordinary evaluation.
An incomplete or failed baseline cannot publish reusable observations, and no
entry survives the search. `RegenerationBaselineReuseTest` checks exact scores,
state and both RNGs, changed keys, calculation failures and capability revocation.
Measure disabled/enabled reuse on the same binary and workload, with identical
diagnostic settings, before claiming a timing improvement.

## Existing opt-in diagnostics

`NH_RENDER_LOAD_TIMINGS=1` enables existing diagnostics only under their logger gates. `NH_RENDER_LOAD_TIMING_LIMIT` accepts ASCII decimal 1–16384; malformed/missing values fall back to 1024. Counters are bounded, and a cap notice means later events are omitted. Disabled paths do not read clocks or allocate diagnostic logs.

- Resource/load records cover reads, decoding, crop/scaling, queue waits and texture creation; stages can overlap. Successful texture hits need not log.
- Battle animation records include `animationAdvance`, `animationDrawReturn`, `projectileAdvance` and `projectileDrawReturn`. Raw `dt_ms` differs from applied `advance_dt_ms`; `pos_x/pos_y` are requested coordinates, not scanout.
- WAIT completion uses `NH_WAIT_END_TIMING` under global/animation DEBUG: initial/order waits, activation, facing completion and UI remainder. It is not CPU or GPU time.
- Hero-cue records use the child `animation.heroProc` DEBUG domain with their independent bounded budget, avoiding global animation DEBUG traffic.
- BattleAI aggregates distinguish callback totals, movement search, ordinary/waited exchanges and nested probe/lifecycle work. Preserve legacy per-call microsecond truncation and additive fine-work nanoseconds; child sums are subsets, not additional body time.
- `NH_HERO_ACTION_TIMING` additionally requires global and AI DEBUG logging. It
  uses the same opt-in environment and bounded limit, cached on first use. Its
  ordinal identifies a Hero Action attempt, not a creature callback. `loop_us`
  includes candidate-loop/join wall time and post-join diagnostic slot reduction;
  full/cached continuation sums overlap across workers and are not extra loop
  time. Submission wall excludes prior validation and ordinary wire construction,
  except Focus Fire's existing inline action-factory argument. These are elapsed
  intervals, not CPU, ACK or presentation measurements. Disabled paths sample no
  clocks or allocate candidate slots; the existing no-Hero return remains first.
  The additional exact opt-in `NH_HERO_DAMAGE_CACHE_TIMING=1` enables existing
  target-probe damage-cache clocks only for already admitted Hero attempts;
  it cannot bypass the base opt-in, either logger gate or the attempt limit.
  It is cached on first admitted use. With this extra opt-in OFF, the aggregate
  schema and nested cache-clock behavior are unchanged. When enabled, every
  admitted aggregate appends `continuation_full_{movement,best_target_waited,
  best_target_normal}_probe_cache_*` fields, including zero-valued fields when
  no candidate slots or continuation measurements exist.
  The fields report body/preflight/dynamic calls and additive nanoseconds,
  remainder nanoseconds, current/original operation traffic, cache hits,
  fallbacks, dynamic bypasses, fills, dead returns, failures and suppressed
  reentries. They reuse the ordinary probe-cache counter meanings: original
  fallback operations can nest current operations, and reentry during a measured
  phase is included in its wall interval rather than separately timed.
  Preflight, dynamic and remainder partition outer cache-body time. Cache work
  includes both primary-damage and DPS-score requests; do not equate its sum to
  the primary-damage scope alone or add it to enclosing probe/exchange time.
  Within dynamic damage, native_payload times native input construction;
  its sequential native_attack_context, native_physical_modifiers,
  native_hero_orders and native_finalization intervals share boundary readings
  and sum exactly to native_payload nanoseconds. Completed calls reach all four;
  exceptions record only reached intervals. These are nested wall-time regions,
  not additional elapsed time or CPU samples.
  bonus_preparation includes declaration initialization (and its first-use Lua
  bonusTypes call), effective bonus lookup and presence maps. lua_call includes
  input/output conversion, Lua execution and native callbacks, not pure Lua CPU.
  These sequential regions are subsets of dynamic wall time, not additional
  elapsed time; its remaining work includes Luck weighting and result cleanup.
  Calculation counters record reached initial, Lucky-second and unlucky-second
  outer attempts, including unwind, not successful results. Nested callback
  calculations remain inside their outer region; their child clocks and counters
  are suppressed, with suppressed region attempts counted separately.
  Operation-local in-flight bookkeeping is not aggregated. These extra clocks
  have observer cost; compare semantic results and OFF/ON timing separately.
  FULL continuations additionally report sequential child scopes for target
  construction (`setup`), explicit reachability update, movement search and
  conditional best-target selection. These do not exhaust FULL work; movement
  search may itself update reachability. Different candidates overlap, and each
  child's maximum need not belong to the candidate with the largest FULL time.
  FULL movement also forwards the existing movement-exchange timing sink. Its
  wrapper, body, probe and lifecycle fields nest inside movement time; they do
  not cover all path-search or attack-preview work. Simulated attack-call counts
  are not a count of every physical strike.
  `NH_HERO_ACTION_TYPE_TIMING` rows share the aggregate's attempt identity,
  opt-in/logger gates and bounded attempt budget. They group candidate elapsed
  time by numeric Order/spell identity after the loop joins, with counts, sums,
  maxima and nested continuation/movement/best-target subsets. The current
  candidate loop is serial. Compare both total cost and cost per evaluation;
  a frequently evaluated Order need not be the slowest individual evaluation.
  At most 64 named types are retained; the explicit overflow row preserves
  totals but prevents a complete type ranking. These rows exclude shared setup,
  final selection/submission and diagnostic formatting, and do not measure CPU
  usage or establish a player-visible speedup.
- Target-probe preparation begins after the invalid-actor/ended-activation guard and ends immediately before primary damage on the ordinary path; early returns and exceptions finalize it by RAII. Applied attacks and null timing sinks do not sample it. Movement, waited and normal contexts report it separately.
- Preparation, primary damage and DPS scoring are disjoint children of target-probe time, not extra terms in the seven-region exchange-body closure. Their remainder includes between-child work, post-work and measurement boundaries; it is not pure postprocessing. Focused native tests protect score/state/action/RNG parity and child bounds. Positive Overwatch early-return preparation-counter coverage remains unverified; adjacent reaction tests do not establish it.
- Windows compiler-command evidence in `tools/ci/windows_ai_compile_flags.py` records a privacy-safe supported flag subset and resolved response arguments. Configured optimization intent alone does not establish final compiler argv.

For guarded Wine diagnostics use only the typed full-game options and fixed admitted environment. Bootstrap/fixture arguments and C/Z mapping are held/checked by the maintained owner. A Wine proof is not native Windows performance or full runtime acceptance.

## Combat restart guardrail

Linux SDL3/OpenGL repaired transition was verified at `d21c7b5017fc`; failure was reproduced at `875b2c5591d1` following the `5229c808b15d` Windows report. Ordinary combat→Settings→Restart→Day1 and menu→new scenario no longer retained battle sprites/background/controls; normal Quit and immutable-file/process checks passed. The second game used the same fixture. Windows and quickload breadth remain unverified.

Protected shutdown ordering from `89bb7c693e5c`: `CServerHandler::restartGameplay` stops old network/gameplay waiters before destroying state. `CClient::endGame` detaches global battle interface, stops waiters, clears GUI, then releases battle. Do not reorder these responsibilities.

Clearing a scene must discard queued redraws, reset pending state and request full repaint. A retained old window must not repaint over a new scene. Queue invalidation alone did not resolve the visible defect.

Definitive scene teardown must release **all** `GpuRenderLayer` indices independently of widget activation. Opening settings deactivates the battlefield/release request, but covered-window full redraw can reclaim BATTLE. Clearing only the active top window cannot release the inactive lower battlefield again. `WindowHandler::clear()` marks all layers through the thread-safe deferred API; the next GUI frame clears released layers before windows draw. SDL2's release API stays a no-op. No artwork-cache flush, network-thread GPU destruction or late destructor release is required.

Run `ctest --test-dir build/new-horizons-linux -R '^nhWindowSceneRedrawTest$' --output-on-failure` and the source guard `client/tests/check-battle-restart-teardown.py` using a verified absolute Python interpreter. Then inspect ordinary battle restart and menu/new-scenario transitions for old windows, layers, queued paints and delayed callbacks. Record platform/build, actual exit/reap and containment evidence. Static guards are not rendered acceptance.

## Decoder and phase controls

`nhSdl3OutlineRuntimeTest` compares the bounded outline kernel to an independent pixel oracle without SDL initialization. `nhAnimationPhaseStartTest`/`nhProjectileFlightLifecycleTest` protect one-shot phase birth: a prior draw releases the defer; absence of drawing cannot stall forever. Movement uses the same stored effective interval and preserves per-hex residual.

`nhSdl3GrayscaleMaskRuntimeTest --interlace-regression` covers actual decoder mask/palette plus synthetic Adam7 pixel/alpha behavior; retain stderr because pixel PASS alone does not show warnings disappeared. `tools/ci/prepare_sdl3_image_interlace.py` checks the supported dependency patch; selecting source is not rebuilding/installing a shipping provider. System providers and Windows recipe source/notice graphs require their own evidence.

Current unresolved performance and acceptance breadth is in [integration gaps](NH_INTEGRATION_GAPS.md); recurring safeguards are in [regression guards](NH_REGRESSION_GUARDS.md).
