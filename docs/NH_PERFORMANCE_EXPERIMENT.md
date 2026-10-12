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

### SDL3 resource-loading diagnostics

Set `NH_RENDER_LOAD_TIMINGS=1` and enable the global DEBUG log level to collect
`NH_RENDER_LOAD_TIMING` records. These diagnostics are disabled by default.
They cover resource reads, SDL image decoding, image/surface bindings, cropping,
scaler preparation, asynchronous queue wait, scaling kernels and texture-cache
miss creation. Successful texture-cache hits do not emit records.

Each of the three instrumented translation units defaults to at most 1,024
records and emits one overflow notice identifying its translation unit and actual
cap. For an explicitly larger diagnostic window, set `NH_RENDER_LOAD_TIMING_LIMIT`
to an ASCII decimal integer from 1 through 16,384; missing, empty, zero, signed,
whitespace-containing, nondecimal or out-of-range values fall back to 1,024.
The limit is cached per translation unit only after diagnostics are enabled;
disabled draws do not parse it, increment event counters or sample timing clocks.
The opt-in and global DEBUG requirement remain unchanged. No default rendering,
asset or caching behavior changes. A notice means later loading events are omitted;
do not treat a capped trace as a complete count. Resource labels are virtual
identities, not workstation paths. Pointer values correlate objects only within
one process.

Durations are microseconds of steady-clock wall time. Nested stages overlap;
do not add them as independent costs. Texture creation measures the CPU call,
not GPU completion or frame presentation. Logs and capture add observer overhead:
compare equivalent instrumentation-off runs before attributing a regression.
These records do not by themselves measure animation cadence, frame rate, cold
OS caches or performance on another platform.

For guarded full-game Wine tests, explicitly opt in with
`--wine-render-load-timings`, optionally adding
`--wine-render-load-timing-limit 16384`. The launcher accepts canonical decimal
limits from 1 through 16,384; the enabled default is 1,024. Both options are
full-game-only, and a limit without opt-in is rejected. The owner forwards only
the two fixed diagnostic variables and verifies their actual Unix readbacks
before each gesture. Disabled runs do not inherit diagnostic variables.
The existing Win32 startup witness does not independently report this cap;
trace activation is not proof of uncapped coverage or improved performance.

### Battle animation diagnostics

The same opt-in settings enable a separate `BattleAnimation` record budget.
`animationAdvance` records active animation frame progression and elapsed update
time; `animationDrawReturn` records the selected frame and CPU draw-call duration.
`projectileAdvance` records flight progress and removal eligibility;
`projectileDrawReturn` records missile draw attempts, selected directional frames
and visible progress. Idle, dead and Frozen creature poses are not sampled.
The budget uses the same default and maximum limits described above.

`steady_us` orders these events within one process. A draw return is not a
presented frame or GPU completion; `imageAvailable` means the image lookup
succeeded, not that a pixel reached the display. Pointer identities can be reused
after object destruction. Compare first-use and repeated attacks at unchanged
animation speed alongside resource-loading records. Large frame steps establish
elapsed-time catch-up, not its cause. Capped traces and logging overhead must be
accounted for before drawing performance conclusions.

### Generated outlines and phase-entry timing

SDL3 generated outlines resolve pixel metadata once and sample a pitch-correct
alpha plane. Stencil work is restricted to the occupied silhouette's influence
bounds, without changing the full canvas, threshold semantics or sprite pixels.
The opt-in `ENABLE_NH_SDL3_OUTLINE_REGRESSION` target
`nhSdl3OutlineRuntimeTest` checks pixel equivalence against an independent generic
reference and prints bounded same-input sparse/dense CPU timings. It does not
initialize video or audio. Component timings are not gameplay frame-time or
Windows presentation measurements.

Creature groups and emitted projectiles defer at most one initial update that
could precede phase creation. A prior draw attempt releases the deferral; absent
rendering cannot repeatedly suppress advancement. Existing animation diagnostics
retain caller `dt_ms` and additionally report applied `advance_dt_ms`.
Movement position reuses the sprite's stored effective interval for the same
update. Entering a new pose clears stale elapsed time; continuing `MOVING`
across hexes preserves residual progress without another start deferral.
Draw diagnostics also include `pos_x` and `pos_y`; these are requested drawing
coordinates, not physical presentation measurements.
`nhAnimationPhaseStartTest` and `nhProjectileFlightLifecycleTest` cover the pure
start/flight policy, not complete battle callbacks or physical presentation.

