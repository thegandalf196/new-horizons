# Mechanic integration review contracts

This guide retains durable engineering requirements extracted from the former
provisional-review ledger. Detailed choices, coefficients, superseded readings
and unresolved design alternatives are preserved in the private
[legacy amendment index](https://github.com/thegandalf196/new-horizons-docs/blob/main/proposals/legacy-amendments.md).
It is not another specification or a claim that every requirement below has
current acceptance. Consult [integration limits](NH_INTEGRATION_GAPS.md) and
[regression guards](NH_REGRESSION_GUARDS.md) before classifying an old source lead
as a current defect. Record new design choices privately.

## Captured state and save admission

Installed configuration and resolved hero/battle policies are separate inputs.
Keep absent-policy history, explicit opt-ins and malformed-present rejection
distinct. An optional block with zero configured chances can still select a
current policy. Validate contradictory retired state before installation, damage
or broadcast, not by clearing it after mutation. Keep raw historical codecs
separate from current authoritative admission.

Default hero profiles, books, creature-line targets, class-derived attributes
and capabilities must retain their own DEFAULT/PRESET/map/save/crossover rules.
Do not recapture an old hero or grant a perk without its parent to repair a
fixture. Reinitialization must preserve captured identity and avoid changing
shared prototypes. Starting armies use independent per-slot capacity; explicit
map armies and old captured requirements are not silently resampled or trimmed.

New semantic fields require compatible guards at inner and enclosing
hero/settings/map/world/campaign-pool/lobby boundaries, including no-hero
containers. Refuse unsupported writers before prefix bytes and validate raw
readers before installing state. Keep primary-profile versions independent as
described in [primary progression](NH_PRIMARY_PROGRESSION.md).

## Accepted action and RNG provenance

Capture action origin, effective controller, source policy and relevant numerical
inputs at the authoritative acceptance boundary. Reactions, primary ordinary
attacks, active creature spells, Hero casts and indirect collateral are not
interchangeable merely because their animation or damage element looks similar.

Use an action's retained Luck/Morale receipt for its targets, delayed effects and
internal echoes; independently admitted follow-ups remain independent. Refunds
reverse actual charged pool provenance rather than turning temporary Buffer into
permanent points. No hidden additional RNG draw belongs in a preview or rejected
action. Distinguish a genuine new admission from a compound action's internal work.

Legality precedes a random outcome when the producer requires a legal pool.
For Genie wishes an empty pool spends neither charge nor RNG. Fate Dealer's
with-replacement spill expectation must match its two actual draws: for a nonempty
legal pool with H hostile, F friendly and N=H+F, each hostile has probability (H+2F)/N² and each friendly
F/N². Current controller, duplicate draws, recipient defenses and no-reroll rules
must agree between authority and AI; do not replace them with initial-owner tests.

## Activation and recipient lifecycles

Keep normal activations, earned extras, Wait, same-activation continuations and
actual forfeitures distinct. Promotion moves an existing activation; it does not
duplicate it. Rapid Response and Seize preserve enemy order, active front,
membership and their separate receipts, including deferred recipients and earned
extra chains. Swift Rebirth's birth-round normal slot follows the same distinction.

A fresh full activation can renew per-activation creature-cast readiness, never
spent charges. Direct creature spell output and ordinary attacks use the captured
activation policy without granting Hero, healing or indirect effects a new bonus.
Complete an accepted strike/death sequence before a post-action Order opportunity;
a defending hero's free allowance must be genuinely usable, not a dormant marker.

Order recipients retain their own benefit and expiry snapshots. Same-Order reissue
replaces its previous snapshot; distinct Orders keep ordinary coexistence.
Spent/broken/completed benefits do not revive. Royal Standard's scheduled recipient
floor and Commanding Presence's benefit-consumption lifetime are not interchangeable.
Ordinary/reactive Warcasting candidates retain separate expiries, use the stronger
eligible value and consume matching readiness without adding the values or actions.

Frozen's next-normal-slot forfeit and successful-application round stamp survive
the relevant cleanse/Shatter/extra-activation paths. Generic bonus duration can be
paused by Time Stop or Spell Lock; a one-turn bonus alone is not proof of literal
first-round expiry. Superseded Heroic Spirit retaliation and generic Lucky Recovery
healing must not be restored as fixes to current hero-Morale/Luck consumers.

## Movement, control and form restoration

Use explicit movement cause and destination legality, not teleport animation or a
voluntary boolean as a substitute for forced-displacement taxonomy. Validate before
position, Entangle or Order mutation. No authored standard physical push producer
may be invented just to claim conditional-immunity acceptance.

Overwatch's accepted voluntary outside-to-inside transition, flying landing and
stable reactor order differ from teleport, forced movement, deployment, gating and
return-after-strike. Occupied transit cannot publish overlap: use the next legal
inside endpoint, or no reaction if transit exits before one. Preserve recursion
guards and next-attack bonus consumption; current-controller hostility deserves
separate controls from original-side perk ownership.

Reality Warp uses an indivisible validated reciprocal plan. Capture allegiance
before cleansing; one resisted hostile endpoint blocks the exchange. Preserve
caster attribution, remaining duration, captured strength and explicit unknown
legacy provenance while deriving transferred beneficiaries from the new recipient.
Move pending effect state, not the recipient's resolved behavior history. Orders,
forms and other typed nontransferable state stay excluded.

Form expiry/Dispel restores only to a legal original footprint. If none exists,
retain the form, HP and position and retry rather than overlapping, killing or
healing. Preserve pending-form provenance, deterministic unit-ID reconciliation,
client refresh and separate damage-cache identities for distinct outcomes.
Phantom offensive body HP is not Integrity; clear form provenance before lethal
Integrity reset. Keep explicit binary-snapshot rejection until the complete form
save contract exists.

Town Portal, pathfinding and accepted boarding costs must share their resolver,
tie order, occupancy and travel-layer rules. A cost-reduction perk cannot increase
a genuinely free action or silently retarget a blocked nearest destination.

## Visits, recruitment and calendar receipts

Qualification and receipt consumption are producer-specific. Preserve ownership at
entry where required, actual positive admission/resource recovery where required,
and first-visit consumption even before perk or building acquisition where authored.
Do not generalize one rule across Academic Study, Sages, Contacts, Prospector,
Legendary Reputation or Pursuit March.

Use authoritative absolute day/week/month identities and publish receipts before
reward side effects. Keep previews, AI and accepted grants on the same rule gate;
old unclassified rewards do not acquire a new classification merely by loading.
Town residence uses reciprocal current ownership and the actual day transition;
aggregate income before ordinary handicap without inventing another treasury grant.

Recruitment provenance requires the real paid/free producer, not generic stack
insertion. Whole-resulting-stack training and original-recruiter cohorts preserve
their respective split/merge/transfer rules without additive duplication.
Temporary army detachment is not transfer. Retained retreat/surrender heroes and
troops require their ordinary postcombat lifecycle, not a newly invented survival
policy. Select reciprocal Scholar teachings before either mutation and preserve
a symmetric meeting receipt without consuming an empty opportunity.

## Numerical components and effect metadata

Strengthen only the configured component: separate fixed terms, SP terms, caps,
floors, duration, target shapes and companion effects. Capture initial numerical
strength once for inherited ticks/infections; do not apply the specialty again
to child effects. Preserve fractions until the specified final floor.

Keep maximum Normal capacity, current Normal points and permanent/temporary Buffer
distinct. Capacity changes are not refills; flat capacity is not multiplied into
other contributions. Spellward strengthens one existing basis-point source, not
Resistance RNG or another aura. Effective perk/hero identity and allegiance remain
live queries against the captured policy.

Structure damage, binary scenery, special/ordinary spell-created obstacles, moats
and creature HP are separate domains. Do not invent HP for scenery or classify an
obstacle only by its triggered spell. Wound healing and casualty restoration are
different budgets; Phantom body, Integrity and temporary HP also remain separate.

Logical debuff identities are not Bonus-component counts. Snapshot the complete
eligible set before resolving damage, preserve source/hostility metadata and do
not infer innate-versus-magical status from duration alone. Rebirth inherits
captured original-HP and appearance state without double application. Contact
premiums use current directional geometry, not historical hit counts; primary
ordinary elemental attacks and magical collateral require distinct controls.

## Source and regression ownership

- Captured hero profiles: lib/entities/hero/NewHorizonsHeroRules.cpp,
  NewHorizonsPrimaryProfile.cpp and NewHorizonsPrimaryGrowth.cpp; hero-rule,
  primary-growth and server HeroGrowth fixtures.
- Activation/reactions: lib/battle/NewHorizonsBattlecraft.cpp,
  CBattleInfoCallback and server battle flow/action processing; Battlecraft,
  Discipline, Morale and elemental fixtures.
- Reciprocal effects/forms: lib/spells/NewHorizonsRealityWarp.cpp,
  battle/BattleForm and corresponding spell effects; RealityWarp/form fixtures.
- Capacity, healing and damage: CGHeroInstance, shared spell mechanics and Lua
  producers; SpellPointCapacity, Spellward, DirectDamage and Metamagic fixtures.

Every relevant forecast regression checks unchanged live/parent/sibling state and
RNG, including nested or lazily materialized children. Use actual accepted actions,
typed receipts and independently calculated oracles; marker/helper tests do not
substitute for a consumer. Keep refusal atomicity, current/absent policy, old-format
and actual AI submission controls, with full state fields rather than normalizing
away an unexplained discrepancy.

Army-exchange slot and exact-plan invariants remain in
[AI exchanges](NH_AI_EXCHANGES.md); shutdown/restart/waiter/repaint invariants remain
in [performance guardrails](NH_PERFORMANCE_EXPERIMENT.md). Native, world-save,
ordinary rendered transitions and platform acceptance remain separate evidence.
This documentation migration establishes none of those runtime results.
