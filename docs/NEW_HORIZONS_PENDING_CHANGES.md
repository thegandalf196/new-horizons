# New Horizons Pending Changes

## Provisional campaign special-creature Leadership requirements

Enchanters require380 Leadership per creature; Sharpshooters require170.
These explicit requirements apply to freshly captured current rules, including
Dracon/Gelu's special-upgrade targets. Existing authoritative admission must
reject above-capacity upgrades before charging resources or changing species.
Default armies remain original-composition and Leadership-limited; explicit
map armies and older captured requirement tables are not rewritten.

The values provisionally match the maximum upgraded-source-line requirements
(Arch Mage/Zealot and Marksman/Grand Elf respectively), not a final valuation
of the stronger target creatures. No statistics, categories, specialties or
upgrade graph changes. Nine focused native cases, seven capability data checks
and module freshness pass; client and native tests build. See the provisional
review ledger for rationale and verification scope. Integrate these entries into
canonical Experimental
Values after verification; this amendment is not a new permanent authority.

## User-authored Thane / Halon class swap — New Horizons only

The user explicitly assigns Halon to Battle Mage (stable class ID
`core:alchemist`) and Thane to Wizard (`core:wizard`). The curated module
overrides only these two hero class fields. Original core definitions remain
unchanged when New Horizons is inactive. The engine resolves class identity
from the current hero prototype, so this data override also changes the displayed
and queried class of these two heroes in existing New Horizons maps, PRESET
starts and saves. It does not recapture a loaded hero's saved primary/capability
rules, resample armies or migrate development. Preserving historical per-instance
class identity would require separate engine state and is not claimed here.
All other heroes, starting-development packages, books,
specialties, biographies, portraits and original troop compositions are
unchanged. Four focused data checks and the actual starting-army suite4/4 pass,
including exact class, primary, Leadership and first-stack assertions. This
amendment awaits canonical integration and changed playable delivery.

Normal class-derived primary growth, skill weights and capabilities follow the
chosen class. At level1 without additional bonuses, Halon's Leadership becomes
875 and Thane's becomes650; existing per-slot starting-army clamping therefore
uses Gremlin maxima17 and13 respectively. The provisional army record reflects
those consequences rather than separately authoring new troop quantities.

## Pasis / Monere — fresh-default Wisp-line specialty successor (provisional)

Root authorizes the best-reasoned replacement for both fresh DEFAULT heroes:
their obsolete ordinary Psychic/Magic Elemental specialty enrolls the current
`new-horizons:wisp` and its Greater Wisp upgrade. Preserve the existing creature
specialty coefficients: +1 Speed, +1 Initiative, +1 Creature Attack and Defense
per five hero levels, capped at +6. No new Rebirth/Magic Elemental rider, book,
army count, class, starting-development rank or biography is authorized.

Canonical creature roster and hero-specialty clauses motivate this amendment:
Wisps replace the ordinary recruitment line, while the workbook's separate
Magic Elemental/Rebirth proposal depends on the removed line and is not adopted.
Accepted UP304 Psychic aliases remain the prototypes and explicit PRESET/map,
saved, absent-opt-in and crossover legacy contexts keep their original targets.
A fresh DEFAULT captures its Wisp target once; refresh, level-up, description
and replay follow that captured target without rereading installed defaults.

Private source/test proposal only, native validation and root import pending.
Two exact authored table entries and one per-instance resolved target require a
new semantic save feature after Protected Adventure Barriers; older writers/readers reject key
presence rather than dropping it. Actual campaign JSON pools and their outer
StartInfo/world/lobby envelopes receive the same guard. No 144-hero completeness
or playable-delivery claim follows from this bounded amendment.

New Horizons.md is the sole canonical design specification. This document
contains only temporary amendments awaiting integration into its appropriate
sections; it is not a second permanent specification.

For each amendment record its user-approved decision, affected canonical
section, unresolved questions, and integration evidence. Remove an amendment
only after integration is verified, or an explicit decision supersedes it.
Implementation status belongs in the completion audit, not here.

The completed legacy Overrides transition is retained as history in
[NH_OVERRIDE_MIGRATION.md](NH_OVERRIDE_MIGRATION.md). Do not copy its retired
entries here and thereby recreate a permanent override layer.

## Awaiting integration

### Complete default hero starting-development packages

Apply the supplied workbook's remaining96 starting Skill/perk packages through
the existing fresh DEFAULT profile initializer. Preserve the48 previously
settled profiles, including their deliberate workbook differences; each hero
keeps its own faction Skill and one installed, active Basic generic perk with
its authored parent rank. Do not infer new Masterful abilities, specialty
riders, books, classes or troop species from this data migration. Explicit
PRESET/map choices and previously captured saves remain unchanged.

All144 profiles are integrated and data/module checks pass. The actual fresh
initialization witness and army/map/save/legacy controls now pass9/9; biography selection is separately
governed by the only-if-better review, not automatic wholesale replacement.

### Provisional Leadership-safe default starting armies

The user explicitly approves filling the hero workbook's TBD troop quantities
provisionally. Preserve each hero's original troop composition and adjust its
starting quantity range to current per-slot Leadership capacity. Preserve the
selected creature-line successors, ordinary random-range behavior where legal,
and at least one legal troop. Explicit map-authored armies and captured older
contexts must not be silently replaced. No additional elite/champion line,
creature identity, class or specialty is implied by this numerical pass.

