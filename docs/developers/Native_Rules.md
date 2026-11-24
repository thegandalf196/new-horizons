# Fixed native rules

New Horizons executes its shipped damage, spell, combat-event and imported map
rules in C++. `GameLibrary` installs `NativeModule`; `NativeScriptFactory` retains
the authored identifiers, schemas, priorities and supported patch-chain identity.
JSON remains the authored data format. Unknown replacement scripts and changed
patch chains are rejected rather than ignored.

The factory covers the shipped spell effects, combat-event identities and damage
calculator. Native damage first captures owned current facts, then runs the pure
numeric calculation. It does not cache damage by unit IDs. Movement queries have
a separate [owned-input and exact-reuse contract](Movement_Queries.md).

## Captured rules and forecast inputs

`nativeRules::CapturedRulePool` owns immutable captures for a game. Heroes retain
independent component identities for mastery, perks, capabilities and development;
equal components share storage. Admission validates and prepares complete
replacements before publishing them. Use the replacement APIs rather than casting
away constness from a captured document. See [serialization](Serialization.md)
for world tables and standalone snapshot envelopes.

Perk catalogues compile dense, capture-scoped handles. Acquired-perk projections
change transactionally with selections; active queries also read the hero's live
Skill rank. A handle from another catalogue cannot select a perk accidentally.
Typed development, mastery and Leadership projections reuse admitted tables for
growth, Skill offers, creature Requirements and specialty policy. Metamagic's
policy belongs to its captured perk catalogue; spell-family and energy tables
belong to the captured world magic profile. Free or ineligible sources may have
a known family without providing energy. Cold JSON remains available to import,
validation, serialization and presentation adapters.

A forwarded owner is usable only when its kind and complete document identity
match the document exposed by that callback. A custom callback exposing a different
profile must admit that profile at the boundary; it cannot silently inherit the
forwarded owner's rules.

Mechanics-presence checks prove only that a bonus type cannot occur. They include
inactive source records, and return unknown for unsupported providers or stale
native caches. Unknown always takes the ordinary query path. AI branch copies
must not consult a mutable presence cache shared with live combat.

`FlatDamageInputs` retains owned numerical observations for an explicitly immutable
forecast batch. Its scope binds the exact callback and unit instances, and ends
before mutation. Unsupported sources and cloned retaliation states use fresh
capture. This is a numerical fast path, not a complete replacement for the
hypothetical battle's effects, resources or activation lifecycle. Tests must cover
both reuse and fallback, as well as unchanged live state and RNG streams.

`CHealth::detached` captures the native health ledgers with owned original-count
and maximum-health facts. It uses the same damage, healing and provenance
arithmetic as live health without retaining a Unit. Assignment and prepared swaps
retain the receiving context; capacity changes follow the native explicit
preserve/update/normalize sequence. A forecast must stage a transition before
publication when it requires atomic rejection. This health seam alone does not
establish complete forecast-state or lifecycle coverage.

Focus charge transitions retain the attack's ranged and ammunition mode before
removing its final charge. An accepted ranged strike consumes a charge even when
it misses. Importing an already resolved native strike must not consume it again;
recasting the enchantment must not reset ordinary ammunition usage.

Owned timed-effect transitions use native expiry predicates and explicit clock
origins. A shared clock must advance once, with every dependent recipient refreshed,
or be rejected before projection. Moving a target does not universally break Bind:
ordinary movement, source adjacency and root-preserving relocation have distinct
native rules. Stage effect, health, numeric and queue changes before publication.

`SearchBudget` provides deterministic work counters and nested reservations for one
synchronous decision. Bind a reserved owner before capture or enumeration. Exhaustion
must preserve a legal incumbent and discard incomplete candidates. Prefer explicit
stages for budgeted scoring and capture. Cleanup must neither let `BudgetExhausted`
escape a non-throwing destructor nor accept an incomplete candidate after catching
it. Resource getters do not spend search work.

## Build and package contract

No target links Lua and the checkout carries no Lua rule sources. The retired Lua
backend, its reference scripts and the native-versus-Lua differential tests remain
available only in Git history; the native tests below are the maintained coverage.

Curated packaging expects a matching native binary and facade built from these
sources. Supplying an arbitrary older Lua-dependent executable to the new resource
packager is unsupported. The existing bug-report feature receipt does not attest
native-rule capability; verify the selected build identity independently. The
launcher can still link an existing legacy package's scripts directory when that
directory is supplied, without making it a requirement of a native package.

## Map and saved-rule compatibility

Saved policy parameters remain captured. Current formats use versioned shared rule
tables; retained legacy enums and import paths do not authorize loading an
incompatible game file. Imported HotA events are represented as versioned JSON
expressions and actions inside the
existing map script field. Native continuation frames handle questions, rewards
and combat suspension. The strict importer translates the recognized historical
generated Lua grammar without executing it; arbitrary authored or modified Lua is
not supported.

Newly saved native map-event programs require this engine. Keep pre-migration saves
when testing rollback to an older engine. Retained archive entry names and enum
labels containing `Lua` protect existing format identities and do not imply a
runtime interpreter. This change does not add serialization of suspended events.

The old interpreter's sibling-table iteration order can vary. Native Timed effects
use stable key order. Disease's physical-affliction stamp, and later absolute
stamp values, can therefore differ while fixed-rule selection order remains the
same in the valid, non-overflow domain. Do not assume byte-identical packets or
save state across this migration. `NativeTimedSiblingOrderTest` exercises the
bounded shipped families, recasts, cleanup and later affliction selection; broader
gameplay and save/load checks remain necessary before accepting a release.

## Verification boundaries

Standalone map-import, registration and event-dispatch tests require no original
game assets. `test/nativeDamage` checks the native calculator's boundary arithmetic.
Resource-backed map, combat, spell, damage-capture and AI fixtures run in
`vcmitest` with normal game initialization and original game data; they do not
replace gameplay acceptance.
