# Sorcery — timing, precision and battlefield manipulation

[Wiki home](../README.md) · [Progression](../progression.md) · [Combat](../combat.md) · [Orders](../orders.md) · [Creatures](../creatures.md) · [Heroes](../heroes.md) · [Towns](../towns.md) · [Skills](../skills/README.md) · [Spells](../spells/README.md)

Sorcery receives Magic Arrow and several new ways to alter position, spell effects and activation timing. Slow reduces Initiative, rather than being a movement penalty.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Magic Arrow** | I; 4 | Deals `20 + 2 × P` damage to one enemy. Optional Overcharge increases damage and Mana expenditure; the ordinary maximum number of Overcharge steps is `min(5, 2 + floor(P / 50))`. |
| **Haste** | I; 4 / 3 | Increases a friendly stack's movement Speed: +3 at no school rank/Basic, +5 at Advanced/Expert. This is distinct from Slow's Initiative reduction. Spell Power and applicable bonuses determine its duration. |
| **Slow** | I; 4 / 3 | Reduces one enemy's **Initiative only** by `min(50%, 20% + floor(P / 5))` for 2 rounds. It does not reduce movement Speed. Expert proficiency alone does not make it a mass spell. |
| **Dispel** | I; 4 | Removes temporary magical buffs and debuffs from one friendly or enemy stack. It does not remove Orders, innate states, physical Poison, Frozen, terrain or summoned creatures. Expert proficiency does not make it army-wide. |
| **Storm of Daggers** | II; 8 | Select **one to five distinct enemy stacks**. Start with a `45 + 1.5 × P` damage pool, increase the pool by **15% per additional selected stack**, then divide it equally among the targets. Friendly stacks cannot be selected. Selecting more targets broadens coverage rather than granting full damage to every target. |
| **Transfigure Matter** | II; 8 | Converts an ordinary physical obstacle into temporary Diamond Golems with `80 + 2 × P + 50 × occupied hexes` total HP. Walls, gates, moats, corpses, magical obstacles and artifacts are not valid material. |
| **Focus Magic** | III; 11 | For 3 rounds, damaging ranged attacks by the enchanted friendly stack place Arcane Breach marks on their victims. Up to three marks; each gives subsequent friendly ranged attacks `min(20%, 10% + 0.05% × P)` Creature Defense penetration. A new mark refreshes existing marks to 2 rounds. The marking hit gains no benefit from its new mark; melee attacks do not benefit. |
| **Teleport** | III; 12 | Relocates a friendly stack to a legal empty destination. It ignores intervening terrain, obstacles, walls and Zones of Control, but still needs room for the complete creature footprint. It does not grant a new activation. |
| **Implosion** | IV; 16 / 13 | Deals `min(30%, 12% + 0.10% × P)` of the target's **current aggregate HP** before magical defenses. After damage, other living stacks within 3 hexes are pulled one legal hex closer. The primary target stays in place; the pull does not trigger retaliation. |
| **Phantom Army** | IV; 15 | Copies one friendly stack's current creature count for 2 rounds. The phantom attacks with the copied body's normal damage, but has a separate Integrity pool equal to source current HP × `min(40%, 20% + 0.15% × P)`. It takes **25% physical damage** and **200% magical damage**, leaves no remains and cannot supply permanent troops or resources. |
| **Spell Lock** | V; 22 | Seals one stack against all magic for `min(3, 1 + floor(P / 80))` rounds. On a friendly target it removes hostile magical effects and preserves beneficial ones; on an enemy it does the reverse. Preserved timed effects continue working while their duration counters pause. Orders are unaffected. |
| **Time Stop** | V; 23 | Places a selected area outside time until the beginning of the caster's next Hero Action. Radius is `min(2, 1 + floor(P / 100))`. Creatures inside cannot act, take damage, heal, teleport or receive new effects; existing effect durations do not count down during stasis. |

**Sorcery perk examples:** Temporal Field grants **Mass Slow** for **12 / 9 Mana**. It affects all eligible enemies at **60% of ordinary Slow's final Initiative penalty**, with the same duration. Overcharger extends Magic Arrow's charging options. Chronomancer raises Time Stop's radius cap to 3 without granting a free radius increase. These are separate effects from ordinary school-rank improvements.

[Spellcasting and notation](README.md#learning-and-casting) · [All schools](README.md) · [Magic Skills](../skills/magic.md)