Record the resulting144 default dispositions and their capacity checks. Treat
the quantities as prototypes for Phase3 balance, not a finalized economic
recommendation. Principal implementation is accepted: the actual hero9/9 gate
covers all144 Leadership-safe original-composition armies and map/save/legacy
boundaries; the starting-army suite4/4 also passes, including swapped-class
primary/Leadership/first-stack controls. Biography selection is governed
separately by the existing only-if-better review rule.

### Conditional physical-displacement immunities — explicit movement cause

Provisionally, implement Unyielding and Deep Bulwark through a real shared
cause-aware immunity query and authoritative forced-relocation entry, without
inventing a physical attacker. Non-magical forced movement is distinct from
magical relocation, ordinary movement, deployment, return-after-strike, gating,
form restoration and Confusion walking. The animation's teleporting flag and
the existing voluntary-movement boolean are not substitutes for that taxonomy.

Unyielding grants deterministic immunity while Defending or receiving effective
Hold the Line under the stack's current controller. Deep Bulwark grants it only
while actually receiving its Defending Bulwark benefit. Neither rolls RNG nor
uses charges. Check protection and complete destination legality before moving,
clearing Entangle or breaking Orders; preserve normal relocation behavior when
not protected. Accepted movement carries validated cause metadata, with legacy
ordinary movement unchanged and old-format rejection for new metadata.

No standard Version1.0 non-magical push/pull attack is currently authored.
Do not reclassify Implosion or add an unapproved attacker to manufacture one.
Phase1 may credit the conditional passive only after an actually executable
typed authoritative server path, shared detached/UI query and focused state/
compatibility controls prove it. A tooltip, dormant marker or missing consumer
still earns no credit. Standard-game triggering and rendered exercise must
remain explicitly unverified; this amends the earlier blanket UP-167 hold,
not the requirement for a genuine mechanic.

### Remaining spell specialties and Navigation specialists

Provisionally, convert only the exact native specialty producers for these six
heroes, on their local copies under a separately captured opt-in. Preserve
original prototypes, classes, armies and biographies. Fresh default books gain
the stated successor; explicit map books and saved/crossover initialization
retain their book contents. Captured current specialty math is independent of
an explicit map book, as with the other accepted conversion families.

| Hero | Retired specialty | Existing successor | Strengthened component |
|---|---|---|---|
| Ash | Bloodlust | core:fireball | +15% Spell Power damage term |
| Darkstorn | Stone Skin | new-horizons:hexOfPain | +15% Spell Power flat-damage term |
| Astral | Hypnotize | new-horizons:phantomArmy | +20% Spell Power Integrity term |
| Septienna | Death Ripple | new-horizons:plague | +15% Spell Power tick-damage term |
| Melodia | Fortune | core:bless | +20% Spell Power duration term |
| Daremyth | Fortune | core:haste | +20% Spell Power duration term |

Do not strengthen fixed terms, target shapes, duration caps, Hex's separate
damage-sharing percentage, Phantom's cap/Illusionist multiplier, Plague's spread
or Haste's Speed. Hex and Plague capture strengthened damage once on application;
triggered ticks and child infections never apply it again. Phantom preserves
fractional arithmetic before its cap and final HP floor. Actual Lua producers,
shared previews and AI forecasts must consume the components; markers alone are
not implementation. No Masterful variants, hero-level or creature-tier riders.

Separately, give Sylvia and Voy fresh default Basic Logistics, Basic own faction
Skill and the existing Basic Navigation perk through explicit captured starting
profiles. Convert only their exact retired Navigation specialty into +20% of
Logistics's core Movement effect. Do not strengthen Navigation's sea movement or
embark-cost perk, invent Forced March riders or globally alias Navigation.
Preserve explicit map development, books, classes, armies, biographies and
legacy/saved/crossover contexts. The learned Logistics parent is required for a
meaningful specialty. Guard these opt-ins and new supported spell identity
before old-format hero/settings/map/off-map/world/lobby prefixes.
Explicit map development means its chosen ranks/perks remain unchanged: the
exact specialty conversion still applies to a freshly created opted-in current
hero even with PRESET development. Without learned Logistics it adds no Movement.
Saved/crossover heroes without the captured local marker are not converted
during reinitialization. Default profile installation remains fresh DEFAULT-only.

### Crisis Command — complete action before the free Order

Provisionally, resolve the accepted action's entire strike/damage/death sequence
before offering the destroyed stack's hero one immediately actionable free
Order. Do not interrupt between hits or offer it after battle finalization.
Resume ordinary earned extra-activation scheduling after that Order opportunity;
never consume the normal Hero Action. The opportunity must work for the
defending hero during an enemy activation, not become an unusable allowance
deferred until the next friendly turn. Rejected Orders do not count as issued.
Once-per-combat receipt, safe action suspension/resumption and required choice
UI remain implementation requirements, not an assumption that existing regular
hero-turn gating already supports interruption.

### Legendary Reputation — consume on actual positive admission

Provisionally, the calendar-month use applies to the first eligible Diplomacy
join that actually admits at least one creature. Refusal, cancellation and zero
admission preserve the use. Existing authored free-join exceptions do not spend
it merely because no Gold was needed. Use the existing calendar's month identity,
not a new hardcoded day divisor. Validate and commit the free-payment receipt
on the authoritative accepted recruitment path, preserving troop-capacity and
remainder rules. Do not charge ordinary recruitment Gold for this eligible use.

### Opportunist — movement-only tail of the stack's own attack

Provisionally, a positive Luck trigger during the stack's own genuine attack
keeps that same activation open for movement only, bounded by both two hexes
and its remaining Movement. Aggregate the resolved strike sequence; do not
grant another attack, extra activation or repeated movement allowance per hit.
Retaliation, Counterfire and Overwatch reactions cannot create an activation
tail. Preserve existing attack-range restrictions and complete the activation
when the player declines or exhausts the movement-only opportunity.

