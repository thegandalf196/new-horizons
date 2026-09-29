# New Horizons functional completion matrix

Updated: 2026-09-29
Canonical source SHA-256: `bc70b440ced11279915de2cfb1e48fc01e20f61ec041e7a68dae336ed86b33e4`

This is the durable evidence register for UP-023. It tracks functional gameplay
completion separately from catalogue presence and artwork. An `active` data row,
a description, an icon, or a generic engine primitive is not by itself a
completed mechanic. The full evidence chain remains relevant for stabilization;
Phase 1 moves on after production implementation, registration, a working
principal path, the necessary interaction hook, a build, and focused
verification without an obvious crash or state-integrity defect. Unverified
cross-system interactions are deferred explicitly to Phase 2.

## Phase 1 specification-coverage snapshot

The counts below describe coverage, not release readiness. `Active` is a
registry/source status unless a focused execution result is cited. Areas
without a defensible item-level denominator remain explicitly uncounted.

| Specification area | Current coverage | Principal remaining work |
|---|---:|---|
| Skills registered | 31/31 | Three Skills have no active rank effects; many registered Skills lack working perk progression. |
| Skill rank effects active | 84/93 | All three Spellcraft ranks now work and are registered active; Diplomacy, Divine Mandate, and Elemental Rebirth account for the nine planned ranks. |
| Skill perks active | 98/310 | 212 planned; active status alone does not certify behavior. Spell Penetration and Empower Spell have authoritative and prediction evidence. |
| Faction Skill ranks active | 21/27 | Six planned ranks. |
| Faction perks active | 43/90 | 47 planned perks. |
| Canonical combat-spell identities registered | 35/67 | 32 missing/inactive; Nature Poison has focused active-profile cast/tick/AI execution evidence, but rendered/playable delivery remains pending. |
| Adventure spells with ordinary acquisition | 5/5 | All five have a validated town unlock/purchase path, saved town state, visitor learning, client purchase UI, and AI purchasing. Existing spell effects still need their own effect-completeness audit; rendered/playable purchase remains unverified. |
| Orders registered | 8/8 | Config and `HeroCommand::isActive` agree; action/AI/UI integration still needs an item-level audit. |
| Hero-class Leadership profiles | 18/18 | Capability data exists; transfer paths remain a user-reported correctness gap. |
| Creature base-line Leadership requirements | 64/64 | Data coverage only; individual creature mechanics remain unaudited. |
| Creature category forms | 126/126 | 50 Core, 58 Elite, 18 Champion are registered; this is not creature-ability coverage. |
| Siege output formula families | 4/4 | Ballista, Catapult, Tent and defensive tower outputs have data; universal Blacksmith access and Ballista Yard's weekly Siege effect are implemented with focused native tests. Rendered/playable acceptance remains open. |
| Recruitment perks active | 4/10 | Six planned; Muster has server and AI paths. |
| Diplomacy ranks/perks active | 0/3 ranks, 0/10 perks | Deterministic Diplomacy and its UI remain missing. |

Additional canonical breadth not yet reducible to a defensible completion
fraction: nine town/faction sections (33 grouped unique-building table rows),
nine artifact-conversion families, six specialty families, required combat/hero/
adventure UI surfaces, save-state representation, and minimum AI hooks. The
next ledger pass must enumerate these items rather than invent a denominator.

Priority for this phase is missing gameplay coverage, especially shared paths
that unlock several specified items. Focused verification is sufficient to
advance to the next item; rendered/playable and broad interaction evidence
remain separately tracked rather than silently assumed.

## Skills and perks baseline

The canonical catalogue contains 31 Skills, 93 rank effects, and 310 perks: 403
requirements in total. After activating the remaining six Archery perks,
Bulwark's first three Basic perks and six further implemented perks, and Light
Magic's Benediction and Nature Magic's Herbalist, 81 ranks and 96 perks are marked active, with 12 ranks
and 214 perks still planned. No
entry yet has the whole
UP-023 evidence chain recorded here. The Basic Bulwark source head
`40628d29d92ab0d47282321fd411f5d079f38844` passed Windows build run
`36360403677` (artifact `10945274902`), but native tests and in-game validation
are still pending. Deep Bulwark remains planned;
`Corpse Preservation` is read but does not
change casualty eligibility.

Current-source native checkpoint: the Linux client, shared library, and test
targets link. In an isolated TEST profile with New Horizons active, v3 Bless
duration/rank/Benediction and hypothetical AI parity pass 7/7 focused tests;
this activates one Light Basic perk but not the other Light effects. Bulwark's
shared ordinary/automatic activation hook and lethal Poison serialization pass
8/8 focused native tests after independent review. At that earlier checkpoint,
seven other Bulwark perks remained planned. Neither
focused result is rendered or playable-delivery acceptance.

