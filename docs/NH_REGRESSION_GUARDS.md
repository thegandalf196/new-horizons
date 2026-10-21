# New Horizons regression safeguards

Keep one maintained invariant and reproducer per recurring defect; retain raw failing attempts privately. Do not erase a failure by narrowing a filter, changing admission or treating a green successor as proof of the original platform.

## Scene and ownership lifecycle

See the [combat restart guardrail](NH_PERFORMANCE_EXPERIMENT.md#combat-restart-guardrail) for exact ownership/repaint ordering and Linux acceptance limits. Run `nhWindowSceneRedrawTest` and `client/tests/check-battle-restart-teardown.py`, then an ordinary human combat→Settings→Restart→new adventure and menu→new scenario witness. Source checks do not certify pixels.

Pre-teleport transition snapshots must render on the GUI thread before the
received teleport changes game state. The receiver releases the interface mutex
while waiting, then checks cancellation again after reacquiring it. Scene
teardown cancels pending captures before network join; late callbacks must not
retain or paint a retired scene. Failed dispatch or capture requests orderly
application shutdown, not silent termination of only the receive thread.
`nhMapTransitionSnapshotTest` and `client/tests/check-teleport-snapshot-thread.py`
cover the rendezvous, queue publication/rollback and shutdown wiring. Retain
visible surface/underground traversal and restart witnesses separately; pure
checks do not establish GPU or platform acceptance.

Before delivering a battle result, retain the admitted primary and additional
receivers for both sides and any admitted spectator. A human interface may
unregister its auto-resolve AI during its own callback; that must not destroy the
AI or omit its terminal callback. Deliver each admitted identity once in stable
order on the calling thread, before `battleFinished`; newly registered receivers
must not receive the old result. `nhBattleEndDispatchTest` covers these ownership
and ordering rules. A bounded Linux SDL3 ordinary-game witness also covered
instant-finish confirmation, the result and captured-garrison dialogs, return to
the adventure map and normal Quit; both sides emitted one terminal AI summary.
That authored fixture had loader diagnostics and did not reproduce the reported
slow fight. Windows, restart and broader interaction acceptance remain separate.

Generated user Maps must remain refreshable after initialization and resolve through the core resource origin before map/header loading, without losing user-map override precedence. Retain `GeneratedMapCreatedAfterInitializationIsRefreshable` and `LateGeneratedUserMapHasCoreOriginAndLoadsHeader`; these filesystem fixtures do not establish rendered Battle Only Begin acceptance.

## Gameplay and forecast integrity

- A range-for over a dereferenced temporary shared BonusList owner can dangle. Retain the owning list through iteration. Keep distinct equal bonus identities, ordered/raw aliases, child ADD/removal/UPDATE and branch aging isolation controls.
- Use authoritative action/activation/controller APIs and legally earned perk/Metamagic allowances in fixtures. Do not forge state to bypass admission. Damage must be meaningful and survivable where later assertions require a living stack.
- Protect redirects the primary victim, not the original melee contact geometry.
  Derive collateral from the Ward's contacted hex. A nonadjacent Protector can
  retaliate against the attacker without adjacent counter collateral; do not ask
  adjacency-only helpers to invent a direction for that remote counter. Keep
  `ProtectAttackPreviewTest` coverage for native/forecast damage, retaliation,
  Order consumption, continuation search and live/parent/sibling isolation.
- Exercise unselected, cancelled and valid Gating reserve choices; an unset creature
  identity must not be dereferenced. Check localization separately. Cover field
  and siege battles with heroes and war machines when validating fortification
  target indexes. Preserve the exact reproducer when assigning a cause or proving
  a repair; do not attribute a failure to a unit without evidence.
- Finalize Plague at actual completion, not WAIT/free cast; hostile Spell Lock pauses countdown, Time Stop uses its distinct clock and Frozen retains its same-round receipt.
- Applied attacks retain pre-hit/after-attack snapshots needed for native Rebirth, Elemental Memory, Hex and Cleave. Skip only data mathematically unused by probes; compare exact scores/actions/state/RNG.
- Deep Flank uses pre-hit contact geometry for the primary shot, but friendly Magog splash casualties must disappear from the next live and freshly detached contact/damage query. `AcceptedMagogSplashDeathRemovesCurrentContactPremium` requires a real accepted shot and both recorded collateral deaths; do not replace these with injected casualty packets or retroactively strip the first shot's premium.
- Persist source-owned control, caster provenance and casualty cohorts. A packet reconstruction is not ordinary world mid-battle save support.
- Read saved policies from the captured context. Missing historical blocks retain documented compatibility; malformed present data is not absence. Reject contradictory current retired Gambler flags before reader installation, visitor mutation and server broadcast.

## Build and test evidence

Include concrete definitions needed by serializer instantiation; avoid broad headers or API casts to conceal a fixture error. C++20 designated initializers follow declaration order; mixed numeric `min/max` calls need a common type on every platform. Static native tests must not falsely claim the copied facade was mapped.

Case-collision/early-refusal fixtures must model impossible-on-this-host inputs explicitly and assert later parsing is not reached. Keep native target-platform tests separate from mocked or host-only checks.

## Packages, privacy and dependencies

Use [Windows acceptance](NH_WINDOWS_ACCEPTANCE.md) and [NHART delivery](NHART_DELIVERY.md). Verify the complete actual archive and resolved dependency graph, including static/header-only source and notices. PE import closure is not dynamic-load proof. Missing source patches or notices must be recovered from the exact producer, not concealed by upgrades or blanket allowlists.

Scan binary ASCII/UTF-16 strings as well as text; successful compilation and stripping debug sections do not establish privacy. Preserve compatible CRT/provider provenance. Supported shell and path/case semantics need real target-platform checks.

## Rendering diagnostics

Pixel equivalence, CPU draw return, image availability, GPU present and visible acceptance are different observations. Keep strict masks and alpha validation; repair decoder semantics rather than assets/log suppression. First-phase deferral is finite and shares sprite effective elapsed time with movement; per-hex residual and no-render advancement must remain intact.

Use existing bounded diagnostic gates and counters. A cap or invalid capture makes scope incomplete, never an inferred engine failure or successful presentation.