### Nonspecialist heroes — current default-book successors

Provisionally, give the following fresh default heroes an existing preferred-
School spell in place of their exact retired prototype inscription. Capture the
explicit per-hero table; absence preserves historical behavior. Replace only
the matching prototype spell during genuine initialization without a PRESET
book. Preserve explicit map books, saved/crossover reinitialization, all other
inscriptions, classes, development, armies, biographies and working specialties.
Do not grant additional School ranks, variants or specialty bonuses.

| Hero (`core:`) | Retired inscription (`core:`) | Successor |
|---|---|---|
| Rion | stoneSkin | new-horizons:guardianSpirit |
| Aeris | protectAir | new-horizons:holyArmor |
| Piquedram | shield | core:slow |
| Neela | shield | core:slow |
| Theodorus | shield | core:slow |
| Ayden | viewEarth | new-horizons:confusion |
| Axsis | protectAir | core:forgetfulness |
| Zydar | stoneSkin | new-horizons:blink |
| Vokial | stoneSkin | new-horizons:lifeDrain |
| Galthran | shield | core:slow |
| Nimbus | shield | core:dispel |
| Nagash | protectAir | core:dispel |
| Jaegar | shield | core:curse |
| Malekith | bloodlust | new-horizons:shadowGift |
| Sephinroth | protectAir | core:curse |
| Gird | bloodlust | new-horizons:vengefulVines |
| Dessa | stoneSkin | new-horizons:regeneration |
| Oris | protectAir | core:forgetfulness |
| Saurug | bloodlust | new-horizons:vengefulVines |
| Verdish | protectFire | new-horizons:regeneration |
| Styg | shield | new-horizons:entangle |
| Tiva | stoneSkin | new-horizons:regeneration |

These are explicit functional successors, not aliases claiming identical old
effects. Ayden changes from adventure information to combat misdirection;
Malekith retains Shadow Gift's existing vitality cost. Specialist conversions,
including Coronius, Inteus, Halon and the six unresolved spell specialists,
remain separate. Old-format writers reject presence of the captured table
before any prefix; raw readers must not invent it. Ordinary accepted casting
and AI enumeration use the existing spells without mastery bypasses.

### Seize Initiative — move a pending normal activation

Provisionally, the first accepted Hero-paid Order each combat consumes the
trigger even if no eligible recipient remains. Rejected Orders and free Order
follow-ups do not consume it. Select the latest-scheduled friendly pending
normal activation, excluding the currently active stack, and move that existing
activation into the next friendly slot after the current activation ends.
Preserve enemy ordering and earned immediate extras. Never grant another
activation or refund the Hero Action. Track normal completion independently
from extra-activation resets of ordinary moved flags.

When a Rapid Response waiter is already pending, it retains immediate priority;
Seize chooses the next remaining friendly slot. Preserve enemy relative ordering
and unique existing activations. This provisional simultaneous-promotion rule
does not grant either stack an additional activation.


### Coronius — current offensive starting spell

Provisionally, replace Coronius's retired Slayer default inscription and
specialty with existing Light Holy Wrath for fresh default heroes under an
explicit captured opt-in. Apply the ordinary damage-specialty15% increase only
to its Spell Power-derived term, retaining fixed40 damage and the existing
Undead/Inferno multiplier. This preserves an offensive monster-hunter identity
within Rampart's preferred schools without inventing a Masterful spell, hero-level
rider or Elite/Champion multiplier. Preserve map-specified books, saved/crossover
and legacy contexts, class, starting development, armies and biography.


### Plaguebearer — per-tick propagation limit

Provisionally, Plague's normal propagation limit means its existing one
recipient per afflicted stack's processed tick. Plaguebearer raises that limit
to two distinct eligible recipients, using the existing deterministic ordering.
Capture the effective limit on the initial infection and inherit it on child
infections. Do not alter damage, duration, number of ticks, resistance or the
existing contagious chain, and do not retroactively change infections when
the originating hero's perks change. Keep the normal limit configurable.


### Havoc structural perks — additive damage and ordinary obstacle scope

Provisionally, Demolitionist's50% and Meteorologist's25% structural modifiers
add within one category, with one final floor and the existing structural cap.
Apply Meteorologist only to Meteor Shower and Demolitionist only when the
existing Havoc structural mechanic can affect a target. Do not expand impact
areas or apply these bonuses to creatures or Earthquake. Cataclysm increases
only Armageddon's Spell Power component by20%; structural damage inherits that
component once, leaving fixed damage unchanged.

Preserve ordinary USUAL physical obstacle destruction and reserve ABSOLUTE
scenic landmarks; their fixed placement is not authored destructible HP.
Cataclysm also removes SPELL_CREATED obstacles whose creation spell is ordinary
common combat magic, excluding special spells and creature abilities. Include
hidden ordinary traps and both sides' eligible obstacles without detonating
them. Preserve moats, including Tower's ability-created mines even when they
share a trigger spell with ordinary Land Mine. Fortifications remain their
existing separate HP domain. AI uses the shared damage getter and branch-visible
obstacles, never hidden live-state inspection for extra cleanup rewards.


### Mercenary Captain and Loyal Mercenaries — original recruitment cohorts

