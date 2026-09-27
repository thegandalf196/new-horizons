# New Horizons functional completion matrix

Updated: 2026-09-27  
Canonical source SHA-256: `7c7c6c1a45c3b4ee4a3c5c625dd1b1d10eb46acdb6029aedfe8735a523c1108a`

This is the durable evidence register for UP-023. It tracks functional gameplay
completion separately from catalogue presence and artwork. An `active` data row,
a description, an icon, or a generic engine primitive is not by itself a
completed mechanic. Completion requires authoritative execution, legal
acquisition/progression, applicable AI behavior, save/network compatibility,
player feedback, focused tests, independent review, and target-build evidence.

## Skills and perks baseline

The canonical catalogue contains 31 Skills, 93 rank effects, and 310 perks: 403
requirements in total. The 2026-09-27 static audit found 81 ranks and 75 perks
marked active, 12 ranks and 235 perks marked planned. No entry yet has the whole
UP-023 evidence chain recorded here. `Mireborn` has an inaccessible partial
runtime hook despite planned status; `Corpse Preservation` is read but does not
change casualty eligibility.

| Skill | Active/planned ranks | Active/planned perks | Immediate state |
|---|---:|---:|---|
| Offense | 3/0 | 10/0 | Evidence audit required |
| Armorer | 3/0 | 4/6 | Six perks missing |
| Archery | 3/0 | 0/10 | Active implementation slice; progression blocked |
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
| Bulwark of the Mire | 3/0 | 0/10 | Progression blocked; Mireborn partial |
| Elemental Rebirth | 0/3 | 0/10 | Ranks and progression missing |

Strict progression requires a perk at the preceding rank before the next Skill
rank. Sixteen Skills therefore cannot normally advance beyond Basic because they
have no active Basic perk: Archery, War Machines, Command, Light Magic, Shadow
Magic, Nature Magic, Chaos Magic, Spellcraft, Diplomacy, Estates, Learning, Luck,
Divine Mandate, Shroud of Malassa, Bulwark of the Mire, and Elemental Rebirth.

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
2. **Archery:** implement all ten perks and remove the Skill's progression
   deadlock, while preserving the existing three rank effects.
3. **Bulwark of the Mire:** next full Skill slice after Spell Lock and Archery;
   validate all three rank effects and implement all ten perks, including turning
   the existing inaccessible Mireborn hook into a complete selectable mechanic.

Next slices are selected by dependency leverage: remove progression deadlocks,
reuse generic infrastructure across multiple requirements, and never activate a
catalogue entry before its mechanic and evidence exist.
