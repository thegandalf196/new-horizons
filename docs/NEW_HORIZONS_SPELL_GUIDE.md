# New Horizons spell guide

This companion to the [gameplay changelog](NEW_HORIZONS_GAMEPLAY_CHANGELOG.md) explains the six schools and their combat spells when coming from **Heroes III Complete**. It describes the **current development baseline, 24 October 2026**. It is not a list of changes delivered specifically in 0.85. Older saved games can use earlier rules.

## Learning and casting

Light, Nature, Sorcery, Havoc, Shadow and Chaos replace the original four elemental schools for hero magic. Each has its own Skill and perks. Through ordinary learning, Level III spells require Basic proficiency, Level IV requires Advanced, and Level V requires Expert. Level I–II spells do not require a school rank. A legitimately learned or inscribed eligible combat spell remains castable without that school rank; this includes eligible scrolls. Its Mana cost, required action, target restrictions and immunities still apply.

School rank strengthens the Spell Power component of supported effects: **100% without a rank, 115% at Basic, 130% at Advanced and 145% at Expert**. Spellcraft adds its own efficiency where applicable: **110%, 120% or 130%** at its three ranks. These strengthen the Spell Power part, rather than multiplying every fixed base, duration or cap. Perks and specialties can add further changes.

**Reading the tables:** `P` means the hero's displayed Spell Power. Formulas show the ordinary effect before School, Spellcraft, perks, specialties and target defenses. Rounding depends on the spell: formulas marked `floor` round down; Storm of Daggers rounds its pool and shares to the nearest HP, with halves rounding upward. A cost such as **4 / 3** means 4 Mana without a rank or at Basic, and 3 at Advanced or Expert, before other cost reductions. A single cost is the same at all school ranks. The spellbook shows the cost and effect for the particular hero.

An ordinary spell costs the hero's shared Spell-or-Order action. Additional spell opportunities must come from an ability that grants them. **Aggregate HP** means the combined HP of the entire stack, rather than one creature. Current aggregate HP is what remains alive; maximum aggregate HP is the stack's full reference health. Healing surviving creatures is different from resurrecting casualties; the tables identify spells that can restore dead creatures.

## Light — protection, cleansing and restoration

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

## Nature — terrain, living troops and temporary allies

Nature controls movement without treating every restraint as a lost turn. It also introduces healing that cannot restore casualties and summons that cannot become permanent recruits.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Entangle** | I; 4 | Roots one enemy ground stack for `min(2, 1 + floor(P / 100))` rounds. Voluntary movement becomes 0, but Initiative, adjacent attacks, retaliation, shooting, Wait, Defend and nonmovement abilities remain available. Teleportation or actual displacement breaks the roots. |
| **Regeneration** | I; 4 | For 3 rounds, marks `min(50%, 25% + 0.15% × P)` of damage suffered by surviving creatures as recoverable. Restores that marked HP at the start of the stack's next activation. Dead creatures are not restored. |
| **Vengeful Vines** | I; 5 | Select three distinct connected hexes; bends and triangles are legal. Each enemy stack touching the selected area takes `20 + 1.1 × P` damage once and loses **2 movement Speed for 2 rounds**. Initiative is unchanged; friendly stacks are unaffected. |
| **Poison** | II; 7 | Physically poisons one living enemy stack. Its next three activations suffer `20 + 0.5 × P`, then **1.5 times**, then **2 times** that damage. Poison does not spread. Cure removes it; ordinary Dispel does not. |
| **Summon Trolls** | II; 9 | Summons temporary Trolls with `100 + 2.5 × P` total HP at a legal empty hex. The final creature can begin wounded to preserve the exact HP pool. Trolls retain regeneration and disappear after combat. |
| **Verdant Prison** | III; 12 | Surrounds a chosen enemy with temporary Dendroid Guards on legal adjacent empty hexes. Divides one `180 + 3 × P` HP pool among the created stacks. Dendroids retain their normal abilities and Initiative and remain even if the target escapes. No legal placement means no cast. |
| **Earthquake** | III; 12 | Outside sieges, deals `30 + 0.8 × P` Nature damage in a radius-2 area and leaves fractured ground for 3 rounds, costing an extra movement point to cross. In sieges, instead deals **100 structural damage** to **2–4 eligible nearby fortification sections**, depending on spell strength. It no longer consists solely of random wall attacks as in Complete. |
| **Hydra's Vitality** | IV; 16 | For 3 rounds, increases each creature's maximum HP in one friendly living stack by `min(50%, 25% + 0.15% × P)`. Casting does not fill the new HP capacity. Each surviving creature regenerates 10% of its enhanced maximum HP at the start of its activation. Casualties stay dead; excess HP disappears when the effect expires. |
| **Quicksand** | IV; 16 / 13 | Places individually selected concealed patches on legal empty ground. An enemy cannot see a patch until triggered. A ground creature entering one stops and is trapped until its next activation; friendly troops can also trigger it. Flying creatures ignore it while airborne. The ordinary number of patches ranges from 2 to 5 with spell strength. |
| **Elemental Convergence** | V; 22 | Summons temporary Elementals with `250 + 5 × P` total HP at a legal empty hex. The battlefield determines the kind: grass favors Air; snow, swamp, coasts and water favor Water; dirt, inland sand, rough ground and underground terrain favor Earth; lava favors Fire. Special magical battlefields can override the ordinary terrain choice. |
| **Nature's Wrath** | V; 23 | Select a first living stack. The spell chains to the nearest unvisited stack, up to **17 different stacks**. It damages enemies and heals surviving allies without resurrecting casualties. Initial strength is `110 + 2 × P`, retaining **93% per hop**. A protected recipient still consumes its place in the chain. |

