# New Horizons functional completion matrix

Updated: 2026-09-27  
Canonical source SHA-256: `b9c3b35495bea2ba3f26d2b6d2529b6051bdd30a96c618df9f9071d28a250ee0`

This is the durable evidence register for UP-023. It tracks functional gameplay
completion separately from catalogue presence and artwork. An `active` data row,
a description, an icon, or a generic engine primitive is not by itself a
completed mechanic. Completion requires authoritative execution, legal
acquisition/progression, applicable AI behavior, save/network compatibility,
player feedback, focused tests, independent review, and target-build evidence.

## Skills and perks baseline

The canonical catalogue contains 31 Skills, 93 rank effects, and 310 perks: 403
requirements in total. After activating the remaining six Archery perks and
Bulwark's first three Basic perks, 81 ranks and 88 perks are marked active, with
12 ranks and 222 perks still planned. No
entry yet has the whole
UP-023 evidence chain recorded here. The Basic Bulwark source head
`40628d29d92ab0d47282321fd411f5d079f38844` passed Windows build run
`36360403677` (artifact `10945274902`), but native tests and in-game validation
are still pending. Bulwark's remaining seven perks are planned;
`Corpse Preservation` is read but does not
change casualty eligibility.

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
| Light Magic | 3/0 | 0/10 | Progression blocked |
| Shadow Magic | 3/0 | 0/10 | Progression blocked |
| Nature Magic | 3/0 | 0/10 | Progression blocked |
| Havoc Magic | 3/0 | 3/7 | Seven perks missing |
| Sorcery Magic | 3/0 | 10/0 | Evidence audit required |
| Chaos Magic | 3/0 | 0/10 | Progression blocked |
| Spellcraft | 0/3 | 0/10 | Ranks and progression missing |
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
| Bulwark of the Mire | 3/0 | 3/7 | Basic progression opened; seven perks missing |
| Elemental Rebirth | 0/3 | 0/10 | Ranks and progression missing |

Strict progression requires a perk at the preceding rank before the next Skill
rank. Fourteen Skills therefore cannot normally advance beyond Basic because they
have no active Basic perk: War Machines, Command, Light Magic, Shadow Magic,
Nature Magic, Chaos Magic, Spellcraft, Diplomacy, Estates, Learning, Luck, Divine
Mandate, Shroud of Malassa, and Elemental Rebirth.

## Spell baseline

The detailed canonical school rosters govern when they conflict with older
summary counts. They contain 67 combat spells plus five Neutral Adventure
spells. Only 30 of the 67 combat identities currently have active roster rows;
37 are absent or inactive. Presence is not proof that the exact canonical effect
is complete.

| School | Canonical | Active identity coverage | Missing canonical spells |
|---|---:|---:|---|
| Light | 11 | 3 | Sanctuary; Guardian Spirit; Holy Armor; Holy Wrath; Heavenly Gale; Divine Retribution; Purify; Crusade! |
| Shadow | 12 | 2 | Life Drain; Hex of Pain; Frailty; Plague; Soul Chain; Shadow Gift; Vampirism; Re-animate; Soul Reaper; Doom |
| Sorcery | 11 | 9 | Storm of Daggers; Spell Lock |
| Chaos | 11 | 3 | Blink; Confusion; Polymorph; Hand of Fate; Puppet Master; Reality Warp; Pandemonium; Shield of Chaos |
| Nature | 11 | 2 | Regeneration; Entangle; Vengeful Vines; Poison; Summon Trolls; Verdant Prison; Hydra's Vitality; Nature's Wrath; Elemental Convergence |
| Havoc | 11 | 11 | None by identity; exact-effect evidence still required |

All five Adventure spell effects have partial or substantial runtime support,
but their normal acquisition path is dead: Mage Guild generation excludes them,
their cost helper has no production caller, no town unlock state or purchase
packet/UI exists, and the AI cannot buy them. They are Summon Boat, Water Walk,
Town Portal, Fly, and Dimension Door.

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
