# Spell Point migration decisions

This is bounded rationale for the accepted Normal/Buffer model, not a second
specification. Current gameplay follows
the [private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md).
The [Overrides migration](NH_OVERRIDE_MIGRATION.md) records the legacy transition.

## Accepted model

Normal Spell Points have a maximum based on effective Knowledge. Intelligence
uses floor(130% of effective Knowledge), including equipment Knowledge.
Buffer is a separate nonnegative pool with no implicit Normal refill.
Total available is Normal plus Buffer; spending consumes Buffer first.
Capacity increases do not refill Normal, and capacity corrections never discard
or create Buffer.

Capacity updates are event-driven at complete equipment transactions, bonus
removal/change and loaded bonus-tree reconciliation. Do not poll on movement,
rendering or timers. Equal-capacity swaps must not transiently truncate points.
Load migration from an old scalar does not fabricate Buffer.

Arcane Reservoir grants +50 Buffer once weekly; Magic Spring restores Normal and
grants +25 Buffer under its visit rules. Ordinary restoration targets Normal only
unless explicitly specified. Permanent Buffer is distinct from combat-only grants;
unused combat-only Buffer expires at combat end. Cancellation/retry restores the
appropriate pre-battle provenance without erasing completed battle gains.

A legitimately inscribed eligible combat spell can be cast without its School
rank. School rank gates ordinary acquisition, not such casting. Costs, action,
phase, target, immunity and Spell Lock restrictions still apply. Spellbinder's Hat
temporarily grants eligible Level 5 combat inscriptions; removal preserves
independently learned spells. Neither rule grants unavailable or Adventure spells.

## Presentation and regression boundary

Show Buffer-inclusive available points against Normal capacity, with the separate
Buffer annotation in yellow. Do not double-count Buffer or restore the superseded
blue presentation note. Enemy-visible displays must not leak hidden pools.

Protect Buffer-first spending, atomic rejection/exhaustion, Normal-only restoration,
equipment transaction stability, Knowledge/Intelligence changes, battle cancellation,
temporary/permanent provenance, both-pool save/load and temporary Hat access.
Relevant fixtures include `test/hero/SpellPointStateTest.cpp`,
`SpellPointCapacityTest.cpp`, `SpellPointRewardTest.cpp` and
`SpellPointPresentationTest.cpp`. Historical native/data checks established the
migration's principal paths, not every interaction or rendered annotation.
See [regression guards](NH_REGRESSION_GUARDS.md) and
[content development](developers/NewHorizonsContent.md).
