# Shadow — curses, suffering and borrowed life

[Wiki home](../README.md) · [Progression](../progression.md) · [Combat](../combat.md) · [Orders](../orders.md) · [Creatures](../creatures.md) · [Heroes](../heroes.md) · [Towns](../towns.md) · [Skills](../skills/README.md) · [Spells](../spells/README.md)

Shadow replaces several narrow legacy effects with debuffs that work across more creature types. Its healing and temporary restoration remain distinct from Light's permanent Resurrection.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Curse** | I; 4 / 3 | One enemy uses its natural minimum creature damage; the old Advanced-rank extra penalty is removed. Ordinary duration is **3 rounds without a school rank, 4 at Basic or Advanced, and 5 at Expert**. It remains single-target without the relevant mass perk. |
| **Sorrow** | I; 4 | Reduces one enemy's Morale by `min(3, 1 + floor(P / 70))` for 3 rounds. The ordinary spell remains single-target at every rank. |
| **Life Drain** | I; 5 | Deals `25 + 1.8 × P` damage to one enemy, then heals a chosen friendly stack for **60% of actual damage dealt**. It heals survivors only; all creature types can receive the healing. |
| **Frailty** | II; 8 | Each cast reduces one enemy's base Creature Defense by `min(20%, 10% + 0.05% × P)`. Recasts accumulate to a **60% total reduction**. It lasts for the battle unless dispelled; there is no ordinary round countdown. |
| **Hex of Pain** | II; 8 | For 3 rounds, after the cursed stack attacks or retaliates it suffers Shadow damage: `15 + floor(0.7 × P) + floor(10% of the actual damage it just inflicted)`. |
| **Plague** | III; 11 | Magical disease lasting 3 rounds. At the end of an afflicted stack's turn, deals `25 + 0.8 × P` and tries to spread to one adjacent uninfected stack, **friend or foe**. Constructs, Elementals, Undead and Demons are eligible, subject to ordinary magical defenses. |
| **Soul Chain** | III; 12 | Link one primary enemy to up to two other enemies for 2 rounds. When a secondary takes damage, the primary suffers Shadow damage equal to `min(40%, 20% + 0.10% × P)` of that actual damage. Echoes cannot trigger another Soul Chain echo. |
| **Shadow Gift** | III; 12 | Sacrifice **10%, 20% or 30%** of one friendly stack's current and maximum aggregate HP for the rest of combat. For 3 rounds, its attacks gain a Shadow-damage bonus equal to the chosen percentage × `(1.25 + 0.005 × P)` of their ordinary damage. For example, sacrificing 10% at `P = 100` gives a 17.5% bonus before further modifiers. It is a real HP sacrifice, not merely a temporary wound. |
| **Vampirism** | IV; 15 | For 3 rounds, one friendly stack heals surviving creatures for `min(50%, 25% + 0.15% × P)` of actual attack damage, including retaliation. Any creature type may receive the enchantment, but it cannot restore casualties. |
| **Re-animate** | IV; 16 | Uses a `220 + 5 × P` HP pool to heal and temporarily restore eligible friendly casualties of any species. Restored creatures fight normally but disappear after combat; unusable remains cannot be restored. |
| **Soul Reaper** | V; 21 | Deals `60 + 1.4 × P + 40% of the target's missing aggregate HP`. If the target remains at or below **10% of its maximum aggregate HP** afterward, its survivors are destroyed. |
| **Doom** | V; 25 | For 3 rounds, reduces one enemy's damage, retaliation damage, Initiative and movement by `min(60%, 35% + 0.15% × P)`. Also gives −3 Morale. Creature Defense is unchanged. |

**Shadow perk examples:** Grand Malediction grants **Mass Curse** for **12 / 9 Mana** and **Mass Sorrow** for **12 Mana**. Blood Drinker raises Life Drain healing to 75% of actual damage. Night Feeder adds 15 percentage points after Vampirism's ordinary cap; Soul Binder does the same after Soul Chain's cap. Reanimator strengthens temporary casualty restoration, while Dark Gift reduces the HP sacrificed to Shadow Gift without reducing its attack bonus.

[Spellcasting and notation](README.md#learning-and-casting) · [All schools](README.md) · [Magic Skills](../skills/magic.md)