Provisionally, accepted paid or free neutral joins record their original
recruiting hero and first three combats under that hero, regardless of whether
Captain is selected yet. Consume one combat at accepted entry, including losses
or retreat; time in another army pauses rather than resets this allowance.
Use whole-resulting-stack nonstacking provenance, copying on split and merging
a bounded union by original recruiter without rejecting otherwise legal merges.
Only positive actually admitted joins qualify; refusal and zero admission do not.
Loyal Mercenaries removes only the faction-mixing penalty contribution for the
qualifying cohort under its original hero. Undead penalties and Morale immunity
remain; excluded cohorts do not fabricate a positive faction-unity bonus.


### Rapid Response — earned extra-activation precedence

Provisionally, finish any immediately earned extra-activation chain before
Rapid Response reorders a waiting friendly stack. At the completed enemy
activation boundary, schedule the latest eligible waiting friendly stack from
the current-round initiative queue to take its existing delayed activation next
after those extras. Do not interrupt, discard or duplicate earned activations.
Wait and same-activation continuations are not completed boundaries. Consume
the once-per-round opportunity only when an eligible existing delayed activation
is actually scheduled; revalidate a deferred recipient before executing it.
This is a moved activation, not a new activation or an Initiative stat bonus.

### Heroic Spirit — useful retaliation lifetime after Morale

Provisionally, a genuine positive Morale trigger grants one additional available
retaliation without stacking repeated grants. It survives the immediately granted
Morale extra activation and expires when the stack's following genuine Creature
Activation begins. Same-activation continuations are not that following activation;
normal or independently granted extra activations are. Do not grant the retaliation
when a purported Morale activation was not actually earned. Preserve ordinary
retaliation consumption, round reset and incapacity restrictions. This explicit
exception avoids erasing the perk at the start of the very Morale activation
that generated it; review the lifetime and round-boundary behavior in Phase2.

### Inteus and Halon — remaining removed starting inscriptions

Provisionally, Inteus replaces removed Bloodlust with existing Crusade! for
fresh default inscription and specialty. Apply the established +20% non-damage
specialty conversion only to Crusade!'s Spell Power-derived offensive/defensive
components, preserving its fixed terms, caps, duration, target shape and costs.
The army-wide successor is an intentional provisional change from Bloodlust's
single-target offense, not a new spell or a numerical compensation rider.
Halon replaces his removed Stone Skin default inscription with existing Guardian
Spirit. This is book-only: retain Metamagic Adept as his sole specialty, with no
additional enchantment scaling. Apply these replacements only to fresh default
books under captured explicit opt-in; preserve map books, saved/crossover and
legacy contexts, class, other development, armies and biographies. Coronius's
replacement remains a separate authored gap, not implicit approval of a proposal.

### Iron Will — recipient carry and same-Order replacement

Provisionally, an unspent and unbroken end-round Order benefit carries separately
for each eligible recipient through its next genuine completed Creature
Activation. Wait and same-activation continuations do not consume that carry;
an incapacitation that actually forfeits the activation does. Time Stop without
an activation does not expire it. Do not revive spent Charge, broken Protect or
completed Second Wind. Reissuing the same Order replaces its prior recipient
snapshot, including carried benefits, rather than preserving parallel instances.
Other Order identities keep their ordinary coexistence rules. Use the shared
effective-benefit lifetime for authoritative execution, previews and detached AI.

### Reactive Weave — independent nonstacking readiness

Provisionally, an accepted enemy Hero spell that actually affects this hero's
army arms half the normal Spell-to-Order Warcasting empowerment, rounded down,
through the end of the next round. Rejected casts and casts with no actual army
recipient do not arm it. Keep ordinary and reactive readiness independently:
the next accepted Order uses the stronger eligible bonus, never their sum, and
consumes both matching readiness candidates. A reaction must not extend or
overwrite stronger ordinary readiness; each candidate retains its own expiry.
The reactive trigger does not count as this hero accepting a Spell, advance an
alternating-action sequence, or grant a Hero Action. Apply existing consumption
perks once to the chosen readiness, not independently to both candidates.
Review readiness coexistence and affected-recipient classification in Phase2.

### Eight retired-development hero starts — explicit captured profiles

Provisionally retain each hero's current Basic faction Skill and replace its
otherwise retired generic starting development with these existing legal choices:

| Hero | Generic starting Skill | Selected Basic perk |
|---|---|---|
| Clancy | Basic Logistics | Pathfinding |
| Piquedram | Basic Logistics | Scouting |
| Thane | Advanced Learning | Scholar |
| Torosar | Basic War Machines | Master Gunner |
| Iona | Basic Learning | Scholar |
| Fiona | Advanced Logistics | Scouting |
| Ignatius | Basic Battlecraft | Tactics |
| Lacus | Advanced Battlecraft | Tactics |

Preserve Advanced parent ranks where an Advanced legacy start is being replaced;
do not invent an Advanced perk. The existing exceptional rank-advancement rule
allows the unfilled slot. All chosen parents are legal for the unchanged class
and all perks are currently active. The faction Skill replaces the retired
second generic choice; no hidden Wisdom/Mysticism/Resistance grants are added.
These are explicit default-start profiles, not a global migration policy.
Capture an optional profile collection in the hero rules, applying it only to
fresh creation with default Secondary Skills. Explicit map starting rosters,
saved/crossover initialization and captured legacy or absent-profile contexts
remain unchanged; do not apply prototype perks unconditionally to a roster
without its parent. Preserve class, army, biography, spellbook and specialty.
Reject malformed profile/perk/parent admission and unsupported old-format key
presence before enclosing serialization prefixes. Review hero differentiation
and Advanced empty-perk-slot pacing after functional coverage is established.

