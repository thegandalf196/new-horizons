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

### Target-probe damage and scoring measurements

The existing battle-summary row separates the primary damage calculation and
DPS scoring inside target probes. Movement uses
`exchange_target_probe_main_damage_*` and `exchange_target_probe_dps_score_*`;
normal and waited forecasts use the corresponding `best_target_normal_*` and
`best_target_waited_*` fields. Each pair reports calls and wall microseconds.

These are disjoint nested subsets of target-probe time, not additional regions
to add to the enclosing exchange or callback total. DPS scoring includes its
reverse-damage and health-scoring callees, not just arithmetic. Early-return
probes may have zero child calls. Applied attacks do not populate these counters;
null timing sinks take no clock samples. Search depth, scoring and rules are
unchanged.

Focused checks cover timed/null score and state parity, applied-attack exclusion,
early/reset zeros, nested bounds and movement/normal/waited aggregation. Existing
one-stack, seven-stack and seven-versus-seven Normal controls exercise the new
properties. These checks validate attribution, not a performance improvement;
reproducible workload-matched measurements are still needed before optimizing.

## Permanent hero state across world saves

`PermanentHeroProcStateSurvivesWorldSaveReloadAndValidatedEndTurn` exercises the
production world-file writer and loader, then submits an ordinary validated
EndTurn request. It checks captured Luck/Morale rules, selected perks and ranks,
positive Normal mana, nonzero permanent Buffer and a nonempty spellbook on the
reloaded heroes. The existing presentation-export fixture remains unchanged.

Public service draws populate both heroes' dedicated Luck/Morale RNG streams.
The test checks whole RNG-state equality across the disk save/load, then compares
the next full receipts and default-generator draw with an independent clone.
These RNG comparisons precede the subsequent EndTurn; they are not paid-proc
presentation evidence.

The opt-in case requires `NH_EXPORT_HERO_PROC_SAVE_ROOT` to match the isolated
test profile's actual save directory; it refuses symlinked or existing output.
Its default TEST content preset and admitted mock connection establish a focused
save/continuation regression, not shipping-only graphical acceptance, successful
proc/refund presentation, negative connection authentication or active-battle saves.

## Hero Luck and spell reflection

`PaidMagicMirrorInheritsOneHeroLuckReceiptAndRefundsOnlyOriginalPayer` checks a
paid Magic Arrow reflected by Magic Mirror: one original Luck receipt, reflected
critical damage, one Hero Action charge and a refund only to the original payer,
including Normal/permanent/temporary Spell Point provenance. The fixture reaches
the caster's activation through accepted actions before taking its snapshots;
it does not overwrite initiative or relax cast validation.

This is focused native coverage, not graphical cue, exhaustive reflection or
statistical probability certification.

## Hero positive-proc presentation diagnostics

With `NH_RENDER_LOAD_TIMINGS=1` and the `animation.heroProc` logger at DEBUG,
`NH_HERO_PROC` correlates accepted Hero Luck/Morale flags with their queued stage,
stage execution, effect registration, first CPU draw return, midpoint and natural
finish. Scene, local action ordinal, source, side and effect identify the chain;
the existing animation ID joins registration to playback. One horseshoe still
represents simultaneous successful Luck perks; the eagle remains a separate stage.

Hero records have an independent bounded counter using the existing strict
`NH_RENDER_LOAD_TIMING_LIMIT` parser (default 1024, maximum 16384, one overflow
notice). Set global, animation and AI logging to INFO and only this child domain
to DEBUG to observe cues without enabling generic per-frame timing records.
Existing generic timing gates and budgets are unchanged.
`receiptNoProc` records accepted zero-proc flags; no cue chain is expected for
that action. This distinguishes an observed non-trigger from missing trace data
without logging random draws or changing probabilities.
Zero flags do not distinguish an ineligible action, an omitted roll or a missed
roll; they establish only that no successful proc was represented.

Receipt-presence fields now distinguish absent metadata from a retained draw.
Hero value, base threshold and dice size come from that authoritative receipt;
absent values are marked unknown. The four `PolicyScale` fields describe the
captured battle policy, not the random result. The Morale grant ID identifies the
consumed triggering grant receipt, not the new Heroic Spirit grant or the consumed
allowance kind. No random draw or bias is logged.
These scalar summaries are copied before queued playback; they do not retain
packet or Order pointers. Component tests cover copy, reset and separate optional
presence, not actual logger output, packet routing or visible effects.

`teardown` marks scene termination, not natural effect completion. Missing images
are reported without changing existing recovery behavior. Disabled or capped
records leave the chain incomplete; absence is not proof of a failed proc or draw.
The first-draw record follows `Canvas::draw` returning, not GPU presentation or
visible pixels. Pure flag/once-only-marker tests do not establish rendered
acceptance. Observe actual successful procs through the guarded graphical path;
do not force gameplay probabilities or infer successful rendering from receipts.
