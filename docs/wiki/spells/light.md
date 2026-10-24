# Light — protection, cleansing and restoration

[Wiki home](../README.md) · [Progression](../progression.md) · [Combat](../combat.md) · [Orders](../orders.md) · [Creatures](../creatures.md) · [Heroes](../heroes.md) · [Towns](../towns.md) · [Skills](../skills/README.md) · [Spells](../spells/README.md)

Light gains several distinct defenses instead of relying on the old Shield, Air Shield and elemental protection spells. Its support spells distinguish ordinary healing, temporary protection, cleansing and permanent resurrection.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Bless** | I; 4 / 3 | One friendly stack uses its natural maximum creature damage. It no longer gains the old Advanced-rank extra damage point. Ordinary duration is `min(4, 2 + floor(P / 80))` rounds. Expert proficiency alone does not make it a mass spell. |
| **Cure** | I; 4 | Heals `25 + 1.5 × P` HP among surviving creatures and can remove one chosen eligible physical affliction, including Poison, Disease or Frozen. It does not resurrect casualties or serve as a general magical Dispel. |
| **Sanctuary** | I; 5 | Protects one friendly stack from deliberate enemy creature attacks and hostile single-target spells until it next acts. Area and indirect effects still reach it. Moving, attacking or using an offensive ability ends protection; Wait and Defend preserve it. |
| **Holy Armor** | II; 8 | Gives one friendly stack `min(60%, 30% + 0.20% × P)` Magical Damage Reduction for 2 rounds. It reduces magical damage; it does not prevent non-damaging spells. |
| **Guardian Spirit** | II; 8 | Gives one friendly stack a separate `50 + 2 × P` protective HP pool for 2 rounds. Physical creature damage consumes this pool before creature HP. Spell damage bypasses it. |
| **Holy Wrath** | III; 11 | Deals `40 + 2 × P` damage to one enemy stack, increased to 150% against Undead and creatures native to Inferno. |
| **Heavenly Gale** | III; 13 | Protects the friendly army for 2 rounds against physical ranged projectiles, including siege shots. Reduction is `min(80%, 50% + 0.15% × P)`. Melee, spells, magical beams and explosions are unaffected. |
| **Divine Retribution** | IV; 16 | Protects one friendly stack for 2 rounds. Enemy creatures that damage it are Judged: at round's end each takes Holy damage equal to the smaller of **30% of damage it dealt to the protected stack that round** and `25 + 1.25 × P`. Each attacker is punished once per round; spells do not trigger it. |
| **Purify** | IV; 15 | Cleanses friendly stacks within 2 hexes of the selected point. Removes up to `min(2, 1 + floor(P / 120))` temporary negative magical effects from each. Does not heal or remove positive effects. The Purifier perk additionally removes one physical affliction per affected stack. |
| **Resurrection** | V; 22 | Uses a `100 + 4 × P` HP pool to heal the chosen friendly stack and permanently restore eligible casualties. Unlike Re-animate, restored creatures remain after combat. Unusable remains cannot be recovered; summoned creatures and phantoms are ineligible. |
| **Crusade!** | V; 24 | Empowers the friendly army for 3 rounds: Attack and Defense `+min(6, 3 + floor(P / 75))`; flat Initiative `+min(3, 1 + floor(P / 100))`; Magical Damage Reduction `min(25%, 12% + 0.065% × P)`. Negative Morale is treated as 0. |

Earlier New Horizons saves may retain Holy Wrath's previous `40 + 0.2 × P` formula. Loading them does not replace their spell damage with the current default.

**Light perk examples:** Litany grants **Mass Bless**, a distinct army-wide version costing **12 / 9 Mana**. Benediction extends Bless by one round. Healer strengthens Cure's Spell Power healing; Guardian improves Guardian Spirit; Aegis improves Heavenly Gale; Miracle Worker improves Resurrection's casualty-restoration portion. These are learned benefits, not automatic rewards for reaching Expert Light.

[Spellcasting and notation](README.md#learning-and-casting) · [All schools](README.md) · [Magic Skills](../skills/magic.md)
