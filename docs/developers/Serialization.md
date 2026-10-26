# Serialization

## Captured contexts and detached descriptors

Append serialization features before the CURRENT/MINIMAL aliases and keep version
progression monotonic; a new enumerator must not lower the advertised current
format. Preserve full counter width and explicit domain limits rather than
narrowing a value to bypass an unsupported-type guard.

New-game registry completeness and loaded-snapshot validity are separate checks.
Preserve captured absence on reused objects and reinitialization; never fill it
from installed defaults or mutate shared prototypes. Use the owning world/battle
snapshot, not a conflicting fallback. Keep explicit zero in optional value APIs
distinct from legacy numeric zero-as-absence sentinels.

Where a setting explicitly supports null, preserve absent/null/object distinctions
through JSON, binary, copies and load/resave. Write authored map overrides, not an
installed snapshot invented by the writer. Do not generalize one setting's nullable
exception to unrelated settings or infer runtime admission from header/schema
acceptance alone.

Validate raw structural references before post-deserialization binding can
dereference them; initialized alive/ghost/controller checks follow the appropriate
local initialization. Detached battle teardown must not clear army links owned by
another battle. Descriptor fixtures must project independent stacks and faithful
metadata without mutating the original graph or serializing attached live stacks
as independent. The [battle descriptor limits](#new-horizons-immediate-double-command-continuation)
below still apply; this does not add ongoing-battle world-save or general health
restoration support.

## Legacy border guard and gate conversion

The pre-`QUEST_REWORK` border-guard/gate layout is read-only compatibility input.
Its helper rejects writers before its payload bytes or object mutation, and
rejects a null decoded quest before reading the destination object base or
appending a quest. Valid records retain the legacy quest and synthesize the
colour's `requiredKeys` entry. Registered type10 guard conversion, type12 gate
identity, supported versions and the current quest-source layout are unchanged.

## Captured Skeleton-focused Necromancy

`NEW_HORIZONS_SKELETON_NECROMANCY` protects the optional versioned
`necromancyPolicy` in the captured perk registry. Present policy rows validate
strictly; historical absence keeps the previous perk meanings and is never filled
from installed defaults. The stable perk identifiers retain their historical
names, tiers and mechanics when this marker is absent.

Direct perk-state and hero writers, authored map overrides, pooled/map heroes and
enclosing world writers reject unsupported captures before writing their prefixes.
Campaign scenario/global crossover JSON pools and their StartInfo, lobby and world
envelopes apply the same guard, including heroes reserved outside the current map.
Readers reject an unsupported policy rather than silently discarding it. The old
Zombie, Wight, Bone Dragon and corpse provenance fields remain readable for
historical captures and specialty results.
The new policy includes ordinary magical casualties, still excludes destroyed
remains, and produces Skeletons from enemy living casualties. Graveplate no longer
raises Constructs/Elementals. Grave Knowledge instead restores the winner's
eligible original Skeleton/Skeleton Warrior losses in their original forms before
fresh raising consumes capacity. The existing winner-side Undead eligibility map
carries that filtered input; historical captures retain enemy Undead/Construct
raising. Temporary surplus, summons, clones and destroyed remains are excluded.

The same capability appends actual `recoveredSkeletons` and
`recoveredSkeletonWarriors` counts to the Necromancy result. Direct and enclosing
result writers reject unsupported nonzero receipts before their prefixes; older
readers reset them to zero. Recovery remains separate from fresh output, Mana,
Ossuary delivery and Vidomina's independent conversion. `applied` and
`blockedByArmyCapacity` describe fresh delivery; actual recovery counts remain
visible even when fresh raising is empty or blocked.

## Captured individual specialty riders

`NEW_HORIZONS_INDIVIDUAL_SPECIALTY_RIDERS` protects the optional versioned
`individualSpecialties` block in captured hero-development rules. It contains
Isra's Grave Reaper and Malekith's Warlock's Formula coefficients; it adds no
per-hero runtime receipt. New games capture the installed block. Historical
snapshots without it retain their previous mechanics and specialty presentation,
even when installed defaults contain the new coefficients.

Direct and enclosing save writers reject unsupported captures before writing
their prefixes. Older-format readers reject a rider block rather than silently
discarding it. The capability is appended after the Fly day-end feature;
historical enumerator values remain unchanged.

## Captured school-scoped Spellcraft specialties

`NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES` protects the optional versioned
`schoolSpellcraftSpecialties` block. Its four named coefficient rows are captured
for new games; historical absence remains absent. The existing
`individualSpecialties` version and its compatibility boundary are unchanged.
Direct and enclosing writers reject unsupported captures before their prefixes;
older-format readers reject the unsupported block rather than discarding it.

Original cast factors are transient mechanics context, not a new saved hero or
status field. Copies retain the original Hero-cast fact and numerical factors
derived from saved school membership, including an explicitly empty context. Reflection cannot
recapture the reflecting side's specialty. Cast-specific component bonuses
compose afterward; stored effects retain their evaluated numerical snapshot.

## Captured additional hero specialties

`NEW_HORIZONS_ADDITIONAL_SPECIALTIES` protects the optional versioned
`additionalSpecialties` table. Version 1 admits independently optional
`fieldPhysician`, `manaFurnace`, `graveArchivist` and `spiritTeacher` rows. Their numerical fields are defined in
the hero-rule schema; no missing row is filled into historical captures.
Present rows validate strictly; historical absence stays absent. Unknown keys or
semantic versions are rejected rather than silently ignored. The existing
individual and school-specialty tables retain their own versions and guards.

Field Physician scales Cure's fixed and Spell Power components separately before
Healer's existing Spell Power-only stage. Its Tent creature-healing budget also
feeds Battlefield Medic; repair output and cleansing count retain their ordinary
rules. Starting equipment remains governed by existing hero creation rules.
Direct and enclosing unsupported writers reject before their prefixes; readers
validate captured data before installing it.

Mana Furnace applies only at accepted `BattleStart`, after existing temporary
Buffer and signed drains. Both sides' policies and snapshots validate before
attachment or pool mutation. Each side records only the actual Normal recovery
in `manaFurnaceRecoveredNormal`, bounded by the configured maximum of 1,000,000.
The same additional-specialties capability explicitly protects this
appended runtime field as well as the optional policy table. Historical readers
default the receipt to zero; direct Side, BattleInfo and BattleStart writers
reject lossy output before their prefixes. Readers reject malformed receipts.
A fresh entry must carry zero, preventing replay of an already-applied grant.

Original initial Normal/Buffer snapshots remain the cancellation baseline;
restart rolls back before a fresh entry. Canonical Buffer cleanup is unchanged.
Legacy scalar completion limits retained Normal to the original Normal plus only
the actual persistent recovery, using saturated wide arithmetic, so unrelated
temporary combat bonuses do not become persistent. Active Intelligence and the
final accepted maximum bound the grant; neither Buffer nor capacity is increased.
World saves preserve the resulting pools without dispatching another entry.
They do not preserve or resume ongoing battles; descriptor codec coverage does
not establish midcombat save/resume support.

Grave Archivist and Spirit Teacher reuse the captured table capability without
adding runtime save fields. Their Learning enhancement scales only the core
Learning contribution. Eagle Eye selects distinct eligible enemy-hero casts using
stable descending spell level and original cast order; winner and retained-result
paths use the same selector. Spirit Teacher scales a separate raw Mentor award,
revalidated before granting XP. The existing 250/500 authorization coefficient and
weekly recipient receipts are unchanged; recipient XP modifiers apply once.
Historical row absence retains the former mechanics and specialty presentation.

## Captured recovery specialties

`NEW_HORIZONS_RECOVERY_SPECIALTIES` protects the optional versioned
`recoverySpecialties` table and accepted-combat recovery receipts. Its independently
optional rows are `infernalWellspring`, `ritualReserve`, `deepTrance` and
`herbalSurgeon`; historical absence remains absent. Unsupported direct and enclosing
writers reject before their prefixes, and readers validate captures and receipts.

Deep Trance records the accepted-combat day on the hero and actual recovered Normal
Spell Points on the battle side. Entry validation precedes mutation; cancellation
and restart retain the existing rollback rules. Legacy readers reset absent
receipts, and pool completion retains only actual persistent recovery. These fields
do not add ongoing-battle world-save support. Herbal Surgeon's original-caster
percentage travels with the existing transient cast context; reflection must not
recapture the reflecting hero's specialty.

## Captured Learning and reclamation specialties

`NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS` protects the independently optional
`learningExchangeSpecialties`, `swampSeerSpecialty` and `reclamationSpecialty`
captures. Historical absence remains absent; explicit zero remains a capture.
The shared `SpecialtySerializationContext` admits each family through the same
direct and enclosing guards, including world and lobby serialization. Unsupported
writers reject before their prefixes; readers validate before installing rules.
The older boolean compatibility wrapper cannot admit these later captures.

`NecromancyResult::reclaimerZombiesRaised` records the Zombie subset funded by
reclamation. Current readers validate its bounds and conservation with the final
batch; historical readers reset it to zero. Direct result and enclosing
`BattleResultsApplied` writers reject lossy output before either prefix. This
suffix does not reinterpret historical casualty summaries or add battle saving.

## Captured Fly day-end policy

`NEW_HORIZONS_FLY_DAY_END` protects the presence of
`fly.requireLegalDayEnd` in captured magic rules, including an explicit false.
It does not change magic ruleset version3 or append another hero field. Direct
and enclosing unsupported writers reject before their prefixes; older readers
reject unsupported captures rather than discard the policy. Water Walk keeps
its separate existing compatibility boundary.

New games capture the installed Fly policy. Loaded worlds retain their own
absence/false/true value; initialization must not fill absence from defaults.
The existing flight-layer receipt is tracked when required by protected barriers
or Fly landing policy. Barrier collision still follows only its own policy.
An eligible AIR receipt on ordinary land may end the day without a fictitious
same-coordinate LAND move; the existing NewTurn transition resets the receipt.

Landing plans are transient values, not saved routes or additional receipts.
They do not rescue an already stranded historical hero with no legal movement.
See [AI travel execution](AI.md#same-day-travel-landing).

## Strict perk-tier progression

`NEW_HORIZONS_STRICT_PERK_PROGRESSION` (1118) protects captured
`perkProgressionPolicyVersion: 1`. It permits one Basic, one Advanced and one
Expert perk per Skill, in order. The marker is independent of weighting
`offerWeightPolicyVersion: 2` and its configurable preference ratio; it does not
change the class-weight table or XP curve.

Missing progression markers preserve the world's historical rules, including
previously legal duplicate tiers under weighting policy2 and pending offers.
Do not inject current defaults into an old hero or remove its legal selections.
Strict captures reject duplicate/gapped tiers instead of running the historical
tier-conflict normalizer. Direct and enclosing unsupported writers/readers must
reject strict rules, including settings/worlds without heroes, rather than drop
the marker or silently reinterpret them. Historical marker-absent policy2 remains
representable at its original 1117 boundary.

## Combat activation snapshot state

`NEW_HORIZONS_COMBAT_ACTIVATION_SNAPSHOT` appends two existing combat receipts:

- `CStack` stores Veteran's signed 64-bit physical-damage accumulator. Negative
  values are invalid; older readers initialize absent history to zero. Writers
  targeting older formats reject nonzero history before emitting a stack prefix.
- `BattleInfo` stores a bounded No Quarter lifetime sidecar keyed by unit ID.
  Entries must be unique, reference existing units and contain one or two remaining
  activations. Each entry must match exactly one No Quarter Morale penalty.
  Decode validates the whole sidecar before assigning counters. A legacy penalty
  without an unambiguous lifetime is rejected rather than inventing a duration.
  Codec validation reads serialized local markers, not the searchable bonus
  graph, which need not be bound yet in an in-memory or BattleStart descriptor.

`BattleInfo` and `BattleStart` perform the same unsupported-state checks before
their own prefixes. Stack rebinding preserves both restored receipts.

This extends combat descriptors, not the ordinary midcombat-save contract.
Binary stack descriptors still do not carry ordinary current health; existing
authoritative unit-state JSON remains its carrier. Normal combat saves remain
blocked, and this change does not establish complete battle resume support.

## Optional component help reason (spell-acquisition feedback)

`COMPONENT_HELP_REASON` appends `Component::helpReason`, an optional localized
`MetaString`, after the existing type, subtype and value. Spell reward components
carry the actual recipient's missing School rank only when that is the sole
learning blocker. The client appends this reason to the ordinary spell description;
it does not infer a selected hero or change learning/casting eligibility.

Older reads clear the optional field, including when reading into a reused
Component. Older writes omit this presentation-only metadata without changing
the source Component or its existing identity/value bytes. This omission loses
only explanatory text, not gameplay state. Focused current/older serialization
and recipient classification acceptance is tracked under UP280; rendered help
and playable delivery remain separate gates.

## Primary Experience reward classification (Historian foundation)

`NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD` appends the explicit Boolean
`Reward::primaryExperienceReward`. Missing JSON and older binary reads default
false; non-Boolean authored/map JSON is rejected. Unsupported true direct Reward
writes reject before that Reward's payload. This is not a zero-byte guarantee
for an enclosing world save. Ordinary reward previews and grants share the same
classified fixed-XP calculation. Learning Stone is the first authored source;
Historian remains planned pending the other source classifications, and this
representation does not activate the perk or classify mixed/level rewards.
Focused execution/serialization acceptance is tracked under UP276.

## Armorer Last Stand (source/native verified)

`NEW_HORIZONS_ARMORER_LAST_STAND` appends a per-side combat-used flag.
Accepted physical attack updates carry the selected side and, for lethal
retaliation saving the acting attacker, activation-ended provenance. UnitChanges
JSON retains separate Last Stand Defending and activation-ended markers; their
expiry is the next real activation, not round rollover. Guardian absorption is
separate from ordinary health in the shared lethal-hit calculation.

Older reads default to unused; populated unsupported direct/enclosing writers
must reject before payload bytes. Binary battle descriptors omit general
CUnitState and therefore reject either active Last Stand transient unit marker,
even in the current format. Side-used history alone may roundtrip; this does
not add ongoing-battle save/resume or restore omitted health/Defend lifetimes.
Principal live/packet and detached-AI acceptance passes in UP079:13 focused
cases and22 activated adjacent cases, zero skips. Broader interactions remain
deferred; this is not evidence of midcombat save/resume support.

## Battlefield Mastery round award (source integrated; native validation pending)

`NEW_HORIZONS_BATTLEFIELD_MASTERY` appends two historical per-side award-round
stamps to BattleInfo. Each defaults to -1; nonnegative stamps must not exceed
the current round. Rollover does not reset them. UnitChanges JSON carries separate
Wait/Defend doubled-rank markers with their existing effect lifetimes, and a
validated SetBattlecraftMasteryAward update changes marker plus side stamp
atomically. Unsupported writers reject populated state before payload bytes.

Binary battle descriptors do not retain general CUnitState Wait/Defend lifetime;
even current BattleInfo/BattleStart writers reject active mastery unit markers
rather than pretend to restore an ongoing battle. A historical side stamp alone
can round-trip in the current format. Older reads start at -1. This extension
does not add midbattle save/resume; focused acceptance is tracked in UP-156.

## Round-bounded spell response (UP180; partial runtime/native accepted)

`NEW_HORIZONS_SPELL_RESPONSE` appends `SpellResponseState` to each battle side.
Its `armedInRound` is `-1` when empty, otherwise a nonnegative trigger round.
Readiness includes that round and the following round; checking readiness does
not mutate state or require polling. A later trigger refreshes the same window,
not a second charge. Accepted consumption clears it. Detached battle copies
must retain their own state and apply updates without modifying the live battle.

Older reads default to empty instead of reconstructing a trigger from spell
history. Unsupported writers must reject populated direct and enclosing records
before payload bytes; the new state-update packet is unavailable to older wire
formats. Malformed negative stamps and invalid packet targets are rejected.
This representation does not enable complete ongoing-battle save/resume.

The current catalogue marks Counterpressure active. An accepted enemy Hero spell
arms readiness only when it actually changes that side's army: Dispel removing
its Curse counts even though beneficial, while rejected empty Dispel and a
semantically identical effect refresh do not. Focused live/detached regression
coverage does not establish complete interaction or platform acceptance.

Detached response updates validate the modeled side's actual hero capability,
as live updates do. A player-filtered hero query may hide an opposing hero;
that must not invalidate a legitimate internal effect receipt. Public hero
queries remain filtered. Missing Counterpressure, invalid sides and expired
round windows still reject before changing detached state.

## Ordinary Hero Action sequence (UP244; source/native accepted)

`NEW_HORIZONS_HERO_ACTION_SEQUENCE` appends three right-aligned Action entries
to `AlternatingHeroActionState`. Leading NONE entries represent fewer than
three accepted ordinary Hero-paid actions; remaining entries are SPELL or
ORDER. Unknown entries and internal NONE gaps are invalid. The existing
receipt-filtered authoritative and detached hooks record the same facts,
including zero-empowerment actions. Readiness expiry preserves this history.
Typed Spell/Order grants and creature actions do not enter it.

Older reads start with empty history, not inferred prior actions. Populated
unsupported state and enclosing SideInBattle/BattleInfo writes reject before
payload bytes; BattleStart rejects before either packet field. This prerequisite does not activate Perfect Rhythm or choose
its Master Synthesis stacking rule, and does not lift ongoing-battle save
restrictions. Focused acceptance and playable delivery are tracked separately
in UP244.

## Battlecraft Pre-emptive Strike round state (source/native accepted)

`NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE` protects the new
`CUnitState::battlecraftPreemptiveStrikeRound` carried in unit-state JSON.
`-1` means no dispatched reaction; nonnegative values identify the last round
in which this stack dispatched the Battlecraft reaction. Missing older JSON
defaults to `-1`, and values below `-1` are invalid. State copies retain the
stamp. Another Defend or round rollover does not reset it: availability compares
the current round, independently of Bulwark's Defend-scoped marker.

Populated unsupported direct `UnitChanges` and enclosing `BattleUnitsChanged`,
`BattleStackAttacked`, `BattleAttack` and `StacksInjured` writers reject before
their payload bytes. This extends replicated/detached combat state, not complete
ongoing-battle world-save support. Principal execution/transport acceptance is
tracked in UP157; existing casualty and form guards remain intact.

## Mandate of Heaven completed-pair cap (source/native accepted)

`NEW_HORIZONS_MANDATE_OF_HEAVEN` permits the existing Divine Mandate completed-
pair count to reach4. It appends no fields and does not introduce a second
once-per-combat flag. Expert heroes with the selected active perk derive a
four-pair cap; other existing rank caps are unchanged. Completed history remains
total accepted pairs rather than a mutable remaining-use counter.
Count0–3 preserves the existing layout. Unsupported old direct and enclosing
writers must reject count4 before payload bytes; old-format reads must not
accept an out-of-format fourth pair. Current history is not clamped if later
rank/perk changes lower the derived cap: further grants fail closed instead.
This does not lift whole-battle health/provenance save restrictions. See UP108.

## Knightly Sequence Order efficiency (source/native accepted)

`NEW_HORIZONS_KNIGHTLY_SEQUENCE` appends the separate
`HeroOrderState::knightlySequenceEfficiencyBonusPercent` contribution:0 for
ordinary Orders,5 for a selected Divine Mandate Order with the active perk.
Sacred Command retains its independent0/10 provenance. Shared numerical
consumers sum the two captured contributions, without scaling fixed terms.
Old reads default Knightly to0; populated unsupported direct/enclosing writers
must reject before bytes, and progress updates cannot rewrite the contribution.
The paired Spell discount is derived from the existing typed allowance and
needs no direction ledger or new Mana state. This does not lift existing
whole-battle save health/provenance restrictions. Acceptance is tracked in UP108.

## Sacred Command Order efficiency

`NEW_HORIZONS_SACRED_COMMAND` appends
`HeroOrderState::sacredCommandEfficiencyBonusPercent` after the existing Order
descriptor fields. Zero denotes an ordinary Order; 10 denotes an Order issued
using the selected Divine Mandate allowance with Sacred Command. The shared
pre-acceptance preparation captures this contribution independently of
Warcasting, so later effects retain the accepted value after the opportunity
is consumed. Flat formula terms are not scaled.

Older records default to zero. Populated unsupported direct and enclosing
writers must reject before their payload bytes; invalid contributions are
rejected, and progress-only Order updates cannot change the captured value.
This extension does not implement complete midbattle save/resume; the existing
health/provenance and creature-form fail-closed restrictions still apply.
Implementation and focused validation are tracked separately under UP108.

## Elemental Rebirth starting HP basis (source/native accepted)

`NEW_HORIZONS_ELEMENTAL_REBIRTH` appends a nonnegative frozen battle-start
maximum aggregate HP basis to the `CStack` descriptor. Zero means uncaptured or
ineligible; older reads default to zero rather than inventing historical HP.
Initial eligible stacks capture once after battle-start bonus initialization,
not when evaluator copies are created or later health/HP bonuses change.
Detached views retain/delegate this immutable metadata. Unsupported old-format
writes must reject a populated basis before direct and enclosing payloads.

Temporary Elemental creation uses ordinary unit-addition and exact-health state
updates; no new packet type or normal-Mana-like resource is introduced. This
metadata extension does not implement complete midbattle save/resume: existing
health/provenance and creature-form fail-closed restrictions remain in effect.
UP046 verifies current descriptor-basis roundtrip and unsupported old-writer
rejection. This is not a whole-combat health/save restoration claim. Graphical
acceptance and playable delivery remain separate.

## Explicit elemental spell damage

`NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE` gates the appended `ELEMENTAL_SPELL_DAMAGE`
Bonus type. Its subtype is an explicit damage element, not a Magic School.
Current Bonus records preserve the type/subtype/value normally; direct writers
reject unsupported old formats before their payload. Static spell element
metadata is loaded with spell definitions and adds no per-hero counter or saved
spell-state field. Ordinary equipment identity and bonus inheritance remain the
source of the equipped Orb effect.

`NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE` separately gates the appended
recipient-side `ELEMENTAL_SPELL_DAMAGE_RECEIVED` Bonus type. Bonus records and
stack-effect packets refuse unsupported old writers instead of dropping the
modifier. The earlier outgoing-element milestone remains sufficient for the
outgoing type alone. No new per-stack mutable field is introduced.

## Independent Conflux Core recruitment

The New Horizons Conflux catalogue uses eight recruitment rows, preserving the
first seven dwelling indices and appending the independent Sprite dwelling.
Current town stock and captured creature-growth rules use their existing saved
representations. No new field or binary-format version is introduced.
An old seven-row Conflux stock vector cannot be interpreted under that catalogue:
town decode rejects it before gameplay, with a request to start a new game.
It does not move, convert or delete old Sprite stocks, or rewrite the save file.
Eight-row current records retain their independent stocks through ordinary loads.

## Rewardable next-level Experience

`NEW_HORIZONS_REWARDABLE_NEXT_LEVEL_EXPERIENCE` appends
`Reward::heroExperienceNextLevelPercent`, an integer from 0 to 100. The
percentage is applied to the hero's current Experience gap to the next level;
the base portion is rounded down and then uses the ordinary hero Experience
gain modifier. Rewardable configurations and Experience-component previews use
the same calculation. Older records reset the field to zero, and writers reject
a populated field before writing Reward payload bytes when the target format
does not support it.

## Creature ability suppression (source/native accepted; delivery pending)

`NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION` gates the appended
`CREATURE_ABILITY_SUPPRESSION` Bonus type. Value 1 restricts special, triggered
and activated creature capabilities; value 2 additionally suppresses explicitly
classified passive offensive traits. The timed spell bundle carries ordinary
Spell-effect source/provenance and expiry. It filters evaluated views rather
than deleting intrinsic bonuses, so expiry and Dispel can restore capabilities.
Direct Bonus and enclosing SetStackEffect writers reject unsupported old formats
before their payloads. UP194 passes the focused build/native gate, including real
cast, expiry/Dispel, detached projection restoration and BattleAI coverage.
This does not add ongoing-battle save/resume support. Broader integration,
rendered UI, status-popup wording and playable delivery remain separate.

## New Horizons Forced March state

`NEW_HORIZONS_FORCED_MARCH` appends two absolute-day hero markers: the last
successful exhaustion allowance and the pending next-combat Morale penalty.
Both default to `-1`. A dedicated typed packet carries the complete snapshot;
expiry is evaluated against the current day, without a reset scan.
The battle captures the pending penalty in a generic per-side first-round
Morale modifier, consumed only at authoritative battle startup. The shared
Morale calculation uses that modifier only during round 1, so Time Stop does
not extend it. Old records default to unused/zero; populated state must reject
unsupported writes before payload bytes rather than silently lose the use.
This descriptor extension does not add ongoing-battle save/resume support.
UP192 tracks implementation and focused acceptance separately.

## Explicit bonus status classification

`BONUS_STATUS_TAGS` appends `Bonus::statusTags` and `statusIdentity` after
spell-caster provenance. Tags are explicit author metadata, currently `DEBUFF`;
they do not infer classification from value sign, source, spell polarity or
allegiance. Empty identity retains the existing source/SID grouping identity;
an explicit identity can distinguish different statuses from the same source.
Older reads clear both fields. Populated metadata cannot be down-saved: direct
Bonus and enclosing SetStackEffect writers reject it before their payloads.
JSON uses `statusTags: ["DEBUFF"]` and optional `statusIdentity`, with the same
validation as current binary records. This representation alone does not define
Pandemonium's repeated-application count or activate that spell.

## New Horizons Diplomacy eligibility

`NEW_HORIZONS_DIPLOMACY_ELIGIBILITY` appends the explicit
`CGCreature::diplomacyEligible` map-authored opt-out. The matching optional
`diplomacyEligible` JSON property defaults to true. Older object records reset
the field to true; a false value cannot be written to an older format and is
rejected before object payload bytes. A missing/old captured hero perk-rule
snapshot continues to use legacy Diplomacy behavior rather than activating
rules from the currently installed registry.

## Necromancy Ossuary destination

`NEW_HORIZONS_NECROMANCY_OSSUARY` appends the result's `ossuaryTown` object ID
after special-casualty summaries. NONE denotes normal Hero delivery; a populated
town ID requires an active, successfully applied, positive raised army and no
capacity-block marker. Older records reset NONE; unsupported populated direct
and enclosing writes reject before payload bytes. Actual destination validation
and whole-batch army admission happen at post-battle authority time; the UI
consumes the selected destination without recalculating nearest towns.

## Necromancy Lord of the Dead

`NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD` appends a snapshot flag to
`BattleResult` recording whether the original defeated army contained a living
Champion-tier creature, plus two post-battle summary counts: Skeleton equivalents
consumed and Bone Dragons actually raised. The flag is captured before ordinary
casualty mutation and uses the battle's explicit creature-category snapshot.
Older reads default the flag and counts to false/zero. A true flag or nonzero
Lord of the Dead summary cannot be written to an older format; both the direct
result and enclosing `BattleResultsApplied` validate and reject unsupported
writes before payload bytes. A Dragon result is valid only as exactly one Dragon
consuming exactly 12 offered Skeleton equivalents in an applied, nonblocked
summary.

## Special Necromancy casualty pools

`NEW_HORIZONS_NECROMANCY_SPECIAL_CASUALTIES` appends captured nonliving and
Undead eligible casualty maps and an explicit capture flag to `BattleResult`.
Older records reset these to empty/false; raw casualty maps cannot supply the
new perk inputs. The same boundary appends four nonnegative summary counts:
Death Lord and Grave Knowledge eligible inputs and their generated base
Skeleton-equivalent contributions, before category conversions. Unsupported
populated direct/enclosing writes reject before payload bytes. This does not
add ongoing-battle save/resume.

## Master of Bones output form

`NEW_HORIZONS_NECROMANCY_SKELETON_FORM` appends `skeletonCreature` after the
Wight count. NONE denotes the base/legacy Skeleton output; a populated form
identifies the actual configured upgraded creature, even in a mixed reward.
Older records reset the field to NONE. Direct and enclosing result writers
reject a populated form under unsupported versions before their payloads.
An explicit form requires a nonnegative creature ID and positive Skeleton
output count. Army and UI consumers use the authoritative form, not a separate
client-side perk or town-availability calculation.

## Soul Harvester result payload

`NEW_HORIZONS_NECROMANCY_WIGHTS` appends `wightsRaised` to the existing
Necromancy result without reordering older fields. Older summaries read with
zero Wights. Nonzero Wight outputs cannot be written in an older format:
both the result and enclosing `BattleResultsApplied` reject the write before
their payload. Negative Wight counts are rejected on write and read. Mixed
outputs use the explicit counts, not the legacy single-stack descriptor.

## Ordered usable casualty provenance

`BATTLE_CASUALTY_PROVENANCE` identifies state updates retaining ordered usable
casualty cohorts in each health JSON snapshot. Cohorts carry count, actual damage
nature and temporary-restoration identity. Resurrection selects newest usable
casualties first; temporary expiry preserves the prior death cause, while a new
death takes its new cause. Destroyed remains stay separate and non-restorable.
The original Battle Form health ledger owns provenance while a form is active.

Old snapshots with no recorded causes retain unknown prior deaths as OTHER;
they must not fabricate magical casualty history. New events record explicit
damage nature. UnitChanges, injury, attack and health-change packets reject
lossy older-format writes before their payload. Current JSON state snapshots
preserve matching health and ordered provenance together.

This does not add ongoing-battle save/resume. Binary CStack/BattleInfo/BattleStart
descriptors still omit general CUnitState; a casualty-only sidecar cannot restore
matching health or temporary resurrection. These descriptors reject nonlegacy
casualty provenance even in the current format rather than misrepresenting it.
Ordinary adventure saves do not contain ongoing battles and remain supported.

## Resolved initial deployment ordering

`BATTLE_INITIAL_DEPLOYMENT_ORDER` appends the resolved first side to the generic
`BattleDeploymentState`. Older records default to attacker-first. Initial
opportunities follow that resolved ordering, skipping sides without eligibility;
the separate final-relocation stage remains attacker-first. Live updates may
complete only the current opportunity and cannot change resolved ordering.
Non-default ordering cannot be written to older formats: the state, enclosing
BattleInfo, BattleStart and deployment-change packet reject before their payloads.
Grand Tactics resolves this field during battle setup; it adds no separate action
or duplicated client-side deployment allowance.

## New Horizons Portal of Summoning source

`NEW_HORIZONS_PORTAL_SOURCE` appends the town's external source-dwelling ID and
last successful selection absolute week. Troop stock remains on the dwelling;
the town serializes only the link and quota marker. Old town records default to
no source and week -1; populated state cannot be written to an older format.
The new SelectPortalDwelling and SetPortalDwellingSource packets have appended
polymorphic type IDs. RecruitCreatures appends optional Portal-town context;
ordinary older records default to none, while a populated context fails closed
on an older writer before its payload. Authoritative recruitment validates the
owned built Portal, matching selected still-owned source, and town-associated
destination before charging resources or deducting the source's real stock.

## New Horizons immediate Double Command continuation

`NEW_HORIZONS_DOUBLE_COMMAND` appends a per-side combat-used marker and contextual
Order/Second Wind continuation, plus an ORDER-only allowance source. Accepted
`StartAction` and `BattleHeroOrderStateChanged` packets carry validated optional
transitions. Older records default to unused; writers targeting an older format
reject populated state or grants before writing bytes rather than dropping them.

Battle descriptors validate ledger, round, Order and unit references on decode.
Alive, ghost and current-controller checks require initialized unit state and run
after `BattleStart::localInit` and before writes. This does not add midbattle
save/resume: binary stack descriptors omit `CUnitState`, and ordinary game saves
do not serialize ongoing battles. Descriptor roundtrip tests must not initialize
fresh health and present that as restored combat state.

## Bonus effect hostility

`BONUS_EFFECT_HOSTILITY` appends `Bonus::appliedByEnemy`, a target-relative
application-time provenance flag. Current binary and bonus JSON snapshots
preserve it. Older readers cannot represent a populated flag, so down-saving
one is rejected before the bonus is written. Old records load false: their
unrecorded caster allegiance is not inferred. Dynamic propagated aura ownership
continues to use the existing owner updater and limiter, not this field.

## Bonus spell-caster owner

`BONUS_SPELL_CASTER_OWNER` appends `Bonus::spellCasterOwner` after the
target-relative hostility flag. Actual spell-effect applications record the
effective caster-side owner when known; copied bonuses preserve it, while
non-spell and legacy bonuses default to `CANNOT_DETERMINE`. This stable origin
lets a later effect transfer recompute `appliedByEnemy` for the new recipient
without attributing the effect to the transferring spell. Older readers cannot
retain a known caster owner, so down-saving it is rejected before the bonus
payload. Bonus JSON snapshots use the numeric PlayerColor ID, accepting player
IDs 0–7 and the supported `NEUTRAL`, `UNFLAGGABLE`, and `CANNOT_DETERMINE`
sentinels; old JSON without the property remains unknown.

## New Horizons Land Surveyor weekly allowance

`NEW_HORIZONS_LAND_SURVEYOR` appends a hero's last successful rewarded mine-
capture absolute week. Older hero records default to -1 (unused); a spent
marker cannot be down-saved to a format unable to retain it. The authoritative
capture path replicates the marker through the existing `SetObjectProperty`
packet's appended `NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK` property. That property
is rejected under an older wire version rather than silently discarded. Mine
ownership and resource grants continue through their ordinary packets; there is
no periodic allowance-reset scan and no new packet type registration.

## New Horizons Diplomacy weekly state

`NEW_HORIZONS_DIPLOMACY_WEEKLY_STATE` appends a hero's last Peacemaker use week,
the protected neutral creature's object ID, and last Tribute use week. Old hero
records default to `-1`, `NONE`, and `-1`; populated state cannot be down-saved
to a format that cannot represent it. Week markers below `-1`, negative
non-sentinel object IDs, and a protected target without a nonnegative
Peacemaker week are rejected. A spent marker with no protected target is valid
after deliberate attack; a stale target ID is inactive once its week no longer
matches the current absolute week, so no week-start cleanup scan is needed.

The `SetNewHorizonsDiplomacyState` packet carries the complete weekly snapshot
and has appended polymorphic type ID 293. `NEW_HORIZONS_RECRUITMENT_PACT_STATE`
appends the absolute expiry day of the hero's armed Recruitment Pact. Its
sentinel is `-1`; values below `-1` are invalid. The older weekly-state format
preserves its original three-field payload and defaults the Pact expiry to
`-1`. Writers reject a populated Pact state when saving to a format that cannot
represent it. The hero helper reports the Pact active only while its perk is
active and the current day is nonnegative and no later than the saved expiry;
expiration is evaluated on use, with no polling or cleanup scan.

The packet writer rejects formats without the weekly-state feature, and rejects
a populated Pact expiry when the newer feature is unavailable. Readers reset
fields absent from their format and validate the complete state before
application. The game-state visitor applies the full snapshot atomically to the
referenced hero.

## New Horizons battle Mana expenditure

`BATTLE_HERO_MANA_EXPENDITURE` adds accepted hero spell costs and paid opposing
Counterspell ward costs to `BattleSpellCast`, and the cumulative gross payment
ledger to each `SideInBattle`. Old records load zero; nonzero expenditure may
not be down-saved to a format that cannot represent it. Negative amounts and
inconsistent caster/ward metadata are rejected. Separate restoration, Buffer
grants and hostile drains do not reconstruct or alter this ledger.

Mana Conservation consumes the ledger at authoritative battle finalization and
restores only Normal Spell Points after ordinary result cleanup. This state
extension does not certify full battle-form binary save support; that separate
state still fails closed rather than losing creature-form provenance.

## Introduction

The serializer translates between objects living in our code (like int or CGameState\*) and stream of bytes. Having objects represented as a stream of bytes is useful. Such bytes can send through the network connection (so client and server can communicate) or written to the disk (savegames).

VCMI uses binary format. The primitive types are simply copied from memory, more complex structures are represented as a sequence of primitives.

### Typical tasks

#### Bumping a version number

Different major version of VCMI likely change the format of the save game. Every save game needs a version identifier, that loading can work properly. Compatibility depends on the format and its supported feature guards. The version identifier is a constant named version in Connection.h and should be updated every major VCMI version or development version if the format has been changed. Do not change this constant if it's not required as it leads to full rebuilds. Why should the version be updated? If VCMI cannot detect "invalid" save games the program behaviour is random and undefined. It mostly results in a crash. The reason can be anything from null pointer exceptions, index out of bounds exceptions(ok, they aren't available in c++, but you know what I mean:) or invalid objects loading(too much elements in a vector, etc...) This should be avoided at least for public VCMI releases.

#### Adding a new class

If you want your class to be serializable (eg. being storable in a savegame) you need to define a serialize method template, as described in [#User types](#user-types)

Additionally, if your class is part of one of registered object hierarchies (basically: if it derives from CGObjectInstance, IPropagator, ILimiter, CBonusSystemNode, CPack) it needs to be registered. Just add an appropriate entry in the `RegisterTypes.h` file. See polymorphic serialization for more information.

## How does it work

### Primitive types

They are simply stored in a binary form, as in memory. Compatibility is ensued through the following means:

- VCMI uses internally types that have constant, defined size (like int32_t - has 32 bits on all platforms)
- serializer stores information about its endianness

It's not "really" portable, yet it works properly across all platforms we currently support.

### Dependant types

#### Pointers

Storing pointers mechanics can be and almost always is customized. See [#Additional features](#additional-features).

In the most basic form storing pointer simply sends the object state and loading pointer allocates an object (using "new" operator) and fills its state with the stored data.

#### Arrays

Serializing array is simply serializing all its elements.

### Standard library types

#### STL Containers

First the container size is stored, then every single contained element.

Supported STL types include:

`vector`  
`array`  
`set`  
`unordered_set`  
`list`  
`string`  
`pair`  
`map`

#### Smart pointers

Smart pointers at the moment are treated as the raw C-style pointers. This is very bad and dangerous for shared_ptr and is expected to be fixed somewhen in the future.

The list of supported data types from standard library:

`shared_ptr (partial!!!)`  
`unique_ptr`

#### Boost

Additionally, a few types for Boost are supported as well:

`variant`  
`optional`

### User types

To make the user-defined type serializable, it has to provide a template method serialize. The first argument (typed as template parameter) is a reference to serializer. The second one is version number.

Serializer provides an operator& that is internally expanded to `<<` when serialziing or `>>` when deserializing.

Serializer provides a public bool field `saving`that set to true during serialization and to false for deserialization.

Typically, serializing class involves serializing all its members (given that they are serializable). Sample:

``` cpp
/// The rumor struct consists of a rumor name and text.
struct DLL_LINKAGE Rumor
{
	std::string name;
	std::string text;

	template <typename Handler>
	void serialize(Handler & h, const int version)
	{
		h & name;
		h & text;
	}
};
```

### Backwards compatibility

Serializer, before sending any data, stores its version number. It is passed as the parameter to the serialize method, so conditional code ensuring backwards compatibility can be added.

The generic example below illustrates version-conditioned reads. New Horizons
feature contracts above define supported captured formats and fail-closed
boundaries; absent fields are not permission to install current defaults.

Sample:

``` cpp
/// The rumor struct consists of a rumor name and text.
struct DLL_LINKAGE Rumor
{
	std::string name; //introduced in version 1065
	std::string text;

	template <typename Handler>
	void serialize(Handler & h, const int version)
	{
		if(version >= 1065)
			h & name;
		else //when loading old savegame
			name = "no name"; //set name to a sane default value
	
		h & text;
	}
};
```

### Serializer classes

#### Common information

Serializer classes provide iostream-like interface with operator `<<` for serialization and operator `>>` for deserialization. Serializer upon creation will retrieve/store some metadata (version number, endianness), so even if no object is actually serialized, some data will be passed.

#### Serialization to file

CLoadFile/CSaveFile classes allow to read data to file and store data to file. They take filename as the first parameter in constructor and, optionally, the minimum supported version number (default to the current version). If the construction fails (no file or wrong file) the exception is thrown.

#### Networking

See [Networking](Networking.md)

### Additional features

Here is the list of additional custom features serialzier provides. Most of them can be turned on and off.

- Polymorphic serialization — no flag to control it, turned on by calls to registerType.
- Vectorized list member serialization — enabled by smartVectorMembersSerialization flag.
- Stack instance serialization — enabled by sendStackInstanceByIds flag.
- Smart pointer serialization — enabled by smartPointerSerialization flag.

#### Polymorphic serialization

Serializer is to recognize the true type of object under the pointer if classes of that hierarchy were previously registered.

This means that following will work

``` cpp
Derived *d = new Derived();
Base *basePtr = d;
CSaveFile output("test.dat");
output << b;
//
Base *basePtr = nullptr;
CLoadFile input("test.dat");
input >> basePtr; //a new Derived object will be put under the pointer
```

Class hierarchies that are now registered to benefit from this feature are mostly adventure map object (CGObjectInstance) and network packs (CPack). See the RegisterTypes.h file for the full list.

It is crucial that classes are registered in the same order in the both serializers (storing and loading).

#### Vectorized list member serialization

Both client and server store their own copies of game state and VLC (handlers with data from config). Many game logic objects are stored in the vectors and possess a unique id number that represent also their position in such vector.

The vectorised game objects are:

`CGObjectInstance`  
`CGHeroInstance`  
`CCreature`  
`CArtifact`  
`CArtifactInstance`  
`CQuest`

For this to work, serializer needs an access to gamestate library classes. This is done by calling a method `CSerializer::addStdVecItems(CGameState *gs, LibClasses *lib)`.

When the game ends (or gamestate pointer is invaldiated for another reason) this feature needs to be turned off by toggling its flag.

When vectorized member serialization is turned on, serializing pointer to such object denotes not sending an object itself but rather its identity. For example:

``` cpp
//Server code
CCreature *someCreature = ...;
connection << someCreature;
```

the last line is equivalent to

``` cpp
connection << someCreature->idNumber;
```

``` cpp
//Client code
CCreature *someCreature = nullptr;
connection >> someCreature;
```

the last line is equivalent to

``` cpp
CreatureID id;
connection >> id;
someCreature = VLC->creh->creatures[id.getNum()];
```

Important: this means that the object state is not serialized.

This feature makes sense only for server-client network communication.

#### Stack instance serialization

This feature works very much like the vectorised object serialization. It is like its special case for stack instances that are not vectorised (each hero owns its map). When this option is turned on, sending CStackInstance\* will actually send an owning object (town, hero, garrison, etc) id and the stack slot position.

For this to work, obviously, both sides of the connection need to have exactly the same copies of an armed object and its stacks.

This feature depends on vectorised member serialization being turned on. (Sending owning object by id.)

#### Smart pointer serialization

Note: name is unfortunate, this feature is not about smart pointers (like shared-ptr and unique_ptr). It is for raw C-style pointers, that happen to point to the same object.

This feature makes it that multiple pointers pointing to the same object are not stored twice.

Each time a pointer is stored, a unique id is given to it. If the same pointer is stored a second time, its contents is not serialized — serializer just stores a reference to the id.

For example:

``` cpp
Foo * a = new Foo();
Foo * b = b;

{
	CSaveFile test("test.txt");
	test << a << b;
}

Foo *loadedA, *loadedB;
{
	CLoadFile test("test.txt");
	test >> loadedA >> loadedB;
	//now both pointers point to the same object
	assert(loadedA == loadedB);
}
```

The feature recognizes pointers by addresses. Therefore it allows mixing pointers to base and derived classes. However, it does not allow serializing classes with multiple inheritance using a "non-first" base (other bases have a certain address offset from the actual object).

Pointer cycles are properly handled. This feature makes sense for savegames and is turned on for them.

### New Horizons Confusion metadata

`NEW_HORIZONS_CONFUSION_STATE` appends `battle::ConfusionState` to the binary
`CStack` descriptor. Unit snapshots carry the same value in `state.confusion`:
`pending`, `pendingCaster`, `pendingConfounder`, and `previousResolved`.
Pending control and the last actually resolved Attack/Defend/Wander are separate.
Reapplication and ordinary round boundaries preserve history; a forfeited
activation clears pending control without inventing a resolved result.

Absent older JSON/binary metadata defaults to an empty value. Present fields
have strict types and identifiers. Pending caster provenance accepts a player
or Neutral; an empty pending effect requires `CANNOT_DETERMINE` and no
Confounder flag. These negative sentinel IDs are not numeric player bounds.
Older writers reject meaningful pending state or history before stack, update,
attack/injury wrapper or battle payloads are written, instead of dropping it.

`CUnitState` copies and detached JSON loads retain independent values, and
`CStack::localInit` restores the explicitly carried metadata after ordinary
unit initialization. This does **not** serialize the otherwise omitted general
combat health state or establish full ongoing-battle save/resume support.

`NEW_HORIZONS_CONFUSION_MARKER` adds the dedicated `CONFUSION_PENDING` bonus.
It uses a battle-long `SPELL_EFFECT` marker sourced to the actual spell, with
captured caster provenance, value 1 (ordinary) or 2 (Confounder), and a Debuff
status identity. Marker addition/refresh synchronizes pending control without
erasing history. Accepted removal clears pending only when no marker remains;
Dispel and projected AI removals use the same event-driven lifecycle.
The marker does not use automatic next-activation expiry, which precedes forced
resolution. Direct bonuses, enclosing effect packets and pending-state writers
reject older formats before their payloads. History-only metadata retains the
earlier state-format gate. This does not enable the unfinished forced-action
consumer. The registered effect type `newHorizonsConfusion` accepts only `type`,
`indirect` and `optional`; production saved-roster availability remains inactive
until the entire execution/AI/feedback path is complete.

### New Horizons generic Luck Serendipity

`NEW_HORIZONS_LUCK_SERENDIPITY` appends an independent
`LuckSerendipityState` to each `SideInBattle`: enabled capability, round number,
previous/current-round positive Luck history, and first-ordinary-attack use.
It is separate from the combat-long Sylvan perk with the same display name.
Round one has no previous-round opportunity; eligibility begins in round two.

Accepted `BattleAttack` packets carry the captured controlling side and its
post-strike history. The visitor validates causal attribution and monotonic
same-round transitions before applying mutations. Detached AI copies carry
independent histories and replay the captured controlling side, not a later
owner inferred after the strike.

Older reads default to empty history. Older writers reject meaningful state
before writing enclosing side, battle or attack payloads. This preserves the
existing fail-closed compatibility policy; it does not establish general
ongoing-battle health/save-resume support.

### New Horizons Learning Master Teacher

`NEW_HORIZONS_LEARNING_MASTER_TEACHER` appends two fixed recipient object IDs
to the hero's existing absolute-week Mentor history and to its typed state
packet. Empty slots use `ObjectInstanceID::NONE`, occupy the trailing positions,
and cannot accompany week `-1` with a populated recipient. Recipients must be
distinct nonnegative IDs different from the mentor. Packet application rejects
malformed state and a missing mentor before changing the hero.

Ordinary Mentor records its recipient as well, so later Master Teacher
acquisition cannot repeat or top up that award. Older reads clear recipient IDs.
An older used-week marker with unknown recipients stays spent for that week,
including across resaves; a later week's accepted meeting starts fresh history.
Known recipient IDs cannot be down-saved: direct hero and packet writers reject
that loss before their payload. This is not a zero-byte guarantee for enclosing
world saves. Week rollover requires no scan or state-reset polling.

Master Teacher uses the existing meeting/town authority and recipient Experience
modifier, not a new action. Source integration and focused native acceptance are
tracked separately in UP164.

### Default hero equipment creation intent

Fresh New Horizons Rion and Verdish receive their default First Aid Tent independently
of starting-stack chance. Explicit map equipment, including an empty artifact object,
remains authored. This loader-only intent is consumed by fresh initialization; it is
not new binary state. Binary and crossover reads retain their existing captured-hero
boundary and never request new default equipment. JSON writers preserve an explicit
empty artifact object so a map authoring roundtrip cannot turn it into DEFAULT.

## Embedded replay payload provenance

`NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS` records the exact snapshot encoding and
one encoding version per recorded packet. A chapter can contain packets from
multiple supported versions after loading and continuing an older game. The
canonical packet and replay-snapshot writers return bytes with their actual
writer version; prepared single/batch storage preserves each pair atomically.
Copies, retention, replay planning and cross-chapter fast-forward retain those
versions. A locally captured simultaneous-turn rewind uses its own current
writer version, independently of the historical snapshot that began playback.

Legacy logs contain no trustworthy per-payload provenance. They remain opaque
UNKNOWN records during ordinary load and current resave; the outer save version
is not inferred to describe every historical blob. Playback refuses a sequence
that needs UNKNOWN data before decoding it. A later known snapshot can anchor
later turns without discarding the earlier raw history. No trial decoding or
serializer guard relaxation is used. Unsupported writers refuse loss of known
provenance before direct chapter/log and enclosing world prefixes. UNKNOWN
storage is permitted; UNKNOWN execution is not. Metadata counts, supported
version ranges and empty-snapshot/tag consistency validate on decode, planning
and save. Append reservation checks the current array shape and incoming tags,
then reserves both arrays before its no-throw publication step.
