# New Horizons development phases

Current phase: **Phase 2 - stabilization and integration**, as recorded in
[the Phase 1 exit audit](NH_PHASE1_EXIT_AUDIT.md). The canonical design is
[the private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md); the
[Version 1.0 scope](NH_VERSION_1_0_SCOPE.md) defines release boundaries.
Phase names describe engineering priorities, not active agent sessions or tool goals.

## Phase 1: implementation coverage

Implement the specified mechanics, data/registration, meaningful state and save
representation, required interaction paths, and minimum AI support. Prefer missing
foundational or release-scope features over repeated polishing of adequate ones.

For each feature, verify that its principal production path exists, required
content/UI is connected, it builds, focused checks pass, and no crash, state/save
corruption, or foundational regression is introduced. Record unverified
interactions for stabilization. Registration counts and test counts are not feature
completion. Derive coverage denominators from the actual specification.

Use focused validation per change and batch broader checks at useful checkpoints.
Immediately repair crashes, corruption, and architectural defects. Defer numerical
balancing and cosmetic polish unless they prevent meaningful testing.

Exit after a specification audit accounts for implemented, partial, missing, and
intentionally deferred scope, with explicit interaction and verification gaps.
Do not transition because test coverage alone appears extensive.

## Phase 2: stabilization and integration

Make implemented Version 1.0 systems behave correctly together. Prioritize:

1. Crashes, memory/state corruption, and save/load integrity.
2. Incorrect mechanics, action/activation budgets, initiative, and extra activations.
3. Targeting legality, effect lifecycles, ordering, and cross-system interactions.
4. AI use of the implemented mechanics and UI predictions matching actual results.
5. Remaining relevant regression coverage and platform acceptance.

Exercise applicable interactions: spells with resistance/reduction/dispel/statuses
and perks; Orders with Command/perks/Warcasting; creature abilities with spells and
actions; Wait/Morale/extra activations; death/restoration/casualty provenance;
Leadership with transfer/recruitment/upgrades/Diplomacy; Siege/war machines/repair;
save/load with permanent, combat, temporary, and town state; UI and AI with each.
Select cases from real risks and production consumers rather than generating every
possible Cartesian combination for its own sake.

Run meaningful integration and regression suites at appropriate checkpoints,
including reproducible rendered/gameplay checks where needed. Source inspection,
compilation, synthetic cases, and rendered acceptance are distinct evidence.
Avoid substantial new mechanics while integration remains unstable unless the
user explicitly changes priorities.

Exit when intended mechanics interact reliably, saves are compatible and reliable,
major interaction risks have useful coverage, AI exercises the new systems, UI
communicates outcomes accurately, and remaining issues primarily concern balance,
usability, polish, or isolated edge cases. State evidence and remaining gaps.

## Phase 3: playtesting, balance, and release hardening

Evaluate whether the game plays well through representative completed games,
controlled scenarios, relevant simulations, and human playtests. Tune numbers only
against a stated gameplay problem. Observe, reproduce, identify the responsible
rule, make a bounded change, run regressions, replay, compare, and retain or revert.

Consider pacing, economy, costs/growth/recruitment, Leadership, spells/Orders,
skills/perks, creatures, Luck/Morale, siege, artifacts, and AI valuation in context.
Polish controls, tooltips, targeting, animations, audio, performance, loading, and
accessibility after mechanics and balance stabilize.

A Version 1.0 release candidate needs intended scope, resolved major integration
defects, healthy regressions, normally completable representative games, working AI,
acceptable balance, truthful UI, reliable saves, and no known release blocker.
The phase designation and development version do not prove those gates passed.

## Reporting

Report changes to product scope/behavior, verification, remaining defects, and the
next highest-priority outcome. Keep live work queues and raw execution histories
private under [the documentation policy](NH_DOCUMENTATION_POLICY.md).