Mire Grip's AI forecast and hypothetical expiry also pass 3/3 focused native
cases after independent review. The projection uses the authoritative
Bulwark-sourced, battle-duration Speed penalty, retaining it through round
rollover and spell/Order continuations and clearing it only at the attacker's
next real activation. This removed one AI parity blocker before the later data
activation.

Toxic Spines' detached BattleAI forecast now converts the positive residual
physical-Poison damage delta through the same AI-value helper as immediate
reflection rather than adding raw HP damage to the score. The focused
`ToxicSpinesProjectsActualReflectionPoisonAndActivationTicks` native case passes
1/1 in an isolated New Horizons TEST profile after a valid owner-view fixture
correction; it also checks the converted score, poison state/tick, and live
battle immutability. This was one forecast seam, not complete Toxic Spines
activation or playable evidence at that checkpoint.

2026-09-28 six-perk activation checkpoint: Toxic Spines, Swamp Renewal, Mire
Grip, Shared Cover, Immovable, and Vengeful Mire now have active production
rows; Deep Bulwark remains planned because no nonmagical forced-displacement
producer exists. The isolated New Horizons profile passes 11/11 focused
authoritative cases and 8/8 focused BattleAI cases using production perk data;
the AI fixture no longer overrides statuses. Independent source review found no
blocking activation issue. After repairing seven invalid fixtures, the broader
active-profile Bulwark regression passes 53/53. A subsequent client slice
added persistent physical-Poison status with remaining activations and the
authoritative next tick; the Linux client and focused native UI test pass,
and independent source review found no blocker. Its native-resolution layout
and playable behavior remain unverified, and no target-package acceptance is
claimed.

2026-09-28 magic/Cure checkpoint: saved v1/v2/v3 battle-start round-trips
exercise all 23 inherited core creature-spell Expert target shapes (45/45
focused profile tests). Focus Magic now scales only its Arcane Breach Spell
Power term by the saved Sorcery rank and reports the current ordinary value in
help; 12/12 focused casts/help tests pass and independent review found no
source blocker. Cure's selected physical-Poison path and actual survivor-wound
predicate pass 24/24 focused native cases, including no-op and Spell Lock
rejections; independent review found no source blocker. Focus Magic's
rank-sensitive detached BattleAI projection passes 8/8 focused
cases after independent source review, and the post-Cure combined spell/Cure
selection passes 74/74 with zero skips. Rendered/playable checks remain open.
These slices do not close the remaining combat-spell identities or any
full Skill's completion chain.

2026-09-28 Time Stop rank slice: Sorcery School rank and eligible Warcasting
scale its Spell Power radius threshold without changing its fixed radius,
Chronomancer cap, or stasis lifetime. Authoritative cast, preview hexes, and
AI affected-stack valuation share the Lua radius path; v2 saved rules stay at
100%, and v1 roster access is not widened. The Linux native test target links,
11/11 changed-behavior cases and 15/15 Time Stop-named cases pass in an isolated
New Horizons TEST profile. Independent source review found no blocker. No
rendered/playable or target-package verification is claimed.

2026-09-28 Holy Wrath Phase 1 checkpoint: the missing Level-3 Light identity
is registered at 11 Mana with one-enemy targeting and `40 + 2 × SP` base
damage. Saved-v3 Light School rank strengthens only the SP term; Undead or
Inferno-origin targets receive one 1.5× final bonus before the ordinary
per-source damage cap. Saved v1/v2 roster boundaries remain. Purpose-made
provisional book/effect art is bound; the scenario-bonus frame is still a
placeholder. The Linux native test target builds, the focused authoritative
suite passes 10/10, and the actual BattleEvaluator choice/forecast/cast case
passes 1/1 in an isolated active New Horizons profile. Root confirmation ran
the 11 cases together with zero skips and exit 0. The content suite passes
34/34 and module-mirror check passes. Independent review confirmed the
damage-cap fix. Ordinary guild acquisition, save roundtrip, rendered icon
presentation and playable delivery remain unverified Phase 2/delivery work;
this does not close the broader spell or Skill coverage gaps.

