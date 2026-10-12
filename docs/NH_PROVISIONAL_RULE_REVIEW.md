# New Horizons provisional rule review

## Hero Luck reworks and Heroic Spirit — accepted-action receipts

The canonical generic Luck rows now replace Lucky Recovery/Gambler/Twist of Fate
with Lucky Day/Erratic Mana/Druidic Luck, retaining their saved IDs and Advanced/
Advanced/Expert ranks. Sylvan Lucky Recovery is separate and unchanged. Heroic
Spirit retains its Discipline Expert identity but grants one Order-only Hero
Action from positive hero Morale after an ordinary Order; bonus Orders do not
recur. No creature retaliation or extra creature activation is granted.

Chosen interpretation: reuse actual positive hero Luck and existing Luck curve/
dice/bias; use the actual captured Morale curve/dice for Heroic Spirit. Capture
one action-origin Luck result rather than rolling again for each target, delayed
tick or internal echo/mirror. Independently admitted follow-ups receive their
own Luck check. Lucky Day scales intrinsic Order potency only, not duration,
targets, activation count or companion-perk bonuses. Druidic Luck scales numerical
HP/structural damage including captured damage-over-time/zones, not healing,
counts or instant kills. Erratic Mana may refund a paid accepted spell even if
its output is negated; refund floor50% of actual charge in reverse charge order
(normal, permanent Buffer, temporary Buffer), preserving each pool's origin.
Internal echoes/mirrors cannot generate another refund.

Damage interpretation: Druidic Luck multiplies raw numerical damage before the
existing recipient mitigation, caps and integer rounding. Consequently, the
final HP loss need not be exactly twice an independently rounded ordinary hit.
Life Drain retains its existing healing-from-actual-damage rule; its healing
coefficient is not doubled separately. Revisit both distinctions during balance
review rather than introducing a second critical multiplier after mitigation.

Order interpretation: complete the ordinary intrinsic formula, including its
Warcasting/Divine efficiency inputs and existing formula cap, then apply Lucky
Day. Crown and Altar's completed increment and separate companion-perk effects
remain unchanged. This also applies to active Second Wind creature spells;
neither route adds an activation or reapplies the formula cap after Luck.

Rationale: these boundaries keep the new hero-facing effects tied to accepted
actions, avoid multiplying action economy or applying a cast repeatedly, and
prevent a temporary reserve becoming permanent through a refund. The current
100% perk chance scales,200% potency/damage and50% refund are provisional test
numbers recorded in canonical Experimental Values; no independent trigger curve
or configurable Heroic Spirit grant count was introduced. Optional saved blocks
default to these values when absent, preserving readable earlier snapshots.

Implementation surface: captured `heroLuck`/`heroMorale` values in
`config/newHorizonsMagic.json` and its v3 schema; four stable-ID descriptions in
`config/newHorizonsPerks.json`; generated module uses these canonical values.
Shared C++ validation/accessors and accepted action/packet integration are
implemented. Focused native coverage exercises accepted-action receipts,
allowance ordering, RNG isolation, refund pools, damage scaling and retirement
of the superseded generic effects. A separate paid Hex regression verifies
effect-packet serialization and detached AFTER_ATTACK damage with parent/sibling
state isolation; this is not world-save or battle-resume acceptance. Data/schema
tests check defaults, strict bounds, optional old-snapshot shape, ranks/IDs and
Sylvan isolation.

Focused data evidence: 49 Python registry/schema cases pass, and the generated
module freshness check passes. The first combined invocation had a test import-
path error; the proper existing `tools/tests` import path was used for the pass.
The combined Linux client and native tests build. A bounded ordinary Linux
battle executed a legal Charge Order and returned safely through creature
information and encyclopedia navigation. Successful hero horseshoe/eagle cues
were not captured; their rendered acceptance, Windows gameplay and broad
save/resume interactions remain pending.

Second-look cases: zero-cost paid admission, split charge pools and rounding,
negated multi-target casts, temporary Buffer expiry, independently admitted
follow-ups versus internal echoes, delayed source-owner changes, structural
damage and healing/restoration boundaries, Order companion-perk scaling and
Heroic Spirit no-recursion/save-load/AI continuation. Numerical tuning and broad
interaction certification remain deferred to their appropriate phase.

## Retired Gambler state — captured-policy admission

An explicit, validated captured `heroLuck` block selects the replacement policy,
including when all configured trigger chances are zero. In that context, reject
retired `gambler` or `gamblerAttackUsedThisRound` state before BattleStart install,
BattleAttack damage, or authoritative packet broadcast. Do not silently clear
contradictory state after applying it. Present malformed rule blocks are invalid,
not an older-policy marker. Other Sylvan fortune flags and raw codecs remain
unchanged.

Provisional compatibility boundary: an absent captured block retains historical
admission, including the old physical Gambler window. This is not a claim that
all historical fields are inert, or that ordinary mid-battle world saves are
supported. Chance defaults, wire version and the current installed configuration
must not silently reinterpret a captured battle's policy. Revisit absent-block
migration and unusual replay contexts separately.

Implementation: the shared BattleInfo validator is used at packet decoding,
game-state visitor admission and the authoritative send boundary. Focused tests
cover clean current admission, valid retired raw states, unchanged rejected
damage/state, and absent-block compatibility. The Linux client and native tests
build; all nine focused admission, raw-codec and current Erratic Mana controls
pass. These packet-boundary tests do not certify an authenticated network
session, ordinary world-save combat resumption or rendered presentation.

## Spell Lock — allegiance at cast time

Friendly/hostile preservation follows the target's current controller when the
accepted cast begins, matching existing Spell Lock AI evaluation. Capture this
polarity before cleansing: removing an effect such as Hypnotize must not change
the seal's polarity partway through that cast. The canonical friendly/enemy rule
does not separately specify controlled-target transitions; this interpretation
also follows the accepted current-controller treatment of Reality Warp.

Implementation: `scripts/spells/spellLock.lua` uses the existing
`currentControllerIsCaster` binding for both removal and preservation. Eleven
focused native Plague tests pass, including ordinary hostile preservation and
friendly cleansing. Hypnotize removal, subsequent controller changes and
Pandemonium debuff classification still need focused interaction coverage; those
ordinary casts do not verify these cases.

## Deep Flank — ordinary elemental primary shots (amends physical-only reading)

The earlier root-provisional physical-only interpretation is superseded on
second review: the canonical perk says friendly ranged attacks, without that
restriction. Ordinary elemental primary shots such as Magog and Lich therefore
receive half the Shroud flanking premium under the current captured policy.
Actual spell casts, indirect damage and magical collateral are not new
consumers; historical contexts retain their prior behavior. Both the eligibility
helper and attack payload currently exclude this principal path. A bounded
private correction with actual attack and detached-preview witnesses is assigned;
it is not yet implemented or verified. This does not override a settled user
decision. Wider control/collateral interactions deserve Phase2 review.

## Second Wind — charged creature abilities and direct spell output

Interpretation: a full additional Creature Activation renews per-activation
cast readiness, but never restores expended charges. Its direct damaging casts
use the same existing Second Wind coefficient as ordinary attacks; healing,
passive/indirect effects and hero spells retain their own output. Ordinary Order
continuation is not a fresh activation. This follows the canonical full-activation
and direct-damage wording without changing spell power or inventing a resource.

Implementation: shared spell mechanics and the live/detached activation boundary.
Nine focused native cases pass, including two actual paid casts, exact recipient,
spent-state denial and detached branch isolation. Adjacent Morale casts pass9/9.
Independent review, both-target linkage and binary privacy pass. Revisit unusual
charged abilities, simultaneous extra grants and fractional/cap composition in
Phase2. No canonical amendment, rendered or platform acceptance is implied.