**Nature perk examples:** Verdant Communion grants **Mass Regeneration** for **12 Mana**. Rootcaller extends Entangle by a round, to an ordinary maximum of 3. Herbalist adds ten percentage points to Regeneration before its 50% cap. Beastcaller improves the Troll HP pool by 25%; Verdant Warden improves Verdant Prison's pool by 25%; Elemental Conjurer improves its summon pool by 30% and grants +2 Initiative in the summon round. Mire Shaper adds one Quicksand patch beyond the ordinary cap. Worldroot adds two Nature's Wrath recipients and strengthens its Spell Power term.

## Sorcery — timing, precision and battlefield manipulation

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

## Havoc — destructive damage, mines and fortifications

Havoc gathers much of Complete's direct elemental offense, with revised levels, costs and Spell Power scaling. Its largest spells can also damage the battlefield and siege defenses.

| Spell | Level; Mana | Ordinary damage and important limits |
| --- | --- | --- |
| **Fireball** | I; 5 | `25 + 0.8 × P` in a small selected area. Area attacks can hit friendly troops. |
| **Ice Bolt** | I; 5 | `45 + P` to one enemy, with a **−2 movement Speed** penalty. It does not apply Ice Elementals' separate Frozen status. |
| **Lightning Bolt** | I; 5 | `20 + 1.5 × P` to one enemy. |
| **Frost Ring** | II; 8 | `55 + 1.1 × P` to stacks in the ring around the selected central hex. The center is not struck. |
| **Land Mine** | II; 8 | Each triggered mine deals `60 + 1.1 × P`. Select **2 mine hexes**, increasing to **3 at effective Spell Power 100** and **4 at 200**; school and spell-efficiency bonuses can reach these thresholds earlier. Mines are concealed from the opponent until revealed. |
| **Fire Wall** | III; 12 | Creates a damaging barrier with `40 + P` damage per qualifying contact. Grounded flying creatures are not exempt merely because they possess flight. |
| **Inferno** | III; 13 | `70 + 1.2 × P` over a larger selected area. Friendly troops in the blast are also at risk. |
| **Chain Lightning** | IV; 17 | Initial damage `130 + 1.8 × P`. Up to five hits retain **100%, 70%, 50%, 35% and 25% of the first hit's strength**, rather than repeatedly halving the previous hit. Pay attention to friendly stacks along the chain. |
| **Meteor Shower** | IV; 18 | `110 + 1.5 × P` over the selected area. Also destroys eligible ordinary scenery and deals structural damage to eligible fortifications at **50% of its structural damage strength**. |
| **Armageddon** | V; 24 | `150 + 1.8 × P` across the battlefield, including friendly troops unless protected. Also destroys eligible ordinary scenery and damages fortifications at **100% structural strength**. |
| **Disintegrate** | V; 25 | `180 + 2.5 × P` to one enemy. Creatures killed by the spell leave no usable remains for Resurrection, rebirth or Necromancy. |

**Havoc perk examples:** Mine Layer adds another mine. Conductor improves Chain Lightning's retained damage. **Master Chain Lightning** is a specialty spell rather than another ordinary Guild spell: it uses the same initial damage, strikes up to four enemies and retains **76% per jump at hero level 1**, improving by one percentage point per level to a **90% cap**.

## Shadow — curses, suffering and borrowed life

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

## Chaos — unreliable control and dangerous bargains

