# New Horizons integration limits

Current phase: Phase 2 — stabilization and integration. The product version is not a completion percentage or a release-candidate declaration. The former 0.85 release/tag was withdrawn; do not describe it as a current download. Follow [release scope](NH_VERSION_1_0_SCOPE.md), [canonical design](design-sources/New%20Horizons.md) and [Pending Changes](NEW_HORIZONS_PENDING_CHANGES.md).

This is a bounded contributor-facing requirements list, not a worker queue. Raw attempts, receipts, profiles, saved inputs and task ownership remain in configured private storage.

| Area | Remaining requirement and evidence boundary |
| --- | --- |
| Slow AI turns | Attribute long Normal-difficulty turns to actual adventure planning, battle decisions and waits using the reported save or faithful reproduction. Preserve decision quality, rules and validation; matched action/state/RNG counts are necessary but not proof of speedup. Linux components or unrelated short smokes do not establish Windows gains. See [performance method](NH_PERFORMANCE_EXPERIMENT.md). |
| Hero Luck/Morale presentation | Observe a genuine successful horseshoe/eagle receipt through queue, draw and natural completion in normal legal play. Failed or capped capture and process disappearance are not completion/reaping proof. Do not force chance, seeds or stats to manufacture acceptance. |
| Hero redesign | All 144 ordinary starting profiles do not prove all workbook specialty riders/unique mechanics are implemented. Reconcile actual producers against the authored identity and accepted amendments; different icons cannot fix duplicated mechanics. Twelve campaign/special identities are a separate boundary, not fabricated ordinary profiles. |
| Perk weighting | Current `offerWeightPolicyVersion=1` and `NewHorizonsPerkState::prepareOffer` assign each eligible perk its full parent weight and exclude zero-weight parents. They do not implement the power-normalized parent-first R=2 proposal. R=2 means maximum preference ratio, not squaring. Preserve legitimate zero-weight learned parents, eligibility, distinct-parent and old captured-policy semantics when a change is adopted. |
| Saves and battle packets | Ordinary world saves exclude active battles. Stack/effect codecs and composed BattleStart/UnitChanges/RNG reconstruction are not an ordinary mid-battle save implementation. Verify permanent state, loaded action admission, captured rules and provenance through real production boundaries without weakening older-format guards. |
| Status/action interactions | Retain coverage for Spell Lock/Time Stop/allegiance, Frozen once-per-round receipt, paid control→cleansing, completed Defend/movement/creature casts, extra activations, casualty restoration/Disintegrate, recruitment cohorts and control ownership. Source or an earlier focused pass is not every later-build or whole-family certification. |
| Presentation/delivery | Current selected NHART has 4,016 payloads. Selection, provisional approval, source/container checks, rendered acceptance and playable promotion are separate. Broad spell/status contrast, specialty artwork, all school states/layouts and native Windows rendering remain build-specific checks. |
| Lifecycle regressions | Preserve combat restart/scene teardown and normal Quit/reaping contracts. Linux repaired restart evidence does not certify Windows, quickload or every delayed-callback path. See [regression guards](NH_REGRESSION_GUARDS.md). |
| Campaign/balance | Broad campaign continuation, compound interactions and complete autonomous games remain separate evidence. Experimental coefficients, Leadership and economy require playtest judgment, not theoretical completion credit. |

## Resolved source issues must not be reopened from stale diaries

The Plague/Spell Lock completion adapter is implemented: a hostile seal preserves Plague and pauses countdown while damage/spreading continue; a friendly seal cleanses it. Focused action/movement tests cover later detached completion paths. The old missing-adapter note is not a current defect.

SDL3 grayscale/interlace repairs, Cabir/Wisp initial-phase timing and scene-layer teardown have concrete source fixes and bounded evidence; preserve their regressions rather than infer a new defect from an old failure log. [Phase 1 exit evidence](NH_PHASE1_EXIT_AUDIT.md) states historical scope and limitations.

Forecast HERO_SPECIAL arrival, branch-local expiry and lazy snapshot isolation require matched focused acceptance before release claims. Follow `docs/developers/AI.md`, `StackWithBonuses` and `NewHorizonsDemonicGatingTest`; retain execution status and earlier failures privately.

The real Gating HERO_SPECIAL timer projection has an explicit source fix; current lazy-branch lifecycle acceptance is pending. Wiki exact-equal description deduplication is privately source-reviewed, not rendered acceptance. Selective autocombat controls are intentionally absent; do not invent a Defend/spell-defense gap solely from an old checklist.

Historical interaction leads requiring current-source reproduction, not freshly proven defects: Purify/AI temporary-control versus original-side eligibility; copied Crossfire history after elemental primary/physical reaction; Inspirational HERO_SPECIAL parent expiry; Deep Flank contact loss after splash/control; neutral-join route danger/displacement/economy; and saved combat-state/Dispel composition. Preserve Frozen automatic queue, Plague once-per-round and battle-identity guards rather than reopening the repaired thaw bug.