## Morale extra activation — direct creature spell damage

Interpretation: the existing 75% direct-damage rule includes an ordinary
creature's explicitly activated damaging spell, as well as its physical or
magical ordinary attack. Retained Faerie Dragons can cast again during an earned
Morale activation if they have a charge remaining. Hero casts, passive effects,
indirect damage and healing do not inherit that creature's output reduction.

Rationale: the canonical rule specifies direct damage, not physical damage only.
Use the captured activation origin and percentage, without changing spell power,
charges, creature capabilities or the existing damage taxonomy. The proposed
shared spell-mechanics adjustment runs after target mitigation and existing
chain scaling, and is shared by damage execution and evaluation. Nine focused
cases pass the focused native-v3 gate, including actual paid casts and detached
evaluation. Independent source review, exact import verification, linkage and
binary privacy pass. Other Luck/Morale batch failures remain separately tracked;
this nine-case result does not certify those paths.

Physical attack output retains the existing shared pipeline's single final
floor; its independent fixture oracle does not scale an already-rounded ordinary
preview a second time. A baseline near-integral floating-point underflow remains
a separate integration finding, not permission to rewrite historical damage.

Second look in Phase2: existing damage caps precede chain scaling and active
spell output reduction. Check fractional/cap composition and custom damage scripts
separately; this change does not redesign those systems or establish rendered
acceptance. No canonical amendment is required for this interpretation.

## Discrete School-rank potency — preserve defining activation contracts

Chosen interpretation: strengthen Curse duration to 3/4/4/5 rounds before its
explicit extensions; scale Purify's authored SP/120 term through the existing
captured School coefficient. Explicitly retain rank-neutral Sanctuary, Dispel,
Teleport, Confusion, Berserk, Polymorph, Puppet Master and Reality Warp for the
individual reasons now integrated into the canonical document. Their already maximal scope,
fixed activation contracts or deliberately risky outcome do not admit a modest
universal numerical buff without changing their defining mechanics.

Rationale: fulfill meaningful authored progression where it exists, without
inventing automatic Mass effects, extra controlled activations, selective Warp
or improved Polymorph draws that duplicate existing perks. Purify's shared
count and Curse/Mass Curse duration now pass their focused native23/23 and15/15
gates respectively; this is not exhaustive cross-system acceptance.
Existing absent duration tables and older coefficient rules retain their saved
behavior. The amendment is integrated into the canonical Purify, Curse and
shared School-rank sections; source hash metadata is updated. Runtime acceptance
is established for those principal paths; this ledger is not an alternate specification.

Second look: whether Curse's middle-rank plateau remains desirable in Phase3,
and whether a future explicit non-scope benefit is worth authoring for each
named exception. Do not claim these exceptions are additional runtime buffs.

## Rapid Response and Seize Initiative — simultaneous pending promotions

Chosen interpretation: after earned immediate extras, Rapid Response's pending
waiting stack takes priority. Seize Initiative promotes its selected stack to
the next remaining friendly slot. Preserve enemy relative order, every existing
activation exactly once, the active front and both independent receipts.
Rationale: Rapid promises the delayed activation next; Seize promises the next
friendly activation without creating an extra. Composition uses the Rapid queue,
Seize's friendly-slot permutation, then re-pins the pending Rapid recipient.
Implementation: `CBattleInfoCallback::battleGetTurnOrder`; focused Seize fixtures
check independent priority/order/membership/truncation invariants. Source review
passes; the repaired native cases still require execution. Revisit simultaneous
promotions with deferred extra chains in Phase2, without duplicating activations.

## Pasis / Monere — current ordinary Wisp line, fresh DEFAULT only

Chosen interpretation: both obsolete Psychic specialties use the current Wisp /
Greater Wisp line only on fresh DEFAULT creation under their captured exact
two-row opt-in. Preserve all existing coefficients, native Psychic prototypes,
explicit PRESET development, saved/crossover history and absent legacy rules.
This repairs a functional specialty whose original ordinary recruitment line
was removed; it does not adopt the workbook's incompatible Magic Elemental or
Rebirth riders. No new mechanic, army, book, class or biography is invented.

Implementation proposal: strict hero-rule table, per-instance resolved target,
existing local four-marker producer, description and level-up refresh; raw and
captured old-format pre-prefix guards include actual campaign crossover pools.
Principal authored cases exercise fresh defaults, both real Wisp identities,
unrelated/Psychic controls, PRESET/legacy/absence, real level-up/cap, current
world/crossover replay, repeat init, prototype isolation and guarded save
envelopes. Cases are authored UNRUN; source review/native acceptance pending.

Second look: a deliberate future change of the ordinary Conflux line, custom
campaign identity transformations, exotic upgrade graphs and crossover travel
options that intentionally alter development. These do not justify retroactively
retargeting existing Psychic saves or changing the shared hero prototypes now.

## Nearest-only Town Portal — focused native validation accepted

The canonical nearest-controlled-town rule now has reviewed source in the shared
TownRelated resolver, TownPortalEffect and Nullkiller2 path nodes. Controlled is
provisionally caster-owned rather than allied; legacy allied selection remains
unchanged. Preserve the established squared planar distance and first-entry tie
order. An occupied nearest town blocks the cast rather than selecting a farther
town. The existing positive Movement admission minimum remains; successful casts
still consume all remaining Movement. Revisit allied/cross-layer interpretation
and low-Movement admission in Phase2. Twelve query/rank and two existing AI
controls pass14/14 on the exact linked candidate, with isolated-child proof.

The existing spell-description path supplies localized nearest-only/no-choice
wording; source verification is not rendered tooltip acceptance.

## Conditional displacement passives without an invented attacker

The authored Unyielding and Deep Bulwark rules are deterministic immunity to
physical forced displacement, but no normal Version1.0 physical push producer
is authored. A genuine generic server consumer can implement their conditional
semantics without changing creatures, Orders or magical Implosion. Pending
Changes records explicit cause taxonomy and the narrowly amended coverage gate:
real accepted/rejected movement with unchanged protected state and compatibility
proof, not an inert status. Review future physical producers and normal-game
presentation separately; no standard trigger or rendered acceptance is claimed.
Implementation and native evidence remain pending.

## Six specialty successors and two Navigation specialists

Pending Changes records functional existing-spell successors for Ash, Darkstorn,
Astral, Septienna, Melodia and Daremyth, with canonical damage15%/non-damage20%
component-only scaling. These intentionally change roles rather than pretending
retired spells still work; review Astral's friendly illusion instead of control,
Septienna's delayed contagion and shared supportive specialties during Phase2/3.
Real initial Hex/Plague snapshots and fractional Phantom computation require
producer changes, not only description/bonus markers. Sylvia/Voy need both a
Logistics parent and working core-effect specialty; Navigation's perk remains
unchanged. Exact prototype gates, map/legacy boundaries, real numerical casts/
movement and AI/save evidence are pending. Workbook-only riders remain excluded.
For Sylvia/Voy, fresh current PRESET heroes retain authored ranks/perks/books but
receive the opted-in exact specialty conversion independently of development;
an absent Logistics parent makes it inert. This matches other specialty families
without retroactively converting saved/crossover heroes. Review map authors'
expectations and verify both boundaries in focused controls before acceptance.

## Remaining Command, Diplomacy and Luck triggers