### Loynis and Zubin — authored offensive-enchantment replacements

Loynis provisionally replaces removed Prayer with the existing Light Crusade!
for fresh default inscription and specialty. Crusade! is the authored successor
to army-wide Prayer. Apply +20% only to its Spell Power-derived Attack, Defense,
Initiative and Magical Damage Reduction components, once before existing floors
and caps. Preserve fixed bases, duration, target eligibility, costs and actions.
Zubin provisionally replaces removed Precision with existing Sorcery Focus Magic
for fresh default inscription and specialty. Apply +20% only to its numerical
Spell Power-derived penetration component; preserve fixed10%, cap20%, duration3,
first-shot handling, post-hit Arcane Breach marks and mark cap3. Neither conversion
adds a separate Masterful spell or hero-level rider. Preserve current class,
biography, starting Skills/army, map-prescribed books and captured legacy rules.
Existing legitimately inscribed combat-spell casting governs their known starts;
these replacements grant no unknown spell or ordinary acquisition exemption.
Review high-tier starting impact and enchantment/perk composition in Phase2/3.

### Halon — additional Metamagic capacity without additional mastery

The selected Metamagic Adept specialty grants exactly one additional use per
combat at each actual learned Metamagic rank. Preserve Halon's selected Basic
Metamagic/Basic Spellcraft start and existing specialty bonus; do not restore
Mysticism. Basic/Advanced/Expert capacities become2/3/4, respectively, while
actual mastery remains Basic/Advanced/Expert. Extra capacity does not unlock
Expert-only Grand Metamagic or bypass its perk requirement. A hero without
the learned faction Skill receives no capability merely from a capacity bonus.
Keep ordinary heroes'1/2/3 capacity unchanged. Accepted follow-up consumption,
pending grant expiry, cancellation and Counterspell semantics remain unchanged.
Separate capacity from rank across authoritative transitions, detached forecasts
and UI. Capture any changed rules/state admission explicitly; unsupported old
formats must reject unrepresentable state before writing prefix bytes, not lose
the fourth use silently. Review this provisional clarification after focused
Basic/Advanced/Expert and saved-state controls. Halon's removed Stone Skin start
is a separate unimplemented replacement decision, not addressed by this repair.


### Merist and Labetha — provisional defensive specialty replacements

Under the user's provisional-rule authorization, replace removed Stone Skin
in fresh default starts and specialties with ordinary Hydra's Vitality for
Merist and Guardian Spirit for Labetha. Merist retains a Nature-oriented defense
spell for living Fortress troops; Guardian Spirit also protects Labetha's
nonliving Elementals. Apply +20% only to each spell's SP-derived numerical
component. Hydra's fixed25%, maximum50%, three-round duration and regeneration
rate remain unchanged; Guardian Spirit's fixed50HP and two-round duration remain
unchanged. Preserve existing School/perk staging, other profiles, map books and
captured legacy contexts. These are authored provisional identities, not the
workbook's undefined Masterful variants. Independent review and focused live/
detached/default/map/legacy/save evidence must precede canonical integration.

## Integrated history

### Training, War Machines and Spellcraft/Divine clarifications — 2026-10-09 (integrated)

The exact eleven-perk principal128/128 and adjacent15/15 gates pass.
The Training and War Machines provisional contracts move into their canonical
perk pools. Spellcraft's prospective target count, accepted-cast duration receipt
and disjoint captured-school history, plus Divine Discipline's recipient carry,
are stated explicitly without changing the settled automatic Crown and Altar
pair rule. Other hero/capacity amendments remain pending.

Training79803 passes its actual entry/round1/expiry regression1/1 and
setup92086 passes29/29 after the narrow typed-marker initialization repair.
These subfilters are evidence for the repair, not acceptance of the full batch.

Fresh v5 principal83705 completed128 cases:127 passed and1 failed in43.876s.
The remaining failure exposed an authoritative duplicate-Order guard mismatch:
the shared callback allowed next-round carried reissue, but StartAction rejected
it. The reviewed correction retains same-round rejection and replaces only
prior-round carried state. V6 linked9350 and privacy80564 pass; principal36894
passes128/128 in43.678s and adjacent13551 passes15/15 in6.823s, with zero
failures/errors/skips and exact binary/resource/isolated-child proof.

Other bounded repairs preserve actual mechanics: typed Training markers export
once through canonical localInit; empty valid tiles use ordinary terrain
battlefield fallback; replay uses its actual original location; combat Attack
includes the existing native-terrain bonus; detached forecasts use real-player
callbacks; and Healer/Guardian use separate legal Basic-tier controls. Extend
Spell now classifies the same legacy timed-effect fallback as the spell decoder.
None loosens rank selection, receipts, immunity, caps or save admission.

The focused accepted coverage delta is eleven perks,282 to293 of310. This
does not establish rendered acceptance or a new selected playable delivery.

### Contacts, Phoenix Spark, Arcane Memory and Aenain — 2026-10-09 (integrated)

The canonical rows now record owned-entry/first-empty-row growth, actual scroll
completion and ordinary acquisition, and fixed battle-start Phoenix substitution
with once-use placement semantics. The hero section records Aenain's SP-only
Frailty replacement and preserved default/map/legacy boundaries. Independently
reviewed production and fixture repairs link14567; exact retry2 principal34618
passes42/42 in13.975s and adjacent26046 passes23/23 in8.785s, zero failures,
errors or skips. Initial failed logs/XML remain private. Provisional choices
remain reviewable in the second-look ledger; implementation and playable
delivery are distinguished in the queue.

