# Adventure Movement

New Horizons daily land and sea capacity is evaluated by
`lib/pathfinder/NewHorizonsMovementBonuses.cpp`. `TurnInfo` supplies cached bonus
lists to this evaluator for movement and pathfinding; the hero-window breakdown
uses the same evaluator. Step costs remain in `NewHorizonsMovement.cpp`.

## Captured level growth

The optional `dailyMovement` object in the hero-development rules has version
`1` and an integer `pointsPerLevel` from 0 through 1000. Current configuration
uses 1. Resolution captures the object in each hero's development profile. An
older captured profile without this object receives no level growth; runtime
queries never substitute installed defaults. The existing serialized JSON
profile carries this policy without a separate save field.

The flat addition is `max(0, level - 1) * pointsPerLevel`. At the current value,
an otherwise unmodified hero has 200, 209, 219 and 229 Movement at levels 1, 10,
20 and 30. This changes daily capacity, not remaining points during a level-up.

The shared BonusList-compatible stages are:

1. Add base-number bonuses to the 200-point base.
2. Apply percentages to base, including Logistics and applicable sea bonuses.
3. Add flat bonuses and captured level growth.
4. Apply existing percentages to all and independent bounds.

Thus level 20 with a 30% base bonus has 279 Movement. A subsequent 10%
percentage-to-all yields 306 under the existing integer rounding convention.
Existing source/target bonus scaling and independent-only bound behavior remain
part of the evaluator. Airship capacity retains its separate limit.

`CGHeroInstance::levelUp` invalidates the hero bonus-tree version. Pathfinding
caches must observe that version; merely refreshing a capacity query must never
restore spent Movement. Land/sea capacity, terrain and road step costs, and
embarkation payment are separate calculations.