Crisis Command resolves after the whole accepted action so a multi-hit strike
cannot be interrupted halfway; its immediately usable defending-hero Order is
still required. Review simultaneous destroyed stacks, battle-ending damage and
earned-extra precedence. Legendary Reputation consumes on positive admission,
not merely an offer, preserving a monthly benefit when no troops joined; review
authored free exceptions and treasury/capacity changes during transfer.
Opportunist extends only the actor's own current attack activation, never a
reaction, avoiding unintended extra activations. Review multiple lucky hits,
ranged movement and remaining-Movement accounting. Pending Changes records
these provisional contracts; implementation and principal evidence are pending.

## Nonspecialist default inscriptions — twenty-two explicit successors

The audited current roster filters thirty-one retired default inscriptions;
separate Coronius, Inteus/Halon and six spell-specialty conversions leave these
twenty-two nonspecialist books. Pending Changes records the exact per-hero table,
chosen from existing preferred-School spells instead of global aliases or new
spell mechanics. This addresses empty default books without adopting unauthored
workbook army quantities, masteries or unique riders. Explicit map books and
captured legacy rules retain their existing behavior. Review Ayden's shift away
from scouting, Malekith's vitality trade and hero differentiation in Phase2/3.
Implementation, table-driven initialization, representative real casts, raw/
world/map/lobby guards and AI availability evidence remain pending.

## Seize Initiative — no implicit repeat for the active stack

Current detailed perk, Morale and initiative UI wording describe moving an
existing normal activation, not generating an extra. Excluding the current
stack prevents its unfinished activation from becoming an accidental repeat.
The first accepted Hero-paid Order is the once-combat trigger, even if the
eligible set is empty; delaying consumption would change the authored first-time
rule. Pending Changes records slot, enemy-order and immediate-extra precedence.
Private implementation and focused evidence are pending; review full extra,
control and forfeiture matrices in Phase2.

## Coronius — offensive successor without workbook-only riders

Holy Wrath is an existing Light offensive spell, matching Rampart's authored
preferred Nature/Light schools more closely than an unrelated friendly HP buff
would preserve Slayer's monster-hunter role. Use the established damage-specialty
15% SP-term rule, not non-damage20% scaling. Pending Changes explicitly records
the changed target identity and fresh-default opt-in boundaries. The workbook's
Masterful, tier and hero-level riders are not activated by this decision.
Implementation and focused default/paid/detached/old-format evidence are pending;
review broader targeting and differentiation during Phase2/3.

## Plaguebearer — preserve the existing contagious baseline

The producer currently selects one adjacent uninfected recipient each tick;
its spreadAttempts bookkeeping does not enforce a lifetime or chain-depth cap.
Interpret the authored normal limit as that existing per-tick single recipient,
with Plaguebearer permitting two. Introducing a new finite chain cap or extending
duration would change settled Plague instead of implementing the missing perk.
Pending Changes records captured inheritance and unchanged damage/duration.
Implementation and focused evidence are pending; review wider contagion and
Spellcraft-duration combinations in Phase2, not as a blocker to coverage.

## Havoc remaining perks — existing structural domain, no invented obstacle HP

Demolitionist and Meteorologist add same-category structural modifiers with
one rounding step; Cataclysm scales only the SP term once. Reuse the accepted
structural producer rather than inventing HP for binary scenery. Reserve fixed
ABSOLUTE landmarks provisionally, not as a claimed authored classification.
Ordinary magical cleanup is classified by creation spell, not obstacle trigger:
Tower's ability-created moat mines must survive despite sharing a mine trigger
with ordinary Land Mine. Pending Changes records these interpretations. Private
implementation, actual structural arithmetic and cleanup controls are pending;
review scenery classification and wider structural/perk combinations in Phase2.

## Diplomacy recruitment cohorts — bounded whole-stack provenance

Mercenary Captain tracks three accepted combat entries under the original
recruiter, pausing outside that army without granting a fresh allowance on
return or later perk acquisition. A bounded union preserves different origins
through legal merges, using the existing whole-stack Training convention rather
than inventing fractional creatures. Loyal Mercenaries removes faction-mixing
penalties only; it neither erases undead penalties nor fabricates unity bonuses.
Pending Changes records this interpretation; private implementation and focused
transfer/merge/save/entry evidence are pending. Revisit whole-stack amplification,
late acquisition and multi-origin mixed-army valuation during Phase2/3.

## Rapid Response and Heroic Spirit — activation ordering

Historical note: the Heroic Spirit retaliation interpretation below is superseded
by the canonical hero-Morale Order-only rework above. Rapid Response's separate
ordering interpretation is unchanged; retain this record as prior reasoning,
not authority to grant the removed Heroic Spirit retaliation.

Rapid Response defers until earned immediate extras finish, preserving their
action economy without an interruption/resumption subsystem. It then moves one
existing delayed activation, with no use consumed for an invalid recipient.
Heroic Spirit's additional retaliation survives the granting Morale extra and
ends when the following genuine activation begins; otherwise the immediate
Morale activation could erase it before it has a meaningful opportunity to work.
Pending Changes records these provisional choices. Production and focused
evidence are pending. Review extra chains, hostile control, forfeiture and round
reset composition during Phase2 rather than stalling missing implementation.

## Inteus and Halon — remaining default inscription gaps

Inteus uses Crusade! as the existing general offensive-enchantment successor,
with SP-component-only specialty scaling. Its broader target shape is recorded
for Phase2/3 differentiation review, not hidden behind a new Masterful spell.
Halon gains only Guardian Spirit's default inscription; Metamagic Adept remains
his sole specialty. Pending Changes records opt-in/default/map/legacy boundaries.
Implementation and focused evidence are pending; Coronius is not covered by this
decision and may not inherit a replacement silently.

## Iron Will and Reactive Weave — resume previously held implementation

Iron Will reissue replaces the same Order's recipient snapshot, following the
nonstacking lifecycle already used by Divine Discipline; it never revives spent
benefits. Carry ends after the next genuine completed or forfeited activation,
not a Wait or continuation. Reactive Weave retains separate expiry from ordinary
readiness, chooses the stronger eligible value and consumes both on the next
accepted Order. This avoids reaction-based extension of stronger readiness and
unexpected additive multipliers while honoring the next-Order wording. Pending
Changes records these provisional contracts. Production locations and focused
evidence are pending private implementation. Revisit Time Stop/cleansing/order
reissue and counterspell/resistance/recipient classification in Phase2.

## Eight retired-development starts — legal explicit replacements

The audit identifies eight current Might defaults whose retired generic choices
normalize away, leaving only their faction Skill. Pending Changes authors exact
legal parent/perk replacements while preserving class and other hero content.
Retain Advanced parent ranks rather than lowering them merely to fill a Basic
perk; the existing exceptional progression rule permits the empty Advanced slot.
Torosar's War Machines/Master Gunner choice follows his retained Ballista role;
the other choices preserve the closest current travel, learning or deployment
role. Apply only fresh default development through captured opt-in profiles,
not unconditional prototype edits or global parent-grant migration. Implementation
and focused native evidence remain pending. Review differentiation, removed
second-choice compensation and early development pacing during Phase2/3.

## Loynis and Zubin — current enchantments replace removed defaults

Provisional identities: Loynis Prayer becomes Crusade!, explicitly described as
the current army-wide Prayer successor; Zubin Precision becomes Focus Magic,
the current friendly-shooter enchantment. Each specialty scales only genuine
Spell Power-derived numerical components by20%, preserving fixed values, caps,
duration, marks and ordinary paid resolution. Pending Changes contains the
bounded profiles contract. Default-start, map/legacy, paid/detached and saved
admission evidence remains to be implemented; no coverage credit yet.
Review starting-tier strength and broader enchantment/perk interactions later;
do not reuse these decisions to activate the full hero-workbook proposals.

## Halon — Metamagic capacity is not mastery