### Sage catalog and four Frailty specialties — 2026-10-09 (integrated)

Both Sage rows now state the eligible undisplayed catalog, saved school labels,
ordinary learning/map-ban rules, deterministic selection, first built-Guild
visit receipt and separate reveal rows. The canonical hero-profile audit names
Cuthbert, Olema, Mirlanda and Xsi's existing Frailty replacement, SP-component
conversion, unchanged caps and preserved default/map/legacy boundaries.
Independent source review and exact native70790 pass36/36, including Sage10,
Frailty11 and Shared Purpose15; adjacent61175 passes12/12 after linked38972.
Eligibility, initial corpse provenance and variant prerequisites were preserved
through reviewed fixture-only repairs. These provisional choices remain in the
second-look ledger; other missing hero identities remain separate gaps.

### Thant — authored provisional specialty replacement — 2026-10-09 (integrated)

Integrated into the canonical hero starting-profile audit: existing Re-animate
replaces removed Animate Dead in fresh default starts and specialty, +20% only
to its SP-derived restoration component. Fixed220, temporary casualty rules,
map-prescribed spellbooks, legacy contexts and other profile choices remain.
Source review and ten principal cases pass in native88843; fifteen adjacent
controls pass in3070 after linked25787. Broader casualty/cap composition remains
in the second-look ledger; no new rendered or playable acceptance is inferred.

### Polymorph impossible-footprint restoration — 2026-10-09 (integrated)

Provisional decision under the user's authorization to resolve rule ambiguity:
Polymorph normally restores its original body after two rounds or Dispel, using
the approved nearest legal position when necessary. If no legal position for
the original footprint exists anywhere, retain the current form, surviving HP
and position; retry safe restoration at later round boundaries. Never overlap
another stack, delete creatures or heal them to force restoration. This is an
exceptional delayed restoration, not a new normal duration or a successful
immediate Dispel. It must be represented accurately in status/help text.

Affected canonical section: Chaos School / Polymorph, restoration and duration.
Second-look question: whether another explicit no-space policy is preferable.
Integrated into Chaos / Polymorph together with the Phantom body/Integrity
distinction and explicit Shapeshifter independent draws/whole-stack Army Value.
These reasoned interpretations remain in the second-look ledger. Production
implementation and focused evidence remain separately tracked; integration of
wording is not gameplay completion.

### Elemental Convergence terrain completion — 2026-10-09 (integrated)

User approves Earth for Dirt, inland Sand and Wasteland, and Water for Swamp
and coastal battlefields including coastal Sand. Integrated into the canonical
Experimental Values terrain table, with captured coastal battlefield context
taking precedence over forced Sand. Runtime and AI implementation of the spell
and its terrain-dependent perks remain UP072 work, not completed coverage.

### Battlefield Mastery and Last Stand scope — 2026-10-06 (integrated)

User resolves Battlefield Mastery's award in favor of the first eligible ordinary
stack: an ineligible War Machine leaves it available. Last Stand protects ordinary
stack health, not clones/Phantom Integrity; when lethal retaliation triggers it,
the surviving attacker's current activation ends. Integrated into both canonical
perk rows. Production completion and delivery remain separate in UP-156/UP-079.

### Both Cabir forms are ranged — 2026-10-06 (integrated)

User explicitly directs both Cabir and Cabir Master to shoot. Apply to the
canonical Academy creature roster/ability section; this supersedes inherited
basic Gremlin melee-only gameplay. Preserve both forms' elemental defenses and
Master's repair ability. Ranged art and ordinary ammunition/targeting must be
implemented together; no implication of No Melee Penalty is granted.
Integrated into the canonical Academy creature ability paragraph and roster row.
Source/runtime and art acceptance remain tracked in UP-265; this document change
does not claim shooting animations or playable delivery are complete.

### Barehanded Cabir and repair abilities — 2026-10-06 (integrated)

User explicitly removes the golden pot/fire vessel from both forms. Both get
Fire resistance and Water weakness; Cabir Master repairs Golems and Gargoyles.
Integrated into the canonical Academy creature presentation/ability rule,
superseding the initial unchanged-gameplay restriction for these specified
abilities only. User subsequently approves provisional50% less Fire damage and
25% more Water/Frost damage, and healing plus restoration of fallen creatures
within a surviving stack. These answers are integrated in the same canonical
rule. The canonical implementation prototype now specifies one repair per
combat, 10 HP per acting Cabir Master and a normal creature activation. These
remain tunable values; implementation and artwork status remain in UP253.

### Cabir replacement — 2026-10-06 (integrated)

User-approved decision: replace Academy's Gremlins and Master Gremlins with
original Cabir and Cabir Master artwork and names, retaining the current
creatures' gameplay initially. Integrate into the canonical Academy roster;
do not invent new stats or abilities from the Heroes VII reference. Full
base/upgraded battle animation sets and all creature presentation bindings are
required, not just portraits. Original artwork must use HoMM3 Art, including
provisional assets. Integrated into the canonical creature roster and its
Academy presentation rule. Implementation and delivery are tracked in UP248;
the approved standing-frame draft is not a completed runtime animation set.

### Academy visual identity — 2026-10-05 (integrated)

The user supplied the Academy definitive art handoff and requested integration.
Its visual direction is integrated into the canonical Tower section: Academy
display name and scholarly sandstone presentation, existing Tower identity and
New Horizons gameplay preserved. The user separately confirmed Sand as Academy's
native terrain; existing authored map terrain is not repainted. Original retained pixels remain externally
referenced. Asset integration and playable evidence are tracked in UP-235.

