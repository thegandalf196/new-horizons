# Battle movement queries

Battle movement separates current rule discovery from path computation. The
callback resolves visible battlefield state before selecting a solver. BattleAI
can compile walking inputs into an owned `MovementQuery` and reuse distances and
predecessors across hypothetical branches when every input compares equal.
Ordinary callbacks retain the native direct solver.

## Query contract

The query contains the start anchor, footprint, flight mode, traversable anchors,
anchors that stop further walking, per-tile movement penalties, and the resolved
cost of traversing destructible enemies. It contains no unit IDs, callbacks,
borrowed footprints, bonus lists, or game-state pointers.

Compilation retains the current callback's visibility and rules. Walking accounts
for Ghost Walk, Passing Lines, pass-through, both creature footprints, stopping
obstacles, gate exceptions, active obstacle movement penalties, and enemy-bypass
costs. Flying keeps its separate direct-distance semantics. The existing
accessibility calculation retains Demonic Gate reservations and other endpoint
restrictions.

An effect can change a query without requiring a manually maintained list of
cache invalidation events. Identical compiled inputs have identical mathematical
outputs. Hashes select candidates for lookup; full equality decides reuse.

This contract does not authorize sharing attack scores. Enemy-bypass weights
already depend on health and damage; those resolved weights are part of the
query. Combat exchanges have additional dependencies that are not represented by
this movement key. Reachable-unit membership also depends on turn order, speed,
shooting and control, and contains branch-specific unit pointers.

## Ownership and ordering

The memo belongs to a BattleAI evaluation and is shared with its hypothetical
descendants. Its storage is bounded and contains only values. A returned
`ReachabilityInfo` always receives the current query's parameters and
accessibility metadata. In particular, the memo never retains
`Parameters::knownAccessible` or substitutes another branch's reservation state.

The solver preserves ordered neighbours, FIFO relaxation, strict improvement and
predecessor selection. Weighted movement continues to use the same relaxation
algorithm. Equal shortest distances alone are insufficient verification: changing
an equal-cost route can change a later movement decision.

The existing per-evaluator `ReachabilityMapCache` remains separate. It already
avoids repeated solves for unit IDs held by that evaluator. Compiled-query reuse
addresses equivalent calculations across different evaluators and branches.

## Admission and exact fallback

Flying fills the callback's existing result arrays directly, avoiding a second
initialized path result, unused walking arguments and an array conversion/copy.
The dedicated result-returning wrapper uses the same small inline calculation.
Walking memoization bypasses storage when
disabled or when the raw map has at most 24 open/gate/occupied cells. Finding 25
such cells ends that admission scan early. If a cheap field is encountered before
any entries were stored, the rest of that decision uses the direct solver too.

Before the first hit, a successful exact miss for a previously seen starting
anchor and footprint closes admission. Different inputs at the first repeated
footprint show that reuse has not materialized; storing further one-off queries
would add work. The 32-miss ceiling remains for queries with all different starts.
An exact hit before either cutoff keeps admission enabled. Failed solves do not
record a starting footprint or close admission.

These are cost heuristics, not search limits: every requested query still receives
complete exact paths, including after a small field expands or the inputs change.
They can miss reuse that appears later in a decision. Storage remains bounded
independently of admission (256 entries by default), and unused FIFO bookkeeping
does not allocate memory.

Hashing reads explicit field representations, never structure padding, and full
key equality resolves collisions. FIFO bookkeeping references stable map keys
instead of retaining a second copy. Cache statistics distinguish bypassed direct
solves from successful reuse.

## Verification and measurement

Compare full distance and predecessor arrays against the prior algorithm. Cover
weighted and unweighted movement, flight, both widths and sides, stopping tiles,
invalid starts, changed inputs, cache capacity, failed calculations, and separate
branches. Native integration additionally checks endpoint legality, visibility,
rule capture, selected actions, state and RNG preservation.

Measure repeated-query and no-reuse workloads separately. Report compilation,
lookup and solver costs distinctly from inclusive movement selection. Movement
selection also simulates attacks and exchanges; a path-solver improvement is not
by itself a whole-battle speedup. Existing component and historical gameplay
evidence remains scoped under [the performance method](../NH_PERFORMANCE_EXPERIMENT.md).

The asset-free Linux harness can be built with
`cmake -S test/movement -B build/movement -DCMAKE_BUILD_TYPE=Release`, followed by
`cmake --build build/movement -j 4` and
`ctest --test-dir build/movement --output-on-failure`. It requires C++20, Boost
headers, GoogleTest and threads. `nhMovementQueryBenchmark` compares the frozen
native reference, direct production solver, compiled solver and admitted memo.
The reference is reported both inline (`legacy`, retained for comparison with
earlier measurements) and through a separate translation unit (`legacy_external`)
to expose call-boundary and code-layout effects. Both remain independent of the
production solver. Flying uses the dedicated inline production wrapper, matching
the original inline reference's compilation boundary. The benchmark still excludes
callback output/metadata initialization and therefore does not time the saved copy.
It does not run the full BattleAI projection fixtures or require game assets.