Provisional clarification: honor the shipped Metamagic Adept's additional use
at Expert as well as Basic/Advanced, yielding2/3/4. Existing effective-use
clamping silently removes that specialty at Expert. Separate actual mastery
from use capacity; neither surplus uses nor a standalone bonus unlock Grand or
grant an unlearned Skill. Pending Changes records the exact bounded contract.
Implementation, version admission and actual native evidence remain pending.
Review legacy bonus-only profiles, live rank changes, capacity reductions and
Grand sequence accounting in Phase2; retain consume-on-accepted-follow-up rules.

## War Machines — meaningful selection, outer adjacency and surviving repair

Basic War Machines already grants manual Catapult control, so Precision
Bombardment cannot be implemented as a redundant unlock. Provisionally lock
the selected legal part: preserve accuracy/damage rolls, but misses do not
redirect and destroyed selected parts do not provoke silent retargeting.
Breachmaker uses explicit outer-line adjacency excluding the keep, one
floor(excess/2) applied-final-damage carry, and lowest positive neighboring HP
with stable ties. Do not infer geometry from enum order or chain the overflow.
Field Workshop repairs surviving damaged allied machines/defended structures
only, using full shared Tent output before target missing-HP caps. This avoids
inventing resurrection, rebuilding, blocked-breach placement or turret creation.
Pending Changes contains the complete implementation contract.

Review selected-shot utility/AI policy, central-keep topology, repeated shots,
primary tower exclusion, repair/rebuilding distinction and opened-gate visual
state in Phase2/3. Ordinary control, target legality, final HP, defensive-shooter
removal and actual manual/AI eligibility are Phase1 requirements, not deferred
polish. No source implementation or native acceptance is implied by this audit.

## Arcane Memory — captured ordinary acquisition remains controlling

The existing completion/provenance producer is suitable for shipped activation.
Choose ordinary acquisition eligibility rather than inventing a scroll bypass
for paid Adventure Guild unlocks. Current Fly scroll success spends its normal
resources without granting a permanent unlock; captured older rules may allow
learning. Town Portal's actual Guild teaching remains that Guild's independent
reward. Capture exact scroll source before effects so destination learning never
misattributes provenance. Book/tome priority, reusable scrolls, rejected effects,
cancellation and pending queries retain their ordinary rules. Pending Changes
records the provisional scope. Eleven current authored cases are source-clear,
not yet integrated/native accepted. Review Adventure acquisition exceptions,
accepted negated combat casts and future six-school Tomes in Phase2; this is not
authorization to add missing Tome content or alter existing unlock prices.

## Recruitment training — whole resulting stacks

Status: production producers and typed receipts are integrated; focused v6
principal128/128 and adjacent15/15 pass with zero failures/errors/skips.
Training79803 passes1/1 and setup92086 passes29/29. Fresh v5 completes127/128;
its unrelated next-round carried-Order rejection is repaired for the v6 gate.
Accepted perk coverage increases with the full gate; playable acceptance remains separate.

Use whole-stack training after genuine direct recruitment, not mixed individual
cohorts. This matches existing stack-level Attack/Morale/Initiative and avoids
introducing weighted statistics. Same-hero split/merge/reorder preserves compact
receipts without additive bonuses. Cross-army movement permanently clears Field
Instructor and Reinforcement Drill on moved troops; Drill Sergeant travels with
them because only the other perks require combat/residence under the recruiter.
Do not treat temporary setArmy(nullptr) detachment as an army change.

Use recruitment day through day+6 for the seven-day Drill window. Consume at
first actual combat even if immune/capped. Field Instructor activates only after
the first completed battle for surviving original strategic troops, including
ordinary retained retreat/surrender results. Reinforcement Drill picks the
lowest eligible original slot at simultaneous accepted battle entry, includes
Champions, spends once per absolute week, and uses flat round1 Initiative.
Paid and genuinely free external recruitment qualify; generic addToSlot does
not establish recruitment provenance. After the required full gate, canonical
perk wording receives this amendment; second-look interactions remain here.

The initialization repair re-stages only exact typed Training markers before
canonical export so the accepted bonus is installed once, preserving descriptor
identity, duration, setup preview and serialized replay. Actual combat starts
once; round1 grants and subsequent expiry remain asserted. Strategic Attack
controls distinguish the existing native-terrain battle bonus from training.

Review aggregate-stack benefit from a small recruited addition, split/merge
training propagation, transfer/return permanence, seven-day endpoint, simultaneous
entry tie-break and retained losing armies in Phase2/3. Minimum AI must execute
the real recruitment producers and consume shared combat stats; deeper strategic
training-aware exchange valuation is deferred, not claimed implemented. Required
feedback must distinguish pending first combat, deadline and active training.
No native acceptance or coverage increase follows from this contract alone.

## Merist and Labetha — provisional defensive replacement assignment

Merist receives ordinary Hydra's Vitality, matching Nature affinity and living
Fortress troops. Labetha receives Guardian Spirit: Hydra's Vitality and
Regeneration exclude her nonliving Elementals, so a physical damage buffer
better preserves the original general defensive role despite lower Light
affinity. Known starting inscriptions retain their existing rank exemption;
do not change starting Skills. +20% applies only to SP-derived terms, preserving
fixed values, caps, durations and other perk staging. Record these authored
identities in Pending Changes until focused implementation is accepted. Review
Labetha's School affinity and specialist differentiation in Phase2/3. No source,
native or playable acceptance is inferred from this assignment.

## Aenain — provisional replacement assignment

Choose existing Frailty for Aenain's removed Disrupting Ray: both target enemy
Defense, unlike the workbook's undefined Earthquake scaling. Reuse +20% only
to the SP-derived Defense-loss term with existing fixed term and caps unchanged.
Preserve other starts, map spellbooks and legacy contexts. Pending Changes
records the authored amendment; private implementation and principal paid-cast,
default-versus-map, legacy and save evidence are still required. Revisit the
shared spell identity across several specialists and its hero differentiation
in Phase2/3; this does not authorize wholesale workbook adoption.

## Four-perk batch — principal native validation passed

Magnate records the most recent owned-at-entry town visit in the previous
absolute week. At week start it snapshots a seven-day town award; losing the
town pauses payment, and same-owner reacquisition resumes the original clock.
Losing the granting hero does not cancel the town award. No fallback is invented
when the most recently visited town has been lost. Amounts aggregate before the
ordinary income handicap. Review unusual calendar lengths and the current-vs-
next-day income tooltip boundary in Phase2.

Academic Study consumes each hero's first town arrival even before perk
acquisition or before a Guild exists; later construction cannot create a repeat
reward. Ownership is not an authored restriction. The hero-only visitor marker
precedes XP and does not alter player/team or building-visit history. Mentor's
existing meeting snapshot is retained. Review composed level-up/building/Scholar
queries in Phase2.

Elemental Memory captures the destroyed stack's positive net ordinary Morale and
Luck independently, then adds only the missing contribution after newborn
positive inheritance. Newborn-specific negative effects and elemental Morale
immunity remain effective. Ordinary one-round bonus expiry and Time Stop duration
handling are retained; detached copies eagerly snapshot their own receipt.
Review uncommon aura/controller and paused-duration interactions in Phase2.

Veteran Cohesion uses battle-start maximum aggregate HP and a strict below-half
threshold after actual HP loss. Its once-combat personal receipt survives healing
and restoration; ordinary immunity and caps still govern its +2 Morale. Time Stop
or fully absorbed damage cannot earn it. Review temporary HP, polymorph and
controller transitions in Phase2. Sources are the shared Discipline/unit-state
and Rebirth paths, town/hero visitor helpers, NewTurnProcessor and existing
netpack serialization. These four implementations are source-reviewed and
accepted at native85299:36/36 principal cases pass after linked90928, zero
failures/errors/disabled/skips. Shared68621 passes33/33 on the same pair.
These interpretations remain reviewable; broader interactions above are Phase2.

