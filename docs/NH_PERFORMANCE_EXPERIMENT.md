# Performance measurement and lifecycle guardrails

The transport architecture investigation is closed. Shipping execution remains queued, authoritative, in-process VCMI; no whole-game synchronous replacement or cooperative scheduler was accepted. The formation-only experiment did not establish a player-visible benefit. See [bounded findings](NH_PERF_RESULTS.md).

## Measurement contract

Pin source, client/facade, configuration, save/map, resource inventory, renderer, animation speed and workload. Preserve originals; use disposable profiles. Compare repeated matched/interleaved variants where practical, retaining failures/timeouts and action/state/RNG equivalence. Do not reduce forecast depth, legality or safeguards to obtain a faster result.

Separate adventure planning, combat callback work, server dispatch/completion waits, rendering and pacing. Wall-time sums are not CPU time; nested children must not be added twice to disjoint totals. ACK, CPU draw return and available images are not physical presentation. Clock domains and missing joins remain explicit. OS cache is not cold merely because a process is fresh.

Do not run concurrent builds/heavy tests during measurements. Graphical probes require the maintained silent guarded helper, isolated Xvfb, disposable saves and actual dummy-driver/process-identity checks before input. Owned absence is not parent reaping or exit-zero proof. Keep raw logs/captures/receipts private; do not change guard budgets to conceal an invalid run.

## Existing opt-in diagnostics

`NH_RENDER_LOAD_TIMINGS=1` enables existing diagnostics only under their logger gates. `NH_RENDER_LOAD_TIMING_LIMIT` accepts ASCII decimal 1–16384; malformed/missing values fall back to 1024. Counters are bounded, and a cap notice means later events are omitted. Disabled paths do not read clocks or allocate diagnostic logs.

- Resource/load records cover reads, decoding, crop/scaling, queue waits and texture creation; stages can overlap. Successful texture hits need not log.
- Battle animation records include `animationAdvance`, `animationDrawReturn`, `projectileAdvance` and `projectileDrawReturn`. Raw `dt_ms` differs from applied `advance_dt_ms`; `pos_x/pos_y` are requested coordinates, not scanout.
- WAIT completion uses `NH_WAIT_END_TIMING` under global/animation DEBUG: initial/order waits, activation, facing completion and UI remainder. It is not CPU or GPU time.
- Hero-cue records use the child `animation.heroProc` DEBUG domain with their independent bounded budget, avoiding global animation DEBUG traffic.
- BattleAI aggregates distinguish callback totals, movement search, ordinary/waited exchanges and nested probe/lifecycle work. Preserve legacy per-call microsecond truncation and additive fine-work nanoseconds; child sums are subsets, not additional body time.
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
