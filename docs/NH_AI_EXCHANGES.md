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

### Upgrade, purchase and route reconstruction follow-up

Town-path forecasts previously merged duplicate physical slots when no upgrade
was available. Their purchase and upgrade estimates could also lose the actual
hero identity when operating on a synthetic army. The follow-up preserves slot
identity and threads the carrier through path, Gather Army and hill-fort reward/
cost estimates. Upgrade candidates are filtered against that carrier's capacity;
purchase counts are capped before allocating the resource budget.

This deliberately mirrors current recruitment's first matching slot. Spare room
in a later duplicate slot is not yet usable by the actual recruitment command;
fixing the forecast is not a claim that this broader recruitment limitation is
resolved. Native purchase-capacity/resource-ordering and real/synthetic upgrade
carrier tests pass. The public town-path regression exposed a separate
reconstruction defect: an accepted purchase actor is discarded when the carrier's
arrival and post-purchase nodes have different chain masks. The projected army
and exchange actor are created successfully; path reconstruction rejects their
chronological dependency.

The correction distinguishes one hero's successive states along its carrier
predecessor path from incompatible commitments in separate donor branches. A
mask change is permitted only at an explicit exchange boundary: the predecessor
must carry the same hero, its mask must be disjoint from the donor's, and their
union must exactly equal the exchange result. Compressed predecessors need not
occupy the exchange tile. Verified transitions also advance local history through
starting-position nodes, without making those nodes real-movement commitments.

Distinct movement commitments are retained by predecessor-spine identity and
mask. Ordinary movement does not add duplicate commitments. Separate branches
still reject incompatible masks, including subset relationships; keeping only
the latest mask would incorrectly lose this protection. Reconstruction remains
local to a path query, with no persistent state or world-wide scan.

Regression acceptance must retain the purchase/arrival action dependency,
physical stacks and unchanged authoritative state while continuing to reject
conflicting branches. Direct reconstruction cases cover successive exchanges,
compressed predecessors, starting-position transitions and bounded bookkeeping.

The client and native test executable rebuild successfully. All 13 focused
purchase/reconstruction tests pass, including the previously failing public
town-path case. A broader run passes 301 tests across 42 suites covering
adventure AI, battle magic AI, Leadership formation/exchanges, Necromancy,
Spell Point pools and capacity, primary progression and historical rule
snapshots. These are synthetic/headless checks, not a replay of the reported
slow match or graphical acceptance, and they do not establish full-turn latency.