## Prepared private candidates — not accepted implementation coverage

Accepted batch status: Royal Standard, Deep Flank, Encircled Doom, Vanish,
Historian, Scholar and Archivist now pass their principal gates in88843:79/79,
with adjacent3070:15/15. Haste and Thant specialty paths pass in the same batch.
The implementation interpretations below remain reviewable Phase2 items;
earlier authored/unrun labels are historical, not the current acceptance state.
Shared Purpose, both Sage perks and four Frailty replacements pass36/36 exact
native70790 and12/12 adjacent61175 after linked38972, with independent source
review. Perks279/310 are accepted; Sage/Frailty amendments are integrated into
the canonical document. Phoenix Spark and Recruiter's Contacts remain private
source-reviewed candidates without native credit.

Recruiter's Contacts: provisionally an empty pool means an individual external
dwelling row, not every row simultaneously. Replenish only the first empty row
with positive ordinary weekly growth in stable stored order; preserve base and
upgrade sharing. Qualification uses ownership at entry, before the ordinary
capture path. Only a successful positive stock grant spends the hero's weekly
use; zero growth and failed/stale receipts do not. Reuse the external dwelling's
own percent/flat growth formula, not town/Castle production. Private implementation
is underway, with no activation/native evidence. Review multirow selection and
captured versus previously owned dwellings in Phase2.

Phoenix Spark: provisionally its explicit25% battle-start maximum aggregate HP
replaces the ordinary Rebirth/Greater Essence HP percentage, rather than adding
Greater Essence's15 percentage points. Capture the first eligible friendly
Champion death and the once-combat use before summon creation; a failed legal
placement does not shift 'first' to a later Champion or recursively retry.
Use authoritative category and existing Rebirth eligibility, temporary summon,
placement and lifecycle infrastructure. Related rebirth traits require explicit
scope review rather than assuming Phoenix is an ordinary Elite Elemental.
Private implementation is assigned; no activation or focused native evidence.
Revisit failed-placement use and other Rebirth perk composition in Phase2.

Both Sage perks: interpret the Guild's available schools and built levels as
its eligible catalog, not only already displayed spells. Ordinary Guild learning
already teaches all legally learnable displayed spells, so the latter would make
Learning Sage ineffective. Respect saved per-level school labels and map bans.
Wisdom reveals a previously undisplayed eligible spell globally; Learning learns
the highest eligible unknown spell personally. Canonical scoped spellbook order
breaks ties; provisionally Wisdom also chooses the highest eligible level. A
hero's first built-Guild visit consumes its receipt even before perk/book
acquisition; arriving with no Guild does not. Separate revealed rows preserve
ordinary fixed slots and research. Existing-position overflow must remain
accessible through a native component dialog, not disappear or overrun layout.
Principal Sage10 passes in native70790; both perks are active and the authored
clarification is integrated. Review multiple holders, later Guild construction,
school labels, rendered overflow and book acquisition timing in Phase2.

Vanish and Encircled Doom: Vanish earns a move-only half-Speed budget only from
the acting stack's qualifying primary physical flanking kill. Combine it with
existing Pursuit by maximum, not addition; no extra attack/activation is granted.
Encircled Doom adds10 percentage points per additional distinct contact side,
up to50, only to a qualifying physical melee flank. Existing contact geometry
and controller filters apply. Review transient post-attack Speed forecasting and
broader collateral/temporary-form cases in Phase2. These integrated candidates
still await their combined native gate.

Scholar: choose both reciprocal teachings before either mutation, with scoped
canonical identity ties and a symmetric pair/week receipt. No eligible teaching
does not consume the use. Ordinary book/school/rank/banned-spell rules still
apply. Haste specialists keep fixed Speed unchanged; the canonical non-damage
SP-component enhancement applies to duration. Thant's replacement applies it
only to Re-animate restoration, not its fixed220 component. Neither change
authorizes other starting-profile replacements. Their combined native gate is
pending; review unusual meeting, duration and restoration compositions in Phase2.

Royal Standard: capture only the original recipients of an actually selected
Divine Mandate Order follow-up. Its negative-Morale floor lasts through that
Order's scheduled round expiry, even when a Charge is spent, Protect is broken
or Second Wind's activation finishes. Current hostile control suspends the
floor; friendly control restores it within the original lifetime. This differs
from Commanding Presence's explicitly settled benefit-consumption lifetime.
Review Second Wind and future Divine Discipline extensions in Phase2. The
shared morale callback and captured Order recipients implement the private
candidate; eight cases are authored and source review is clear, not executed.

Deep Flank: interpret 'currently attacked from at least two distinct melee
sides' as present friendly melee-capable contact geometry, not historical hit
counts. Union the existing directional footprint masks; repeated contacts on
one direction do not add sides, while a double-wide footprint can contact two
distinct sides. Apply half the rank's base Shroud flanking damage bonus only
to the qualifying physical ranged attack, with fractional precision preserved;
do not halve unrelated Backstab or Ambusher bonuses. Existing Formation Fighting
protection remains. Shared damage/callback and Lua context implement the private
candidate. Eight cases are authored and source review is clear, not executed.
Review incapacitated contacts, walls/terrain and wider double-wide geometry in
Phase2; this is not a completed AI-choice or rendered acceptance claim.

Historian: classify the shipped Chest XP choice and Tree's XP reward explicitly.
Preserve Tree's existing whole next-level threshold delta, not remaining XP to
the next level. Only New Horizons level-derived rewards gain the ordinary
Learning calculation and Historian's additive50%; legacy Tree grants/previews
and AI remain unchanged. Grant, preview and AI share the same gate. Saved
unclassified rewards are not silently migrated. Seven cases are authored and
repaired v2 source review is clear; actual native gates remain pending. Review
unusual mixed reward components and saved classification migration in Phase2.


## Swift Rebirth — normal birth-round activation

Advance the reborn stack's one normal activation after the acting stack's
current activation, preserving any already-earned immediate Morale first.
Simultaneous spawn IDs order ascending. WAIT delays the same normal activation;
it does not create a second one. Birth-round extra activations are excluded,
while genuine continuations remain legal. Frozen/Time Stop forfeiture spends
the normal slot. Later rounds return to ordinary scheduling. Reasoning: the
authored immediate turn is normal initiative promotion, not an extra-action
generator. Shared callback/Swift lifecycle, server flow and detached AI consume
the same typed receipt. Ten principal native cases pass after linked82853, including
incapacitation during an accepted action and nested detached replacement.
Review lazy child materialization before first access and approximate projected
mid-exchange rebirth valuation in Phase2; do not claim exhaustive AI forecasting.

## Prospector — eligible resources and capture timing

The first visit to a mine already owned by this hero's player grants Wood/Ore+2
or one of Mercury/Sulfur/Crystal/Gems+1. Gold is neither common nor rare and
does not pay or consume the weekly use. Capturing a mine is not an owned-at-entry
visit; Land Surveyor retains its separate capture reward. Each hero has an
independent absolute-week receipt published before resources are granted.
Reasoning: use the authored common/rare distinction and avoid consuming a
reward on an ineligible resource. The trigger is CGMine::onHeroVisit; tooltip
and receipt serialization are shared. Nine principal and five minimum AI cases
pass after linked82853. Review custom resource classification, multiple-goal AI
receipt forecasting and capture-followed-by-visit
interaction in Phase2. This interpretation does not amend the canonical text.

## Merchant Prince — resident scope and virtual Marketplaces