### Crown and Altar automatic second-action bonus — 2026-10-05 (integrated)

In response to predeclaring a pair target versus redesigning the perk, the user
preferred the less-clicky option. The canonical perk now automatically boosts
the second action's rating-derived components by20% on friendly recipients
also affected by the first, in either pair direction. No additional button,
target-preselection dialog or retrospective first-action change is introduced.
The first action is unchanged. Integration verified in the Divine Mandate perk
pool; its planned registry description is synchronized. Runtime implementation,
recipient provenance, AI and focused evidence remain work tracked in UP108/
UP023, not claimed complete by this specification change.

### Commanding Presence effective lifetime — 2026-10-02 (integrated)

User-approved decision: its negative-Morale floor ends for a recipient when
that recipient's Order benefit is spent or broken. It does not persist solely
because the Order snapshot remains until round end. Examples include a consumed
Charge, a broken/exhausted Protect pair, and the end of Second Wind's extra
activation. Affected canonical section: Command perk pool, Commanding Presence.
No unresolved lifetime question remains. Integrated into the canonical Command
perk pool's Commanding Presence row. Implementation and validation belong in
UP-142, not in this amendment.

### Polymorph footprint relocation — 2026-09-30 (integrated)

The user chose relocation to the nearest legal position when a replacement
footprint cannot fit at the original hex. Integrated into canonical Polymorph:
do not exclude the form solely for its original-position footprint; retain
ownership, allegiance and current Initiative queue position. Runtime coverage
and the shared form/HP foundation remain tracked under UP-066.

### Paradox Shield and damage caps — 2026-09-30 (integrated)

The user selected the existing physical cap: Paradox Shield adds ten percentage
points after the spell's own formula cap, but total Physical Damage Reduction
still cannot exceed 80%. Magical protection can reach 90%, subject to its
ordinary 95% total cap. This clarification is integrated into the canonical
Shield of Chaos section; implementation evidence belongs in UP-063.

### Hand of Fate secondary mitigation — 2026-09-30 (integrated)

User-approved decision: after selecting the secondary recipient uniformly from
the surviving friendly/enemy pool, apply that recipient's own magical defenses
to the hit whose base is half the primary target's actual HP loss. Defenses do
not affect selection and must not cause a reroll. This clarification is integrated
into the detailed Chaos / Hand of Fate section of New Horizons.md.
Runtime implementation and delivery remain tracked in the priority queue.

### School-rank spell potency — 2026-09-27 (integrated)

Basic, Advanced, and Expert rank in each of the six Magic Schools must
progressively strengthen applicable spells of that school, in addition to
gating ordinary acquisition of higher spell levels. A legitimately inscribed
spell remains castable without the rank. Begin with a modest, coherent
Heroes V-like damage rule: improve the coefficient that multiplies Spell Power,
not merely flat base damage. Expert rank does not automatically grant Mass
versions; those remain perk-granted. For non-damage spells, choose meaningful
effect-appropriate rank improvements, with explicit exceptions instead of a
blind numerical multiplier. The initial authored coefficient ladder (provisional
balance) is: no School rank 100%, Basic 115%, Advanced 130%, Expert 145% of
the spell's Spell Power coefficient. Apply it before existing integer rounding;
preserve flat base terms, caps, costs, target shape, mitigation, and specialty
rules. For a multi-school spell, use the highest applicable rank once, never
stack the schools. Adventure Spells remain neutral and unscaled. Saved magic
rules version 3 carries the ladder; v1/v2 saves retain 100% so an ongoing game
does not silently change its formulas. Numeric healing, temporary HP, and
restoration may follow the same coefficient principle where the formula has an
actual Spell Power term. Discrete/binary spells need authored rank variants or
an explicit exception. The shared rule is now in the canonical Markdown Magic
Skills section; implementation and validation of specific spell effects remain
tracked in UP-027.

The five amendments approved before 2026-09-27 were integrated into the
canonical DOCX: Spell Lock duration stacking, Arcane Acquisition target
semantics, generic Faction-Skill combat statuses, consume-on-use Metamagic and
its accompanying perks, and ordinary Mage / Arch Mage melee penalties. The
edited document passed ZIP integrity and a complete 191-page LibreOffice render;
the affected pages were visually inspected without clipping or overlap.

### Bulwark of the Mire perk rules — 2026-09-27 (integrated)

Affected canonical section: Fortress faction Skill, Bulwark of the Mire perk
table. User-approved changes integrated into the repaired canonical
`New Horizons.md`:

- Toxic Spines: “the first melee attacker each round” is tracked separately
  for **each Defending Bulwark stack**, not once per hero army. That attacker
  must suffer Bulwark reflection before receiving physical Poison. Its base
  damage is `max(1, floor(actual reflected HP damage / 4))`; on the victim's
  next three real activations it deals base, `floor(1.5 × base)`, then
  `2 × base` physical damage. A new application with equal or higher base
  replaces the old potency and restarts three ticks; a weaker application is
  ignored. Poison does not stack. Cure removes it; ordinary Dispel does not.
- Immovable: its first-physical-creature-hit 25% final-damage reduction applies
  **only while the particular Bulwark stack is Defending**. Track the first
  qualifying hit separately per stack per round.

The repaired Markdown contains both clarified rows, checked against the perk
registry with no unapproved cell differences. The original DOCX remains an
archival conversion reference. No design choice remains open for these rules.


## All38 audited default-development replacements — private integrated provisional candidate

The user authorizes the following exact fresh DEFAULT development profiles.
Ranks1/2/3 mean Basic/Advanced/Expert. All selected perks are existing active Basic;
no new spell, specialty rider, army, biography or class is introduced.