2026-09-28 Nature Poison source checkpoint: the distinct
`new-horizons:poison` Level-2 hero spell is registered in the saved-v3 Nature
roster at 7 Mana. `core:poison` remains the older creature ability and the
physical-affliction marker recognized by Cure; reclassifying it as a hero spell
would be invalid. The cast path uses the existing serialized physical-Poison
state, School-rank-scaled Spell Power term, three escalating activation ticks,
and equal/stronger refresh rules. Provisional purpose-made art is bound.
Both Linux `vcmitest` and `vcmiclient` link, and the offline content suite
passes 47/47. A fresh isolated
TEST preset activating New Horizons passed eight authoritative/AI Poison cases
with zero skips; the saved-v2 roster exclusion and adjacent Magic Arrow AI
regression each pass 1/1. Earlier runs that skipped every case under stale
presets are not counted. Hero-source kill attribution, broader save/dispel
interactions, and rendered/playable acceptance remain separate.

| Skill | Active/planned ranks | Active/planned perks | Immediate state |
|---|---:|---:|---|
| Offense | 3/0 | 10/0 | Evidence audit required |
| Armorer | 3/0 | 4/6 | Six perks missing |
| Archery | 3/0 | 10/0 | All ten perks are active; focused evidence pending |
| Battlecraft | 3/0 | 1/9 | Nine perks missing |
| War Machines | 3/0 | 0/10 | Progression blocked |
| Discipline | 3/0 | 1/9 | Nine perks missing |
| Recruitment | 3/0 | 4/6 | Six perks missing |
| Command | 3/0 | 0/10 | Progression blocked |
| Light Magic | 3/0 | 1/9 | Benediction active; nine perks missing |
| Shadow Magic | 3/0 | 0/10 | Progression blocked |
| Nature Magic | 3/0 | 1/9 | Herbalist active with Regeneration-focused runtime evidence; nine perks missing |
| Havoc Magic | 3/0 | 3/7 | Seven perks missing |
| Sorcery Magic | 3/0 | 10/0 | Evidence audit required |
| Chaos Magic | 3/0 | 0/10 | Progression blocked |
| Spellcraft | 3/0 | 2/8 | Spell Penetration opens normal Advanced progression. Empower Spell uses the final Wisdom-adjusted 12-Mana threshold and scales Spell Power-derived terms without moving fixed bases; its Advanced selection opens Expert progression. Basic/Advanced/Expert rank efficiency is 110/120/130% under saved v3 rules. |
| Wisdom | 3/0 | 1/9 | Nine perks missing |
| Warcasting | 3/0 | 4/6 | Six perks missing |
| Logistics | 3/0 | 3/7 | Seven perks missing |
| Diplomacy | 0/3 | 0/10 | Ranks and progression missing |
| Estates | 3/0 | 0/10 | Progression blocked |
| Learning | 3/0 | 0/10 | Progression blocked |
| Luck | 3/0 | 0/10 | Progression blocked |
| Divine Mandate | 0/3 | 0/10 | Ranks and progression missing |
| Sylvan Luck | 3/0 | 10/0 | Evidence audit required |
| Metamagic | 3/0 | 10/0 | Evidence audit required |
| Shroud of Malassa | 3/0 | 0/10 | Progression blocked |
| Demonic Gating | 3/0 | 10/0 | Evidence audit required |
| Necromancy | 3/0 | 3/7 | Seven perks missing; one inert hook |
| Bloodrage | 3/0 | 1/9 | Nine perks missing |
| Bulwark of the Mire | 3/0 | 9/1 | Nine perks are active in committed source; 53/53 native regressions pass; persistent Poison status builds and passes focused tests; Deep Bulwark and rendered/playable evidence remain open |
| Elemental Rebirth | 0/3 | 0/10 | Ranks and progression missing |

Strict progression requires a perk at the preceding rank before the next Skill
rank. Eleven Skills therefore cannot normally advance beyond Basic because they
have no active Basic perk: War Machines, Command, Shadow Magic,
Chaos Magic, Diplomacy, Estates, Learning, Luck, Divine
Mandate, Shroud of Malassa, and Elemental Rebirth.

## Spell baseline

The detailed canonical school rosters govern when they conflict with older
summary counts. They contain 67 combat spells plus five Neutral Adventure
spells. Rechecking the current saved roster after adding Holy Wrath, Storm
of Daggers, Regeneration, Nature Poison, and Holy Armor shows 36 of the 67 combat identities with active settings rows and
registered mod/core definitions, including Spell Lock; 31 are absent or inactive. An active
identity is not proof that its exact canonical effect is complete.