An owned visiting or garrison hero qualifies while linked to this town. Two
holders grant +2 virtual Marketplaces once, not +4. Existing supported exchange
modes consuming Marketplace efficiency benefit, retaining their ordinary caps;
fixed transfers and unsupported modes do not change. A real Marketplace remains
required. No kingdom-wide count is mutated. Reasoning: the text grants one town
modifier, not a kingdom-wide or per-holder count. Implementation is
CGTownInstance::getMarketEfficiency; eight principal and three Resource Broker
native cases pass after linked77224. Review future independent holder stacking
or new exchange modes if authored. These scope choices are provisional.

## Steward — resident contribution and day boundary

Provisionally both owned, reciprocally linked visiting and garrison heroes
qualify; each distinct selected holder contributes250 Gold, two contribute500.
Unlike Merchant Prince's town-single modifier, Steward describes a hero's
individual end-day condition. Sample residence at the existing authoritative
day transition and apply the town's ordinary handicap once to combined income;
do not introduce a separate persistent end-turn receipt or treasury grant.
Implementation candidate: CGTownInstance::dailyIncome consumed by
NewTurnProcessor, existing town income label and passive right-click breakdown.
Nine transition/handicap/ownership native cases pass after linked82853.
Revisit captures or residence changes between individual player
turns, asynchronous play and any explicitly authored future town-wide cap.
The choice is provisional and does not amend the canonical wording.

## First Blood and Slayer — independent extra increments

Provisionally each perk adds one extra ordinary Bloodrage increment to its
qualifying destruction. The first Elite/Champion death therefore grants three
increments when both apply, not four multiplicatively or two redundantly.
Both holders observe the same first qualifying death before its shared combat
receipt is spent. A capped grant still spends First Blood; resurrection never
restores it. Slayer uses the saved Core/Elite/Champion category, not numeric tier.
Existing summoned/clone exclusions and Bloodrage caps remain unchanged.
This interpretation remains reviewable; it does not amend the canonical design.

## Eagle Eye retained combat participants

The canonical wording says after combat, not only after victory. Provisionally
grant selected Eagle Eye to participants whose heroes remain available:ordinary
winners and retained retreat/surrender heroes. Deleted defeated heroes gain
nothing. Draw retention follows the existing hero-retreat setting; troopless
winners follow the existing automatic retreat. Introduce no new survival policy.
Capture the highest eligible
Level1–3 enemy HERO_SPELL before battle cleanup, keeping earliest-cast ties,
then use existing learning receipts before map removal/tavern preservation.
Creature casts do not enter that history. Saved roster, school eligibility,
already-known exclusion and selected current-v3 rules remain authoritative.
The unselected/legacy bonus path remains unchanged. This scope is reviewable,
not a claim that every retention/save interaction is certified. All11 principal
cases pass after linked11731, including actual winner/retreat/surrender, creature
history exclusion and defeated hero removal. Broader retained draw/troopless
winner, tavern rerecruitment and save restoration remain Phase2.

## Lucky Recovery composition and attack-local Avatar admission

Historical note: generic Lucky Recovery is now Lucky Day, with the same saved ID
but no generic survivor-healing effect. The generic-plus-Sylvan20% composition
below is superseded. Sylvan's separate10% survivor recovery and the independent
Avatar admission interpretation remain unchanged; prior tests are historical
evidence, not acceptance of the removed generic effect.

Generic and Sylvan Lucky Recovery each recover10% of actual non-overkill melee
damage on positive Luck, healing only survivors. If both independent perks are
selected, provisionally add their authored fractions and round down once:
20% total, not two independently rounded ticks. Neither effect resurrects a
dead attacker or applies to ranged/record-only forecasts. Review composition
and delayed casualty interactions in Phase2. Focused live/detached cases are
source/native accepted within the30-case gate after linked71404, zero skips.

Avatar of Rage tests the attack's captured effective Bloodrage against that
side's captured maximum. Blood Scent's existing attack-local increase may meet
that threshold for this attack only, without changing stored Rage. Its25%
intrinsic Creature Defense ignore adds through existing physical damage
channels and their cap; it never converts Hero Primary Attributes into
Creature Attack/Defense. Expert perk cardinality does not permit simultaneously
selecting Endless Bloodshed; extended-cap threshold controls are therefore
pure captured-rule fixtures, not illegally selected hero builds.

Veiled Movement follows the existing current-controller Ghost Walk capability
and suppresses only explicitly movement-triggered Overwatch reactions. Unlike
Night Prowler's separate crossing requirement, its wording does not require
actually crossing another creature. Ordinary shooting is unchanged.
The current-controller fixture preserves the legacy enemy selector's original-
target-side semantics. It proves perk ownership, not correct current-controller
hostility; that existing allegiance interaction remains a Phase2 finding.

## Frozen normal-activation timing

The supplied Conflux rebalance requires a Frozen stack's next normal Creature
Activation to be forfeited, followed immediately by thaw. The freezing attack
and a subsequent Shatter attack never permit retaliation, including after
Frozen is removed by that Shatter. Cleansing before the normal slot permits
normal action; nothing restores an already forfeited activation.

Provisional interpretation: Morale, Second Wind and other extra activations
cannot move, attack or use abilities while Frozen, but do not consume its
normal-slot lifetime or thaw it. Preserve the recipient's successful-application
round stamp after every removal, so cleansing or Shatter cannot enable another
successful freeze that round. The required20% trigger belongs to a resolved
stack melee attack/retaliation, not individual creatures; its roll is distinct
from whether the surviving recipient remains eligible for application.
Review bonus-slot scheduling and multi-strike attacks explicitly during focused
acceptance; do not substitute existing Stone Gaze/Blind activation semantics.
Status:source/native accepted in47/47 focused cases after linked6035; rendered
verification remains pending and is not inferred from native success.

## Reality Warp mixed-pair resistance and confirmation

Status:source/native accepted in73/73 principal cases after linked6035;
rendered verification remains separate.

The canonical total exchange does not specify how a mixed friendly/enemy cast
handles Resistance or Magic Mirror. Selected hostile endpoints, determined by
their current controller, receive the existing cached Resistance resolution;
friendly endpoints do not resist. If any selected hostile endpoint resists,
neither endpoint exchanges effects. A reciprocal exchange is not redirected
onto a third stack by the single-target Magic Mirror path. Recipient eligibility
for each captured effect remains deterministic and does not roll Resistance.
This preserves the exchange's indivisibility without making hostile casting
ignore defenses. Revisit two hostile Reality Breaker targets, Twist of Fate,
Magic Mirror and Counterspell composition during Phase2.

The human interface selects two stacks and opens one centered, scrollable
complete move/stay preview. Only Confirm casts; changed captured state requires
fresh review. This adds no separate activation ability. AI initially values
the next reachable attack horizon using actual detached before/after states;
long-horizon regeneration and delayed statuses deserve a second look.
Implementation: BattleSpellMechanics, RealityWarpEffect, the shared collector,
BattleActionsController and BattleAI. No rendered acceptance is implied.

## Fate Dealer spill candidate draws

Status: provisional implementation; focused native22/22 passes after linked46720.

Generate two independent uniform legal spill candidates with replacement,
consistent with Shapeshifter. If exactly one is hostile to the caster's current
ownership, choose it; otherwise choose fairly between the draws, including
duplicates. Preserve the existing half-primary-actual-HP-loss amount, then apply
the selected recipient's own defenses. Do not reroll an immune or resistant
recipient. Review replacement versus distinct candidates later; the canonical
rule already specifies the hostile preference and random same-side choice.

