# Leadership-aware AI army exchanges

## Contract

Leadership limits each physical hero army stack, not the aggregate count of a
creature type. Exchange planning must account for partial transfers, multiple
same-type stacks, available slots, both heroes' capacities when swapping, and a
donating hero's required last creature. Excess troops must not be dismissed.
The server remains authoritative for every arrangement request.

## Implementation checkpoint

The shared read-only `armyFormation::projectArmyExchange` returns legal transfer
operations and the projected physical armies. `ArmyManager::getBestArmy` uses
the projection for Leadership-aware reinforcement valuation and can return that
exact plan to the gateway. The gateway submits those operations, checking
each operation's expected pre-state and authoritative post-state. It stops if
those states diverge. The legacy selection/execution path remains separate.

This repairs the evidenced mismatch where a capped receiver was still valued
as gaining three Pikemen, although the transfer executor could accept none.
With room for only one Pikeman, the old estimate likewise counted three.

## Validation — 2026-09-23

- Both isolated valuation regressions failed before the production change.
- The shared-library and native-test targets compile.
- All 16 focused tests in `NewHorizonsArmyFormationLeadershipTest` and
  `Nullkiller2_Behaviors_GatherArmyBehavior` pass. They cover capped and partial
  reinforcement, duplicate stacks, empty receivers, last-stack retention,
  source-side swap limits, legacy transfers, gather-task suppression, and an
  actual server-backed transfer followed by an idempotent repeat.
- Fourteen related priority, task-failure, recruitment, and army-loss tests pass.
- The older garrison-upgrade fixture now explicitly selects legacy capabilities
  for its 1,000-Pikeman army; its upgrade assertion is unchanged.

This checkpoint is not a claim of complete adventure-AI acceptance; a fresh
headless game run remains pending.
Path-chain estimates now pass the donating hero separately from a synthetic
army, retaining source-side Leadership checks even when the army itself has no
owner identity. Rejected non-improving exchange candidates also release their
allocated projected army.

The synthetic-donor fixture now uses a level-two receiver and explicitly asserts
an Archangel capacity of one, ensuring it exercises the intended swap rather
than consolidation. The subsequent combined Spell Point and AI regression run
passed all 73 tests, including that fixture, physical-stack preservation, and
server-backed exchange checks. This is native regression evidence, not complete
adventure-AI gameplay acceptance.

## Review correction

Independent source review found that projecting twice could change which
duplicate stack was reserved. A first plan could swap away one of two matching
receiver stacks, while retaining the other because it could not be returned.
Treating that result as a new preference from the original armies then reserved
the wrong duplicate and prevented the swap. The gateway now obtains the exact
transfer plan from the valuation call instead of reconstructing it from the
resulting army. This retains the expected-state checks and server authority.

## Validation — 2026-09-24

All 19 focused formation and Gather Army tests pass after rebuilding the client,
shared library and native tests. The new duplicate-Angel/Phoenix fixture proves
that reprojecting the resulting physical slots can lose the intended swap.
The server-backed partial-transfer regression now captures the valuation plan
and compares every resulting receiver and donor slot against that exact plan,
in addition to checking conservation, capacity and repeat-call idempotence.
These checks do not establish full-match turn-time performance or graphical
acceptance. Duplicate-stack handling during separate upgrade/purchase planning
still requires review.

### Exchange timing diagnostic

`RepeatedExchangeValuationIsStableAndReadOnly` measures three full valuation
calls with seven repeated Pikeman stacks on each hero, only one unit of receiver
capacity available, and captured New Horizons rules. Samples on the initial
diagnostic build were 390, 376 and 375 microseconds. Each call returned the same
one-unit transfer and physical army plan; serialized authoritative state remained
unchanged. There is no machine-dependent pass/fail timing threshold. This small
fixture does not establish whole-turn performance, diverse-creature swap cost,
or a need for a new persistent capacity cache.