Windows CI run `38193691694`, source `d042cb3f4fd2`, passes all 135 outline
pixel-equivalence cases. On the synthetic sparse 450x400 input, three
single-thread reference runs took 178.9–180.8 ms versus 1.21–1.24 ms for the
optimized path. The reference ran first and operating-system cache state was
not controlled. These are same-input CPU component measurements on the hosted
runner, not an in-game speedup, GPU timing or certification of later revisions.

### Resolved Cabir and Wisp playback regression

First-use custom frames triggered expensive generated-outline preparation over
large, mostly transparent canvases. The resulting draw stalls let wall-clock
animation updates catch up across multiple frame indices. Available image
payloads therefore did not guarantee that every pose received a draw opportunity;
this was not evidence of missing artwork or an NHART extraction problem.

The repair combines the bounded, pixel-equivalent outline kernel described above
with initial-pose and projectile-birth timing corrections. Walking position now
uses the same effective update interval as its sprite, preserving movement
progress across hexes. Sprite pixels, authored frame lists, resource identities
and the NHART package were not changed for this repair.

The implementation is in `clientsdl3/render/SDL_Extensions.cpp` and
`client/battle/{AnimationPhaseStart.h,CreatureAnimation.cpp,
BattleAnimationClasses.cpp,BattleProjectileController.cpp}`. The outline/phase
repair landed in `d042cb3f4fd2`; the walking synchronization in `5a85daca0ad5`.
Both are included in Windows build
[38201168973](https://github.com/thegandalf196/new-horizons/actions/runs/38201168973),
source `5229c808b15d`. Player verification confirmed Cabir and Wisp playback fixed
in that build. This is separate from the native CPU/pixel and Linux draw-trace
checks: it does not provide measured Windows frame times, certify every animation
state, or establish a fix for slow AI turns or scenario-restart UI cleanup.

### Regression guardrail: restarting during combat

Status: reproduced on Linux SDL3/OpenGL at source `875b2c5591d1`, following
the report from the `5229c808b15d` Windows player build. An ordinary human
combat restart completed teardown and loaded a new Day 1 adventure, but the
old battlefield and creatures remained composited over it. The probe exited
normally; this is a visible failure, not a failed launch or source-only finding.
Rendered verification of the scene-layer repair and Windows acceptance remain
required. A second scenario was not exercised in the failing probe.

The previous repair, `89bb7c693e5c`, established shutdown ordering:
`CServerHandler::restartGameplay` stops the old network/gameplay waiters before
destroying game state; `CClient::endGame` detaches the global battle interface,
stops its waiters, clears its GUI, and then releases the battle. Preserve that
ordering. The original `client/tests/check-battle-restart-teardown.py` checks
source ordering; it is not rendered restart acceptance.
The production-used scheduling regression is
`client/tests/WindowSceneRedrawTest.cpp`, registered as `nhWindowSceneRedrawTest`.
Run it with `ctest --test-dir build/new-horizons-linux -R '^nhWindowSceneRedrawTest$' --output-on-failure`,
alongside `python3 client/tests/check-battle-restart-teardown.py`.

Protected rendering invariant: clearing a scene must discard its deferred
redraw requests, reset the pending flag, and request a complete repaint. An old
window retained by another owner must not repaint over the next scene merely
because a request was queued before teardown. `WindowHandler::clear()` previously
omitted that invalidation. Repairing the queue alone did not eliminate the
rendered failure. The queue's introduction predates the earlier repair; do not
assign causality from commit order alone.

Protected GPU-layer invariant: definitive scene teardown must release every
scene layer independently of whether its former widget is currently active.
Opening combat settings deactivates the battle and requests a BATTLE-layer
release. A subsequent full redraw includes covered windows, so the inactive
`BattleFieldController::show()` can reclaim that layer. Clearing the scene later
deactivates only the top settings window; destroying the already-inactive
battlefield does not request another release. SDL3 owns the persistent layer
texture, while the battlefield's canvas merely borrows it. A new adventure's
transparent map area can therefore expose the old BATTLE layer above MAP.
This chain is established by source and the rendered symptom; per-frame layer
bits were not instrumented in the failing probe.

`WindowHandler::clear()` now marks every `GpuRenderLayer` released through the
existing thread-safe deferred API. The next GUI frame clears them before new
windows draw; a surviving or restored owner can then reclaim its layer normally.
SDL2's existing release API remains a no-op. This does not flush artwork caches,
destroy textures from the network thread, or change ordinary covered-window
redrawing. The teardown source check includes negative in-memory witnesses for
a missing release or incomplete layer range; it is not an executable GPU test.

Changes to battle ownership, restart/quickload/game shutdown, window clearing
or deferred painting must preserve these invariants and run the linked lifecycle
regressions. Rendered acceptance must exercise battle -> restart -> a new
scenario, plus return-to-menu -> another scenario; verify no old battle window,
queued repaint or delayed callback reappears. Record the tested build and
platform, distinguish normal exit from supervised cleanup, and keep this issue
open until the actual transition is verified. Do not substitute a cache reset
or source-order assertion for that acceptance.

### SDL3 PNG interlace regression

The maintained `tools/ci/patches/sdl3-image-3.4.4-interlace.patch` initializes
libpng interlace handling before both ordinary PNG and APNG read-update paths.
`python3 tools/ci/prepare_sdl3_image_interlace.py --source-root "$NH_SDL_IMAGE_SOURCE"` checks
the exact supported source and patch without modifying either. Its optional
`--output` writes one new patched source artifact into explicitly configured
private POSIX storage, not an installed library or a complete build tree.

Enable `ENABLE_NH_SDL3_MASK_REGRESSION` and run
`nhSdl3GrayscaleMaskRuntimeTest "$NH_MASK_FIXTURES" --interlace-regression`
to add synthetic RGBA/indexed Adam7 and noninterlaced pixel/alpha controls to
the existing mask/palette checks. Retain stderr separately: an exit-zero pixel
test alone does not prove the interlace warning disappeared. Source preparation
does not repair shipping providers; rebuilt library selection, warning-free
decoder execution and corresponding-source packaging require separate checks.
Notice-only repair retains the historical upstream dependency consumer. Its
reference check rejects patched-decoder artifacts with a different source graph;
use a fresh build for those artifacts. The notice-first preflight may compile
the patched decoder, but does not compile the game client.
The root Conan/MSVC workflow and local MinGW consumer select this patched
recipe; the MinGW helper exports it before dependency resolution. System-package
SDL providers are not changed by that selection and require a separate rebuild.

### Private Wine child-environment bootstrap

The guarded full-game Wine helper accepts optional `--wine-bootstrap` and
`--wine-bootstrap-sha256` pins. This test-only executable launches the selected
game with an explicit Unicode environment, replacing only `LOCALAPPDATA` after
Wine's initial registry-environment merge. The game's pre-SDL path, nonce and
audio checks remain mandatory. The bootstrap is not installed or shipped with
player packages; normal Windows startup is unchanged.

For an autonomous fixture, the full-game route accepts
`--wine-fixture-map NAME.vmap` exactly once. The basename must match the
hash-pinned content manifest, use the admitted ASCII grammar and fit the
expanded Win32 path bound. The owner generates only
`--testmap Maps/NAME.vmap --spectate --savefrequency 0`; arbitrary client
arguments remain refused. Bootstrap use requires a newly built, hash-pinned
`NH_WINE_BOOTSTRAP_FIXTURE_V1` executable. Actual child arguments are checked
around runtime proofs. Admission and compilation alone do not establish Wine
rendering or native Windows behavior; fresh containment checks and a separate
bounded run authorization remain mandatory.

Wine can mark its canonical `Z:\` host-root drive alias as a reparse point.
The private bootstrap and game witness recognize only that exact root; other
reparse paths remain rejected. The Linux owner creates and pins the fresh
prefix's literal `z:` → `/` mapping and its directory handles before launch,
then revalidates them around proof, acknowledgement, input and cleanup.
This is boundary validation, not an operating-system sandbox.

Bootstrap checks accept only the exact admitted command, with or without
Wine's executable token in its process arguments. Both bootstrap snapshots
retain exact process-lifetime, executable and hash checks; direct/surface
launches retain their original full-command requirement.

Wine 10's [loader initialization](https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/ntdll/unix/loader.c)
sets `WINELOADER` from its physical executable. After the admitted bootstrap
and game process/hash checks pass, that route accepts only the exact pinned
loader string. Foreign values and all other restricted host/injection routes
remain refused; direct/surface routes retain their strict environment contract.
This corrects test admission, not game playback or Windows performance.

Precreating `dosdevices` also skips Wine 10's normal private C-drive setup
([initialization source](https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/ntdll/unix/server.c)).
The owner therefore creates an exclusive private `drive_c` and canonical
`c:` → `../drive_c`, retaining and checking their directory/link identities
alongside `z:`. This does not reuse a host prefix or relax witness deadlines.

Build test prerequisites into an explicitly configured private output directory,
outside the repository and its build tree:

```sh
: "${NH_TEST_OUTPUT:?Set an existing private output directory}"
g++ -std=c++20 -O2 -DNH_WINE_BOOTSTRAP_ENV_TEST tools/tests/nh-wine-bootstrap.cpp -o "$NH_TEST_OUTPUT/bootstrap-env-test"
"$NH_TEST_OUTPUT/bootstrap-env-test"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -DNDEBUG -D_WIN32_WINNT=0x0601 -municode -static -Wl,--nxcompat,--dynamicbase tools/tests/nh-wine-bootstrap.cpp -o "$NH_TEST_OUTPUT/nh-wine-bootstrap.exe"
```

The first executable checks environment transformation without Wine or SDL.
Compiling the PE does not verify Windows process creation or game rendering.
Use the maintained guarded launch/ownership workflow for runtime tests, with a
fresh disposable prefix, owned background display and dummy audio; do not invoke
the PE directly on the host desktop. Parent-reaped descendants have a separate
kernel-backed cleanup classification, not a fabricated exit status. Direct
children still require an actual wait status.

### Creature-preview surface regression

Creature previews use a shadow-preserving mode without generating a selection
outline that their drawing path hides. Battle animations retain their selection
mode. The SDL3-only, opt-in CPU-surface regression compares the former transparent
selection palette with the preview palette, including conditional reserved
colors, shadows, palette shifts and effects. Synthetic indexed and RGBA surfaces
exercise native and 2x drawing, clipping and flips; every comparison requires
non-background output as well as identical pixels.

With an existing SDL3 build configured and its dependencies available:

```sh
cmake -S . -B build/new-horizons-linux -DENABLE_NH_PREVIEW_SURFACE_REGRESSION=ON
cmake --build build/new-horizons-linux --target nhCreaturePreviewSurfaceRuntimeTest --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy \
  SDL_VIDEODRIVER=dummy SDL_VIDEO_DRIVER=dummy \
  build/new-horizons-linux/bin/nhCreaturePreviewSurfaceRuntimeTest
```

The test does not initialize video or audio. It establishes lower-level CPU
surface/palette equivalence, not composed-layer generation, cache rebuilding,
actual DEF loading, GPU rendering or performance. Its resource-backed player
palette hook is not exercised by the standalone executable. Actual Fort and
battle presentation, SDL2 behavior and Windows performance require separate
checks. Enabling this test does not select the normal playable snapshot.

Focused validation passes all 58 CPU-surface comparisons and SDL2 syntax checks
for the changed backend and shared preview code. In one bounded native-resolution
Linux SDL3 Fort comparison, the same 34 Cabir and 47 Magi frame resources each
changed from two reads/decodes to one; neither trace reached its diagnostic cap.
The seven Academy portraits remained present and the game quit normally. This
establishes removal of duplicate loading for those observed frames, not an FPS
gain or a Windows fix. Individual crop timings did not establish a speedup;
preview poses, scheduling and operating-system cache state were not controlled.

### SDL3 crop preparation regression

The nonindexed crop scan has a direct alpha-byte path for packed 32-bit formats
with an exact eight-bit, byte-aligned alpha channel. It respects row pitch and
host byte order. Other formats keep the generic decoder; indexed images still
retain every nonzero index regardless of its current palette alpha. Crop copying,
virtual geometry and surface ownership are unchanged.

The same opt-in CMake option also builds `nhImageOptimizerRuntimeTest`:

```sh
cmake --build build/new-horizons-linux --target nhImageOptimizerRuntimeTest --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy \
  SDL_VIDEODRIVER=dummy SDL_VIDEO_DRIVER=dummy \
  build/new-horizons-linux/bin/nhImageOptimizerRuntimeTest --benchmark
```

As with the preview test, its configured SDL dependency libraries must be
available to the dynamic loader. No video/audio initialization or artwork inputs
are required. The test compares the actual optimizer with the former generic
scan/crop implementation, including padded rows, weak alpha, output bytes,
palettes, color keys, geometry and reference ownership. The optional finite
benchmark times both complete optimizers on identical synthetic inputs;
allocation and crop blitting are included, fixture creation is excluded.
These CPU-surface results do not establish actual Fort frame times, Windows
performance, GPU costs or cold operating-system file-cache behavior.

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
startup random draw `1284510375`, completed 43 AI turns in a 35-second observation window.
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

### Attributing exchange simulation cost

The dense diagnostic now covers both legacy and New Horizons command rules,
with separated opening and synthetic crowded post-wait formations. Each case
checks complete action identity and authoritative state preservation. The
post-wait activation is deliberately constructed, not a naturally played battle
sequence. This distinction matters: New Horizons command rules require dynamic
damage valuation, whereas the legacy fixture can reuse simpler damage snapshots.

Temporary, aggregated timers measured 880 unreachable-movement searches in a
bounded headless match: 13368 milliseconds total, with 12337 milliseconds inside
exchange evaluation. Reachability setup accounted for only 67 milliseconds.
This run requested seed 1284510375; the startup log printed 293067988 because
that message prints a random draw, not the configured seed. It is not a replay
of the preceding match or the user's unavailable autosave.

In the parameterized synthetic test, New Horizons opening searches took about
143–146 milliseconds and crowded searches 162–164 milliseconds. Legacy cases
took about 19 and 21–23 milliseconds respectively. Nested timers attributed
96–97 milliseconds of the opening case and 108–110 milliseconds of the crowded
case to target-score estimates (2288 and 2600 calls). Round transitions, activation
updates and child setup were each around one millisecond. These are instrumented
diagnostic observations, not whole-turn performance guarantees.

The target-selection comparator recomputed the incumbent score on each comparison.
A local single-pass selector now evaluates each candidate once per selection,
preserving candidate order, the first equal-scoring target, and the no-evaluation
singleton case. It does not reuse scores across attacks or activations, change
damage rules, reduce candidate sets, or bypass dynamic damage validation.

With the same temporary timers, the crowded New Horizons sample reduced target
estimates from 2600 to 1950 and search time from 162–164 to approximately 138–141
milliseconds. Full recorded action signatures matched the baseline for both
formations under both command modes. This small sample establishes removal of
redundant estimates, not a general percentage improvement for complete AI turns.

All temporary timers were removed before the final client/test build. The
broader native AI regression run passed 154 tests across 28 suites, and the
uninstrumented diagnostic action signatures still matched the measured baseline.
No graphical validation or replay of the user's slow match is claimed.

The committed `3cb608d7d` build was subsequently frozen and validated as snapshot
`b753cef75ef2921efe38d30af9c4ac6feef96328263d708abee452bc9a81d433`.
A 35-second headless `All for One` run with a fresh private profile and configured
seed 1284510375 completed 48 AI turns (maximum 4880 milliseconds; mean about
638 milliseconds). It reported none of the checked crash, command-rejection,
Leadership-limit or unsupported-rules errors. The planned timeout stopped the
in-progress turn, and no launcher/client child remained afterward. This is a
bounded smoke check, not whole-match acceptance or a controlled latency comparison.
That exact snapshot is promoted for the ordinary Linux launcher; the preceding
snapshot remains recorded as its rollback reference. Multi-second turn latency
still requires investigation.