Production ownership: scripts/spells/damage.lua collateral selection and
AI/BattleAI/SpellTargetsEvaluator.cpp expectation. With H hostile and F friendly
candidates and N=H+F, each hostile has probability(H+2F)/N² and each friendly
F/N²; without the perk each is1/N. Focused evidence must include duplicate and
same-side draws, controlled ownership, recipient defenses without reroll, exact
weighted AI expectation without live RNG, and paid AI with Orders competing.
Accepted evidence: runtime17/17 in5.471s and AI5/5 in1.888s, zero skips,
failures or errors. Receipt: build/nh-fate-dealer-native.oy8R/receipt.md. Runtime
and AI explicitly compare current combat controller against caster ownership;
ordinary ownerMatches retains its existing initial-side semantics. Review wider
control/casualty interactions in Phase2 and replacement versus distinct draws
in a later gameplay review. Source/native acceptance is not rendered delivery.

## Polymorph footprint restoration and Phantom body

Status: provisional implementation; principal25/25 and foundations17/17 pass.

Final exact-pair native evidence: linked64681, principal24.210s and
foundations1.831s, zero failures/errors/skips. Receipt:
build/nh-polymorph-native.WSrQen7z/receipt.md. Paid registered AI casting with
Orders and active Shapeshifter passes. Runtime, AI, status and protocol guards
are accepted; older preparatory notes below preserve the selection rationale.
No binary form snapshot, bespoke artwork or rendered delivery claim.

Use the approved nearest-legal-position policy for the original footprint on
expiry or Dispel. If no legal original footprint exists anywhere, retain the
current form, HP and position and retry safe restoration rather than overlap,
kill or heal creatures. The resulting delayed restoration is an exceptional
duration rule requiring a second look. Its Pending Changes amendment is now
integrated into canonical Polymorph; status must expose the exception rather
than silently present it as ordinary expiry.

For Phantom Army, preserve the copied offensive body's aggregate creature HP
when transforming; retain its separate current/initial Integrity, ordinary
expiry and Integrity damage behavior. Converting offensive count from Integrity
would silently alter Phantom Army's existing model. Review whether Phantom
forms should intentionally use a different policy. Candidate production seams:
CUnitState, battle/BattleForm and spells/effects/BattleForm. No native proof yet.

Read-only architecture review identifies activation prerequisites: preserve the
logical form marker while restoration is pending; use fresh accessibility in
deterministic unit-ID order after round hooks; validate converted Phantom counts
without confusing body HP with Integrity; clear form provenance before lethal
Integrity resets health; refresh client creature forms after Dispel; and use
separate damage caches for distinct form outcomes. Retain the existing explicit
binary-snapshot rejection until a full form-state save contract is implemented.
These are required implementation work, not accepted coverage.

Shapeshifter uses two independent uniform same-tier draws with replacement;
compare whole transformed-stack Army Value using the exact HP-converted count,
not a single creature's value. Resolve ties by canonical creature order. This
interpretation and Phantom body policy are integrated into canonical wording
under the user's provisional-decision authorization. Review whole-stack value
versus per-creature value and repeated-draw policy later. Runtime and weighted
detached forecasts must agree; no native proof yet.

## Reality Warp transferred-effect beneficiaries

Status: provisional interpretation; next-slice preparation, not activated.

Focus Magic's beneficiary follows the receiving stack's controlling side;
Arcane Breach's beneficiary is the side opposing its new recipient. Preserve
original caster attribution, captured strength/perk parameters and remaining
duration. Keep unknown legacy hostility provenance rather than inventing a
caster. This makes transferred effects function in their new location without
recasting them at the Warp caster's power. Review controlled-recipient ownership
changes after transfer. Confusion pending state travels with its magical effect,
but previous resolved-behavior history stays with the recipient. Orders,
transformations and typed non-transferable effects stay excluded. Candidate
production seam: NewHorizonsRealityWarp's shared validated reciprocal plan and
atomic packet application. No native proof yet.

## Mire Shaper additional Bog Ambush patch

Status: provisional implementation; focused native acceptance passed10/10.

Calculate the ordinary School/Spellcraft/Warcasting-scaled patch count with its
five-patch cap, then add the selected perk's one additional patch. The perk thus
permits three to six patches rather than disappearing at high Spell Power.
This preserves the spell's normal cap and the perk's explicit extra patch.
Review whether a future design should impose a global five-patch cap instead.
Implemented shared seam: NewHorizonsMagic::quicksandPatchCount in
lib/spells/NewHorizonsMagic.cpp; client, Lua authority and AI consume that count.
Focused count, paid placement and actual AI-submission fixtures are present.
Final native10/10 passes, zero skips, in2.587s after fixture-only relink74808.
Receipt: build/nh-mire-miracle-native.lBbjTbsn/receipt.md. No rendered claim.

## Miracle Worker casualty restoration

Status: provisional implementation; focused native acceptance passed14/14.

Preserve healing allocated to wounded surviving creatures, then increase the
remaining casualty-restoration HP pool by25%, flooring once. A fully destroyed
eligible stack receives125% of the whole pool, subject to the existing original
count cap. Wound-only healing is unchanged. This follows the perk's "casualties"
wording without multiplying already-rounded creature counts or granting extra
wound healing. Review whether the intended design should instead boost the
entire restoration pool. Implemented seam: scripts/spells/heal.lua amount
calculation used by legality, forecast and execution. Fourteen focused native
fixtures pass14/14, zero skips, in5.580s on the final exact pair, including
paid AI with Orders. Receipt: build/nh-mire-miracle-native.lBbjTbsn/receipt.md.
Broader casualty/capacity compositions remain Phase2; no rendered claim.

The user authorizes reasoned provisional gameplay judgments when rules or
interactions are unclear. Implement and verify the choice, rather than stopping
the implementation loop for every ambiguity. This ledger identifies situations
worth a second look; it does not replace the canonical New Horizons.md.

For each material choice record the specification clause, behavior, rationale,
source paths, focused evidence and review scenarios. Preserve resolved entries
and their eventual disposition. Actual specification amendments belong in
Pending Changes until integrated. Explicit settled user decisions take priority.

## Terrain Rebirth selection and modifier composition

Status: provisional interpretation; focused native acceptance passed25/25.

The authored terrain table names one Elemental per context. Adaptive Element
therefore selects that legal singleton; Perfect Convergence forces the same
primary result. Do not invent secondary thematic types to manufacture a
difference. Review whether future design should give Adaptive a broader pool.

Elemental Attunement applies to both first and Chain appearances because it
describes a reborn Elemental without a generation exclusion. Perfect's "every
Rebirth" changes Chain's default random selection too. Chain keeps its fixed
25% immutable original-HP basis and combat-use restriction; apply the new
appearance's matching20% bonus once after that base calculation.

Production: lib/battle/NewHorizonsElementalRebirth.cpp,
server/CGameHandler.cpp, AI/BattleAI/StackWithBonuses.cpp. Review successive
appearance HP composition, terrain-selection usefulness, and saved-battle
continuation. Focused native results belong in the functional completion matrix.

## First-round Elemental Conjurer Initiative

Status: provisional lifecycle implementation; normal-round native acceptance passed.

Use the existing one-turn spell bonus for +2 Initiative through the summon
round, preserving normal bonus transport and caching. Review Time Stop and
Spell Lock interaction: those systems may freeze generic duration beyond the
literal first battlefield round. Production: scripts/spells/elementalConvergence.lua.
Normal next-round expiration is included in the focused native fixture.

## Overwatch range and voluntary movement

Status: provisional interpretation; principal native acceptance passed29/29.

Use the ordinary unpenalized ranged distance (10 hexes), capped by an explicit
limited shooting range. No-distance-penalty abilities do not make the trigger
radius infinite. Require an outside-to-inside entry, not arbitrary movement
already inside range. This makes "moves into its range" an observable transition
instead of treating every legal ranged target as already inside an infinite range.