Chaos manipulates intentions, positions, bodies and the distribution of magical effects. Several spells can help or harm either army, making target selection more important than the size of the damage number.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Blink** | I; 4 | Teleports one friendly or enemy stack to a random legal position within `min(4, 2 + floor(P / 100))` hexes. Ignores intervening barriers, but the full footprint must fit. It grants no extra action, retaliation or Initiative. No legal destination means no Mana or action spent. |
| **Confusion** | I; 5 | Replaces the target's next activation with a random **Attack, Defend or Wander** behavior. An impossible attack can fall back to approach or Defend. Applying it removes Berserk; a negative-Morale forfeiture consumes the pending Confusion, and Dispel can remove it before it resolves. |
| **Misfortune** | I; 4 / 3 | Prevents positive Luck on one enemy without changing negative Luck. Favorable random creature abilities use `max(25%, 75% − 0.25% × P)` of their normal chance; deterministic abilities remain deterministic. Lasts `min(4, 2 + floor(P / 80))` rounds. |
| **Berserk** | II; 8 / 6 | One enemy attacks the nearest creature regardless of allegiance, judging distance by movement cost. Shooters are forced into melee; a distant target is approached. The compulsion lasts until its next attack. Higher school ranks do not turn it into the old larger area spell. |
| **Forgetfulness** | II; 8 | For `min(3, 1 + floor(P / 80))` rounds, one enemy may only move, make basic melee attacks, Wait and Defend. Shooting, activated abilities, special attacks, triggered abilities and creature spellcasting are suppressed. Ordinary retaliation and basic creature properties remain. It is not merely reduced shooting damage. |
| **Hand of Fate** | III; 12 | Deals `70 + 2.5 × P` to one enemy, then randomly chooses another surviving battlefield stack, **friend or foe**, for damage based on **half the first target's actual HP loss**. The second target applies its own magical defenses; a blocked result is not rerolled. |
| **Polymorph** | III; 12 | Turns one enemy into a random creature of the same category for 2 rounds. Preserves aggregate current HP, allegiance and Initiative position, relocating only if the new footprint requires it. Expiry or Dispel restores the original body with surviving HP; restoration can wait if no legal space exists. |
| **Puppet Master** | IV; 16 | Controls one enemy for **one creature activation**, without changing its allegiance. Wait postpones that same controlled activation rather than creating another. Afterward, the stack gains **2 rounds of Lucidity**, preventing further mental domination/control. |
| **Reality Warp** | IV; 15 | Exchanges transferable temporary magical buffs and debuffs between one friendly and one enemy stack, preserving their strength and remaining duration. Illegal effects stay with their original recipient. Orders, equipment, intrinsic traits, terrain, summoned identity and transformations do not move. If a hostile target resists, neither side exchanges effects. |
| **Shield of Chaos** | V; 23 | For 2 rounds, gives one friendly or enemy stack physical and magical damage reduction of `min(80%, 50% + 0.15% × P)`, but also **−10 Morale and −10 Luck**. It does not stop non-damaging magic. |
| **Pandemonium** | V; 22 | Hits every living battlefield stack, including allies. Each takes damage equal to **its number of distinct active debuffs × `(20 + 0.25 × P)`**. Stacks without debuffs take no damage. Debuffs are counted before any damage resolves. |

**Chaos perk examples:** Blinkmaster considers two legal Blink destinations and chooses the farther. Mindbreaker adds suppression of passive offensive abilities to Forgetfulness. Reality Breaker permits Reality Warp between any two otherwise legal stacks. Paradox Shield adds ten percentage points to Shield of Chaos: physical reduction remains capped at 80%, while that spell's magical protection can reach 90%. Pandemonium Master increases each debuff's contribution by 25%.

## Town spell preferences and adventure magic

Every town can build Mage Guild I–V, including Castle, Stronghold and Fortress. Towns favor the following pairs; a preference is not a promise that every spell in that school will appear in a particular Guild.

| Town | Favored schools |
| --- | --- |
| Castle | Light, Sorcery |
| Rampart | Nature, Light |
| Academy (Tower) | Sorcery, Havoc |
| Inferno | Chaos, Havoc |
| Necropolis | Shadow, Sorcery |
| Dungeon | Havoc, Shadow |
| Stronghold | Chaos, Nature |
| Fortress | Nature, Shadow |
| Conflux | Havoc, Nature |

Adventure magic is separate from the six-school combat spell lottery. These five spells are purchased at the corresponding Mage Guild level:

| Adventure spell | Guild level | Mana | Purchase price |
| --- | --- | --- | --- |
| **Summon Boat** | I | 20 | 2,500 Gold + 1 each of Mercury, Sulfur, Crystal and Gems |
| **Water Walk** | II | 30 | 5,000 Gold + 2 each of Mercury, Sulfur, Crystal and Gems |
| **Town Portal** | III | 50 | 10,000 Gold + 4 each of Mercury, Sulfur, Crystal and Gems |
| **Fly** | IV | 60 | 15,000 Gold + 6 each of Mercury, Sulfur, Crystal and Gems |
| **Dimension Door** | V | 80 | 25,000 Gold + 10 each of Mercury, Sulfur, Crystal and Gems |

Each hero may successfully cast **one adventure spell per day, shared across all five spells**. Casting Town Portal therefore also uses that hero's daily opportunity for Fly or Dimension Door. Movement, destination and map restrictions still apply. Water Walk and Fly require a legal place to finish the day; they do not authorize ending a turn stranded over an invalid location.

The current ordinary combat roster does not include **Animate Dead, Clone, Weakness or Counterspell**, nor the draft Magic Missile. Several other Complete spells have been replaced or retired from hero spell acquisition. Their old names appearing in an older save, creature ability or historical description does not make them additional ordinary Guild spells in this guide.
