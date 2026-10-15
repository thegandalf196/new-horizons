# New Horizons Pending Changes

## Griffin Elite statistics and Genie wishes — provisional experimental update

Griffin: Attack12, Defense12, Damage7–10, HP45. Royal Griffin: Attack14,
Defense14, Damage9–12, HP55. Existing Elite categories, Speed/Initiative7/9,
Growth5, Gold300/450, Leadership220/260, flying and retaliation abilities
remain as authored. These configurable numbers supersede the earlier weaker
Griffin rows provisionally; review numerical balance during creature playtests.

Regular Genie receives the previous Master Genie's three allied random
enchantments at spell proficiency2 and enchantment power5. Master Genie keeps
three shared wishes: allied recipients use the existing beneficial pool;
hostile recipients use legal single-creature Curse, Weakness, Slow, Misfortune,
Sorrow or Forgetfulness. Current captured spell access, immunity and target
conditions filter each pool. No damage, mass spells or control-transfer spells
enter the curse pool. One accepted wish consumes one creature cast and its
activation, without hero mana. Other Genie statistics, economy and artwork
remain unchanged. Core creature definitions and random casters without the
explicit bonus opt-in retain the earlier allied-only policy.

Values are configurable in `config/newHorizonsCreatureExperimentalValues.json`,
generated as the last curated creature patch. Implementation/acceptance evidence
is maintained separately from this design amendment.
Integrate these amendments into canonical Experimental Values after validation.

## Serena — Arcane Preparation

Supersedes the hero-redesign workbook's earlier Prodigy proposal and its
Learning/Eagle Eye starting package. Serena now starts with Basic Wisdom and
Intelligence alongside Basic Metamagic. Her class, primary attributes, army,
spell access and Metamagic use count are unchanged.

With active Intelligence, let K be current Knowledge and I be the ordinary
Intelligence capacity contribution: floor(K × captured Intelligence percentage
/ 100) − K. Exclude High Lore, artifact Mana modifiers and flat capacity/reserve
bonuses from I. At combat start, Serena receives temporary Buffer B:
p = min(100, 25 + 5 × hero level);
B = I > 0 ? max(1, floor(I × p / 100)) : 0.
The percentages are provisional, configurable and captured by the optional
`arcanePreparationSpecialty` policy. Existing Buffer integer limits bound the
actual grant.

This grant does not refill Normal Spell Points or increase maximum Normal
capacity. It joins existing temporary combat Buffer: spending consumes its
temporary provenance, unused temporary Buffer expires at battle end, and
cancellation restores the precombat pools. Permanent Buffer granted by other
effects remains governed by those effects. Policy absence retains the previous
specialty behavior; older saved starting-development profiles remain captured.

## Daremyth — Fortune's Child

The selected Daremyth specialty replaces the earlier Haste successor with
Fortune's Child: add +1 Luck for every five hero levels (first +1 at level 5,
+2 at level 10). This is ordinary Luck; the common Luck system governs its
limits and effects. No separate specialty cap applies. The coefficients are
provisional and configurable through the captured `fortuneChildSpecialty`
policy. Daremyth's starting development and existing Haste starting inscription
are unchanged; Fortune remains a retired hero spell.

Only new heroes carrying this policy convert the exact native Fortune producer.
Earlier captured worlds without the policy keep their existing Haste successor
or original Fortune specialty.
The historical Daremyth Haste row below is superseded for this new policy.

## Perk offers inherit full parent Skill weights

Supersedes the final sentence of canonical "Class Skill offer weights": each
eligible perk receives the hero class's full captured offer weight for its
parent Skill. Do not divide that weight among a Skill's eligible perks. Apply
existing perk legality and tier checks before drawing without replacement;
zero weight excludes the parent's perks. Skill-choice drawing remains separate.

The explicit captured offer policy controls new worlds. Earlier captured
registries without that policy retain their previous offers and pending-choice
validation. Installed defaults must not reinterpret an old saved query.
Integrate this settled rule into the canonical
section after verification rather than retaining a second permanent rule source.

## Provisional coefficients for three authored Druid specialties

Initial configurable values: Elleshar's High Lore10% per level, maximum100%,
applied only to Intelligence's extra maximum Spell Points; Gem's Restorative
Sorceress5% per level, maximum100%, applied to Cure's Spell Power healing
component and First Aid Tent creature healing; Malcom's Natural Philosopher
5% per level, maximum100%, applied to the core Learning experience bonus,
plus25% raw experience from Learning Stones and Trees of Knowledge.

These numbers are provisional, not asserted to come from the authored workbook.
Historical captured absence retains previous behavior. No starting-profile,
army, book or equipment changes accompany these numerical riders. See the
provisional review ledger for boundaries and remaining verification; integrate
the values into canonical Experimental Values after review.

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
upgrade graph changes. See the provisional review ledger for rationale and
verification scope. Integrate these entries into canonical Experimental
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
unchanged. This amendment awaits canonical integration.

Normal class-derived primary growth, skill weights and capabilities follow the
chosen class. At level1 without additional bonuses, Halon's Leadership becomes
875 and Thane's becomes650; existing per-slot starting-army clamping therefore
uses Gremlin maxima17 and13 respectively. The provisional army record reflects
those consequences rather than separately authoring new troop quantities.

## Pasis / Monere — fresh-default Wisp-line specialty successor (provisional)

Provisionally select this replacement for both fresh DEFAULT heroes:
their obsolete ordinary Psychic/Magic Elemental specialty enrolls the current
`new-horizons:wisp` and its Greater Wisp upgrade. Preserve the existing creature
specialty coefficients: +1 Speed, +1 Initiative, +1 Creature Attack and Defense
per five hero levels, capped at +6. No new Rebirth/Magic Elemental rider, book,
army count, class, starting-development rank or biography is authorized.

Canonical creature roster and hero-specialty clauses motivate this amendment:
Wisps replace the ordinary recruitment line, while the workbook's separate
Magic Elemental/Rebirth proposal depends on the removed line and is not adopted.
Accepted Psychic aliases remain the prototypes and explicit PRESET/map,
saved, absent-opt-in and crossover legacy contexts keep their original targets.
A fresh DEFAULT captures its Wisp target once; refresh, level-up, description
and replay follow that captured target without rereading installed defaults.

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
Implementation and acceptance status belongs in the content/integration records,
not in a permanent amendment history. Integrated decisions live in the canonical
source; their former execution receipts are preserved privately.

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
remain explicitly unverified; this replaces the earlier blanket implementation hold,
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