Only accepted ordinary voluntary movement triggers. Flying checks its landing;
Teleport/Blink, forced movement, deployment, gating and return-after-strike do
not. Simultaneous eligible shooters resolve in stable unit-ID order. Review
whether the intended range should instead be a distinct reaction radius, whether
inside-range movement should trigger, and competing-reactor order. Principal
implementation: NewHorizonsBattlecraft, CBattleInfoCallback, CUnitState and
BattleActionProcessor. Principal29/29 native cases pass; occupied-transit AI/UI
forecast parity and advanced reaction effects remain Phase2 review items.

Pass-through movement across an occupied ground hex cannot publish an overlapping
stack position. Reactions therefore occur at the next legal inside-range endpoint;
if transit leaves range before any such endpoint, no shot triggers. Review this
bounded Ghost Walk/Passing Lines exception without introducing illegal state.

Use the existing counter-reaction recursion guard so Counterfire does not answer
an Overwatch reaction. The existing next-attack Battlecraft Wait bonus applies
to and is consumed by that shot. Review these reaction-chain/next-attack semantics
in Phase2; do not duplicate attack-resolution machinery merely for this perk.

## Rapid Embarkation and Navigation

Status: provisional interpretation; principal native acceptance passed29/29.

Rapid Embarkation's explicit "costs only10% of maximum daily Movement" is a
final boarding/disembarking cost, replacing the ordinary cost rather than an
intermediate amount that Navigation halves again. This honors the named fixed
cost without manufacturing a5% figure absent from its wording. Review whether
future design should intentionally allow multiplicative stacking. Shared movement,
authoritative boarding and pathfinder parity pass in the focused29/29 gate;
custom-vehicle composition remains unverified.

Preserve the explicit Lighthouse/free-boarding exception ahead of Rapid
Embarkation: no embarkation penalty remains no penalty, with the existing ordinary
step-cost treatment. The perk must not increase the cost of an already-free action.

## Nature's Wrath conduction and range

Status: provisional interpretation; Nature's Wrath focused acceptance passed16/16.

The authored nearest-unvisited route has no numerical chaining radius. Use the
whole playable battlefield and deterministic unit-ID ties, without changing
generic Chain Lightning. Healthy friends conduct with zero healing; living
immune/invincible stacks conduct but receive no effect. Dead, ghost, turret,
off-board and Time Stopped stacks are not route recipients. Resolve the route
before defenses: a resisted hop still counts and attenuates but does not stop
the current. These choices follow the described living current rather than
turning defenses into random target-selection changes. Review chaining radius,
defended-target continuation and invalid-status exclusions. Preserve the
authored17/19 distinct-recipient caps,93% attenuation and survivor-only healing.

## Pursuit March daily recovery

Status: provisional interpretation; Pursuit March focused acceptance passed13/13.

After a surviving hero wins, including an opponent's retreat or surrender,
restore floor(10% of the current travel-layer daily Movement maximum), capped
at the missing daily Movement. Never reduce already-over-cap Movement. Consume
the once-per-day use only when positive recovery succeeds. The specification
does not define zero-benefit use consumption; preserving it avoids wasting a
reward that cannot restore anything. Review travel-layer changes, victory modes
and whether zero recovery should consume the use. Persist the day through an
authoritative atomic movement update, without per-update polling.

## Pandemonium logical debuff counting

Status: provisional interpretation; principal Pandemonium native acceptance passed25/25.

Count each distinct authored DEBUFF identity once per stack, not every Bonus
component or repeated application. Snapshot all counts before damage resolves.
Pandemonium Master multiplies each contribution by1.25, rather than growing the
multiplier with the count. Physical and magical Poison share one logical Poison
identity. Ongoing spell debuffs may use PERMANENT bonus duration; duration alone
does not make them innate characteristics. Innate/raw-stat sources stay untagged.
Hostile Spell Lock counts; protective friendly Spell Lock does not. Time Stop's
incapacitation and Frenzy's forced zero Defense count as explicit harmful statuses.
Review mixed-effect classification and simultaneous Poison variants. Metadata
must be complete before activation; an unregistered helper is not coverage.


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

Second-look priorities: retained Advanced rank balance, Isra/Vidomina generic
knowledge differentiation, Ash controlspell availability, Oris futureMentor role,
Deemer exploration/fire identity and resource-economy pacing. No workbook riders
are implicitly approved; broad combat/meeting/specialty matrices remain Phase2.

## Divine Retribution — ordinary attack provenance, not physical-only damage

The detailed spell covers melee and ranged creature attacks and excludes spells.
Provisional interpretation: an ordinary primary creature attack qualifies even
when its damage is magical or its projectile has spell-like presentation. An
actual hero spell or active creature spell does not qualify. This distinguishes
an attack action from its damage element rather than inventing a physical-only
restriction absent from the authored text.

Source evidence: ordinary Magog/Lich shooting enters makeAttack/prepareAttack;
the spell-like adapter enumerates collateral and marks presentation. Actual
creature spellcasting instead enters CREATURE_ACTIVE BattleCast::cast. Current
Divine Retribution judgment and AI threat gates exclude spell-like primary shots;
a bounded correction and positive/actual-cast negative controls are assigned.
Implementation and native acceptance remain pending. Recipient MDR applies to
the eventual Holy payout under the existing general magical-damage rule, without
a second cast, action, resistance roll or fabricated delayed caster.

Second look: area collateral and secondary attack scope, interactions with
delayed caster-specific penetration and unusual scripted attack origins remain
Phase2 breadth. This interpretation does not amend spell costs, caps, duration,
once-per-attacker accounting or casualty provenance.

## Default hero armies — user-approved provisional quantities

All144 supplied replacement troop rows remain TBD; their original army rows
provide composition and reference ranges. The user now explicitly approves
preserving that composition and adjusting quantities to current independent
slot Leadership limits. Prefer preserving ordinary random ranges where legal,
not replacing every army with a fixed maximum. Preserve current creature-line
successors and explicit map/saved-context behavior; do not invent additional
troop types or redesign combat statistics. A dedicated implementation worker
is inspecting the existing initializer and authoring seams before setting the
actual144 dispositions. Source, capacity evidence and playable delivery remain
pending. Revisit these prototypes during Phase3 for hero identity and opening
economy rather than treating legal capacity as ideal starting strength.

## Bloodrage — ordinary elemental attacks

The rank and Blood Scent describe damage from creature attacks, without a
physical-only restriction. Provisionally ordinary elemental primary creature
attacks receive their existing Rage premium; actual spellcasting, indirect damage
and magical collateral do not gain an invented extra consumer. Avatar of Rage
retains its separately settled physical Creature Defense channel. The current
payload places all Rage damage inside a physical-only gate; a narrow correction
and actual shot/detached/negative controls are assigned privately, not accepted.
Preserve historical captured behavior where the existing policy supports it.
Second look: unusual scripted attacks and collateral scope remain Phase2.

## Inspirational Leader — direct damage during the covered activation

Provisionally its unqualified damage increase includes a direct active creature
damage spell cast by the affected stack during that activation, as well as its
ordinary attack. It does not enhance a Hero spell or unrelated damage-over-time.
The present melee/ranged bonus producer covers ordinary elemental attacks but
has no active-creature-spell consumer. A bounded source reconciliation and
focused lifetime/actual-cast evidence are assigned; implementation is not yet
accepted. Preserve the explicit75% Morale creature-spell policy and avoid a
global reinterpretation of all attack premiums. Second look: indirect/collateral
origins and unusual scripted spell actions remain Phase2.