| Hero | Generic parent and rank | Basic perk | Own faction and rank |
| --- | --- | --- | --- |
| core:adelaide | new-horizons:havocMagic 1 | cryomancer | new-horizons:divineMandate 2 |
| core:aeris | new-horizons:logistics 1 | scouting | new-horizons:sylvanLuck 1 |
| core:aine | new-horizons:estates 1 | taxCollector | new-horizons:metamagic 1 |
| core:alamar | new-horizons:shadowMagic 1 | bloodDrinker | new-horizons:shroudOfMalassa 1 |
| core:andra | new-horizons:wisdom 1 | intelligence | new-horizons:bulwarkOfTheMire 1 |
| core:ash | new-horizons:chaosMagic 1 | frenziedCurse | new-horizons:demonicGating 1 |
| core:astral | new-horizons:spellcraft 1 | concentration | new-horizons:metamagic 2 |
| core:axsis | new-horizons:wisdom 1 | mysticism | new-horizons:demonicGating 1 |
| core:ayden | new-horizons:wisdom 1 | intelligence | new-horizons:demonicGating 1 |
| core:caitlin | new-horizons:wisdom 1 | intelligence | new-horizons:divineMandate 1 |
| core:charna | new-horizons:battlecraft 1 | tactics | new-horizons:necromancy 1 |
| core:coronius | new-horizons:spellcraft 1 | concentration | new-horizons:sylvanLuck 1 |
| core:daremyth | new-horizons:luck 1 | secondChance | new-horizons:metamagic 1 |
| core:deemer | new-horizons:logistics 2 | scouting | new-horizons:shroudOfMalassa 1 |
| core:elleshar | new-horizons:wisdom 1 | intelligence | new-horizons:sylvanLuck 1 |
| core:geon | new-horizons:learning 1 | eagleEye | new-horizons:shroudOfMalassa 1 |
| core:ingham | new-horizons:wisdom 1 | mysticism | new-horizons:divineMandate 1 |
| core:isra | new-horizons:learning 1 | historian | new-horizons:necromancy 2 |
| core:jaegar | new-horizons:wisdom 1 | mysticism | new-horizons:shroudOfMalassa 1 |
| core:jeddite | new-horizons:spellcraft 1 | concentration | new-horizons:shroudOfMalassa 2 |
| core:malcom | new-horizons:learning 1 | eagleEye | new-horizons:sylvanLuck 1 |
| core:mirlanda | new-horizons:shadowMagic 1 | witheringTouch | new-horizons:bulwarkOfTheMire 2 |
| core:nagash | new-horizons:estates 1 | taxCollector | new-horizons:necromancy 1 |
| core:nimbus | new-horizons:learning 1 | eagleEye | new-horizons:necromancy 1 |
| core:oris | new-horizons:learning 1 | eagleEye | new-horizons:bloodrage 1 |
| core:rosic | new-horizons:wisdom 1 | mysticism | new-horizons:bulwarkOfTheMire 1 |
| core:sanya | new-horizons:learning 1 | eagleEye | new-horizons:divineMandate 1 |
| core:saurug | new-horizons:estates 1 | prospector | new-horizons:bloodrage 1 |
| core:sephinroth | new-horizons:estates 1 | prospector | new-horizons:shroudOfMalassa 1 |
| core:septienna | new-horizons:spellcraft 1 | arcaneFocus | new-horizons:necromancy 1 |
| core:serena | new-horizons:learning 1 | eagleEye | new-horizons:metamagic 1 |
| core:straker | new-horizons:warcasting 1 | spellward | new-horizons:necromancy 1 |
| core:terek | new-horizons:battlecraft 1 | tactics | new-horizons:bloodrage 1 |
| core:thant | new-horizons:wisdom 1 | mysticism | new-horizons:necromancy 1 |
| core:thorgrim | new-horizons:warcasting 2 | spellward | new-horizons:sylvanLuck 1 |
| core:tiva | new-horizons:learning 1 | eagleEye | new-horizons:bulwarkOfTheMire 1 |
| core:vidomina | new-horizons:learning 1 | scholar | new-horizons:necromancy 2 |
| core:xyron | new-horizons:havocMagic 1 | pyromancer | new-horizons:demonicGating 1 |

Preserve original8 profiles verbatim, PRESET/map-authored choices and captured
absence/legacy/saved/reinitialization boundaries. Retain Advanced own-faction
ranks for Mirlanda/Adelaide/Astral/Isra/Vidomina/Jeddite; retain native training
through Thorgrim AdvancedWarcasting and Deemer AdvancedLogistics with Basicperks
and no automatic Advancedperk. Planned Demolitionist/undefined IronDiscipline
and workbook Masterful/class/book/raising/level-income riders are not adopted.
The shared validated-profile initializer bypasses only legacy migration for an
explicit profile, preventing Rosic/Andra/Caitlin Wisdom erasure; other paths remain.
Structural rank1..3 supports retained faction ranks; genericparent remains<=Advanced.
Root-imported newer hero flags/cloners/epoch guards and historical fixture key
erasures are preserved; no semantic epoch is newly appended by this candidate.

Private authored evidence:234 cases233map+1MirlandaFrailty, UNRUN. Existing8 and
all family controls remain; publicqueries cover Mana/XP/learning/sight/fire/income
and actualCrystal/GemProspector visits. Original39-gap disposition is38 profiles
plus Voy separate Navigation candidate. Source review is separate from native
acceptance/delivery and does not establish all144hero content coverage.
