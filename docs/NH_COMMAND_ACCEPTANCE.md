# Orders acceptance contract

Canonical behavior is in the [private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md).
The eight Orders are Charge, Focus Fire, Riposte, Hold the Line, Brace, Protect,
Flank and Second Wind. Doctrine is not active New Horizons gameplay.

## Required checks

- Human and BattleAI use authoritative target/action validation and the same
  shared Hero Action budget as spellcasting. No fixed default Order substitutes
  for legal comparative AI decisions.
- Bookless or mana-empty heroes retain legal Orders. Spells and Orders have
  separate controls; neither grants an extra action.
- Rejected/cancelled commands preserve state and allowances atomically. Verify
  ownership, controller, phase, action-spent and legacy-rule contexts.
- Different Orders coexist under their own recipient/lifetime rules; reissuing
  an Order replaces its applicable prior state rather than duplicating benefits.
- Forecasts preserve live state and deterministic RNG. Validate copy-on-write
  branches, target status, expiry, initiative and downstream damage.
- Save/load retains representable state and historical contexts; unsupported
  formats reject required new state rather than dropping it.
- Show availability, disabled reasons, targeting, effect values/durations and
  meaningful causal logs. Cancellation, modal return and battle shutdown work.

## Source and evidence

Principal source surfaces are `lib/battle/`, `server/battles/`,
`AI/BattleAI/`, `client/battle/` and `client/battle/BattleHeroActionWindow.cpp`.
Focused fixtures include `test/server/battles/NewHorizonsCommandTest.cpp`,
`NewHorizonsProtectLinkTest.cpp` and
`test/battle/NewHorizonsOrderBadgeHelpTest.cpp`.
Choose additional Order-specific fixtures for the behavior actually changed.

Source/native checks do not establish rendered targeting, animation, readability
or ordinary control routing. Use the bounded isolated graphical route described
in [delivery](NH_DELIVERY_PIPELINE.md). Protect known
[regression invariants](NH_REGRESSION_GUARDS.md) and record unresolved interactions
in [integration gaps](NH_INTEGRATION_GAPS.md).