Quicksand is among the active Nature identities but is not effect-complete.
The saved-v3 Spell Power-derived patch count passes 31/31 adjacent native
tests including actual Lua obstacle application; exact sequential
caster placement, authoritative target validation, corresponding battle UI,
and deliberate AI trap placement still remain. Do not count its identity row
as full spell-specification coverage.

| School | Canonical | Active identity coverage | Missing canonical spells |
|---|---:|---:|---|
| Light | 11 | 5 | Sanctuary; Guardian Spirit; Heavenly Gale; Divine Retribution; Purify; Crusade! |
| Shadow | 12 | 2 | Life Drain; Hex of Pain; Frailty; Plague; Soul Chain; Shadow Gift; Vampirism; Re-animate; Soul Reaper; Doom |
| Sorcery | 11 | 11 | None by identity; exact-effect evidence still required for other spells |
| Chaos | 11 | 3 | Blink; Confusion; Polymorph; Hand of Fate; Puppet Master; Reality Warp; Pandemonium; Shield of Chaos |
| Nature | 11 | 4 | Entangle; Vengeful Vines; Summon Trolls; Verdant Prison; Hydra's Vitality; Nature's Wrath; Elemental Convergence |
| Havoc | 11 | 11 | None by identity; exact-effect evidence still required |

All five Adventure spell effects have partial or substantial runtime support,
and adventure casting calls their Mana-cost helper. Their ordinary acquisition
path now uses a per-town Guild-tier unlock: the authoritative purchase checks
ownership, active turn, eligibility, built Guild tier, duplication, and saved
cost before charging; visiting heroes learn purchased spells, and later visitors
learn town-unlocked spells. Garrisoned heroes are not treated as visitors. The
client exposes purchase controls and Nullkiller can buy affordable unlocks.
The Linux `vcmitest` and `vcmiclient` targets linked and an isolated New Horizons
profile passed 6/6 focused server/AI cases plus 1/1 polymorphic packet
round-trip; the UI source guard passed. A rendered purchase journey and
individual effect-completeness audit remain outstanding. These five spells
are Summon Boat, Water Walk, Town Portal, Fly, and Dimension Door.

Thirty-eight legacy core spells are still admitted despite not belonging to the
detailed canonical combat rosters. The cleanup must disable their ordinary
acquisition without breaking creature abilities or saved compatibility:

`Air Elemental`, `Air Shield`, `Animate Dead`, `Anti-Magic`, `Blind`,
`Bloodlust`, `Counterstrike`, `Death Ripple`, `Destroy Undead`, `Disguise`,
`Disrupting Ray`, `Earth Elemental`, `Fire Elemental`, `Fire Shield`,
`Force Field`, `Fortune`, `Frenzy`, `Haste`, `Hypnotize`, `Magic Mirror`,
`Mirth`, `Prayer`, `Precision`, `Protection from Air`, `Protection from Earth`,
`Protection from Fire`, `Protection from Water`, `Remove Obstacle`, `Sacrifice`,
`Scuttle Boat`, `Shield`, `Slayer`, `Stone Skin`, `View Air`, `View Earth`,
`Visions`, `Water Elemental`, and `Weakness`.

Additional roster corrections: Sorrow belongs to Shadow rather than Chaos;
Implosion belongs to Sorcery rather than Havoc; Earthquake belongs to Nature
rather than Havoc; Counterspell is not in the current canonical roster; Master
Chain Lightning is Solmyr's specialty and must not become an ordinary Guild
spell. Ice Bolt must not retain its legacy Speed/Initiative reduction.

## Active slices

1. **Spell Lock:** complete the already implemented canonical mechanic across
   roster admission, acquisition, AI, feedback and focused evidence.
2. **Archery:** all four Basic and six Advanced/Expert perks are active.
   Authoritative mechanics, AI projection, progression, and combat logs are
   implemented; focused C++ source syntax checks and the 15-case perk-data test
   pass. Native test execution remains pending: `cmake --build
   build/new-horizons-linux --target vcmitest -j4` stops during reconfigure at
   `CMakeLists.txt:846` with `Stale curated module settings: run python3
   tools/update-new-horizons-module.py`. The generator was not run; unrelated
   category/bonus mirror drift is preserved. Record native/runtime evidence
   separately while preserving all three rank effects.
3. **Bulwark of the Mire:** next full Skill slice after Spell Lock and Archery;
   validate all three rank effects and implement all ten perks, including turning
   the existing inaccessible Mireborn hook into a complete selectable mechanic.

Next slices are selected by dependency leverage: remove progression deadlocks,
reuse generic infrastructure across multiple requirements, and never activate a
catalogue entry before its mechanic and evidence exist.
