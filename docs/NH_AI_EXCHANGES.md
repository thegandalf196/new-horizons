# Leadership-aware AI army exchanges

## Physical-army contract

Leadership limits each physical hero stack, not aggregate creature-type counts.
Plans must preserve partial transfers, duplicate stacks, available slots, both
heroes' swap capacities and the donor's last creature. Excess troops are never
dismissed. The server validates every arrangement request.

`armyFormation::projectArmyExchange` returns legal operations and projected
physical armies. `ArmyManager::getBestArmy` values that projection and returns
the exact plan used by the gateway. Execution checks expected pre-state and
authoritative post-state, stopping on divergence. Legacy selection remains separate.
This prevents valuing three reinforcements when capacity admits zero or one.

Never reconstruct a second plan from the first plan's result: duplicate-stack
reservation can change and lose the intended swap. Preserve the donating hero
identity separately from synthetic armies so source-side capacity remains available.
Release rejected non-improving projected armies.

## Purchase, upgrade and route identity

Town-path projections preserve physical slots even when no upgrade is available.
Carrier identity follows synthetic/path/Gather Army/hill-fort estimates.
Filter upgrades against carrier capacity and cap purchases before spending the
projected resource budget. Current recruitment uses the first matching slot;
forecast correctness does not claim later duplicate slots are recruitable.

Path reconstruction accepts successive states of one hero only at a verified
exchange boundary: same predecessor hero, disjoint donor mask, exact union equal
to result. Compressed predecessors need not occupy the exchange tile. Starting
position transitions advance history without creating movement commitments.

Retain distinct predecessor-spine and mask commitments. Ordinary movement does not
duplicate them; incompatible separate branches still reject subset conflicts.
Keeping only a latest mask would lose that protection. This is local query
bookkeeping, not a persistent world-wide cache.

## Source and regression scope

Inspect `AI/Nullkiller2/Analyzers/ArmyManager.cpp`, its gateway and pathfinding consumers,
and the shared army-formation helpers before changing valuation/execution.
Focused formation/Gather Army fixtures cover capped/partial reinforcement,
duplicates, empty receivers, donor retention, swaps, legacy transfers,
server-backed exact-plan conservation and idempotent repeat.
Purchase/upgrade/route fixtures cover real/synthetic carrier identity,
resource ordering and successive versus conflicting exchanges.

`RepeatedExchangeValuationIsStableAndReadOnly` compares repeated legal plans and
unchanged serialized authoritative state. It intentionally has no machine-dependent
timing threshold. Historical native passes are regression evidence, not a replay
of a slow full match, full-turn latency or rendered adventure acceptance.
See [AI development](developers/AI.md), [investigations](NH_INVESTIGATIONS.md)
and [integration gaps](NH_INTEGRATION_GAPS.md).
