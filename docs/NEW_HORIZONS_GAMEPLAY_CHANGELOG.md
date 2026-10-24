# New Horizons: gameplay changes in detail

A detailed companion to the [short gameplay and artwork changelog](NEW_HORIZONS_CHANGELOG.md), for players who want to know what the new choices actually do.

**Comparison: Heroes III Complete → New Horizons. Baseline: current development, 24 October 2026.** This is a gameplay reference, not a list of changes shipped specifically in 0.85. It includes development after the published releases; older downloads and saved games can have earlier rules. Numbers remain subject to balance changes.

## Find a rule

- [Skills and all 310 perk alternatives](NEW_HORIZONS_SKILLS_AND_PERKS.md): rank bonuses, tier restrictions, class preferences and individual perk effects.
- [Magic and the active spell roster](NEW_HORIZONS_SPELL_GUIDE.md): six schools, access, Mana costs, damage, durations and targeting.
- [Heroes and army development](#heroes-and-army-development): attributes, Leadership and movement.
- [Orders](#orders): eight military alternatives to casting a spell.
- [Creatures and recruitment](#creatures-and-recruitment): Academy, Conflux, Griffins, Genies and Freeze/Shatter.

## Heroes and army development

### Hero attributes no longer add directly to creature statistics

In Heroes III Complete, a hero's Attack and Defense are added to the army's creature statistics. In New Horizons, creature Attack and Defense remain intrinsic combat values. Hero Attack and Defense contribute through Orders and effects that explicitly use them. A high-Attack hero therefore has a reason to issue an offensive Order, rather than receiving the same passive benefit on every attack.

Class attribute growth follows a fixed class pattern. Spell Power and Knowledge remain important to magic, while Might and Magic heroes have different starting development and class-specific opportunities. Might heroes can develop **Command**, which strengthens Orders; Magic heroes can develop **Wisdom**, which improves spell efficiency.

### Leadership limits each stack separately

Each army slot has capacity `floor(hero Leadership / creature requirement)`. A hero with 1,000 Leadership can command 20 creatures requiring 50 Leadership in **each** eligible slot. Filling one slot does not reduce the capacity of the other six.

Ordinary creature upgrades keep their base form's Leadership requirement. Upgrading a stack does not make that same stack exceed its capacity merely because its creatures became upgraded. Leadership requirements differ between creature families and are separate from recruitment Gold costs.

### Skill ranks and perks alternate

A fully developed Skill follows **Basic Skill → Basic perk → Advanced Skill → Advanced perk → Expert Skill → Expert perk**. There is one chosen perk per tier. Picking a Basic perk closes the other Basic alternatives for that Skill, rather than leaving three interchangeable perk slots.

An external reward may advance the Skill rank early, but missing perk tiers must still be chosen in order. A newly acquired Expert Skill with no perks starts with a Basic perk, then Advanced, then Expert. Older saves may retain their historical progression policy.

A level-up offers up to two Skill choices and two perk choices; choose **one**. Ordinary Skill offers retain strong class preferences. Perk-parent selection compresses those preferences: the most favored Skill has at most twice the weight of the least favored one. Having more available perk alternatives does not give a Skill more chances to supply an offer. With two eligible parent Skills, the two perk offers come from different parents; with only one eligible parent, it supplies one offer.

See the [complete Skill and perk reference](NEW_HORIZONS_SKILLS_AND_PERKS.md) for the actual alternatives.

### Damage-reduction percentages do not simply add up

Ordinary physical damage reductions combine multiplicatively, with an overall 80% reduction cap. Two 20% reductions therefore reduce damage by 36%, rather than 40%. Effects explicitly described as final damage modifiers are applied separately.

### Movement and turn order have separate statistics

**Speed** determines battlefield movement. **Initiative** determines activation order. Increasing one does not automatically increase the other.

Adventure movement belongs to the hero and no longer falls simply because the army contains a slow creature. Terrain, roads and movement abilities still matter.

## Orders

A hero normally spends **one Hero Action per round**, choosing a Spell or an Order. Orders cost no Mana. Their effects normally end with the round; specific perks and faction abilities can alter their duration, strength or action allowance. War machines do not receive ordinary Order benefits.

The table gives the ordinary effects **before Command, perks and other special bonuses**. Here, **A** and **D** mean the hero's current Attack and Defense ratings, including modifiers. For orientation, a starting Knight has 30 Attack and 45 Defense. Round percentage results to the nearest whole number.

| Order | What it does | Ordinary strength |
| --- | --- | --- |
| **Charge!** | Each eligible friendly stack gains a bonus on its first qualifying melee attack after moving at least three hexes. | +`10 + 0.20 × A`% melee damage. |
| **Hold the Line!** | Protects eligible friendly stacks while they remain at the positions they occupied when the Order was issued. | `10 + 0.20 × D`% less physical damage. |
| **Focus Fire!** | Marks one enemy. Eligible friendly shooters deal more ranged damage against that target and halve ordinary distance and obstacle damage penalties, from 50% to 25%. Legal adjacent shots also receive the reduced penalty; targeting restrictions still apply. | +`5 + 0.15 × A`% ranged damage. |
| **Riposte!** | Reduces incoming melee damage and strengthens retaliations. | `5 + 0.10 × D`% less melee damage; +`5 + 0.15 × A`% retaliation damage. |
| **Brace!** | Gives eligible defenders a pre-emptive strike against an enemy that moves at least three hexes into a melee attack. | Pre-emptive strike deals `50 + 0.25 × D`% of its normal damage. |
| **Protect!** | Choose a Protector, then an adjacent friendly Ward. The Protector intercepts the next qualifying melee attack against the Ward while they remain adjacent. | Intercepted attack deals `5 + 0.15 × D`% less physical damage to the Protector. Normally one interception. |
| **Flank!** | Marks one enemy for stronger melee attacks. Attacking it from additional distinct sides increases the bonus further. | +`8 + 0.12 × A`% melee damage, plus 4 percentage points for each additional side used. |
| **Second Wind!** | Gives a friendly stack that has completed its activation another activation this round, with reduced damage. | Retains `50 + round(0.015 × Leadership)`% damage, capped at 100%. |

**Second Wind's percentage is damage, not the chance of receiving the activation.** With 1,000 Leadership and no further bonuses, that additional activation deals 65% damage.

Command increases the attribute-derived part of Order strength by **10% / 20% / 30%** at Basic / Advanced / Expert. It does not multiply the flat starting value. For example, Charge! with 40 Attack gives +18% damage without Command, or +20% with Expert Command after rounding. Perks can specialize this further, and Warcasting rewards alternating Orders and Spells.

## Creatures and recruitment

Creatures are grouped into **Core, Elite and Champion** categories. Speed governs movement; Initiative governs turn order. The following tables show the current ordinary creature statistics before hero effects, spells and other battle modifiers.

### Academy: Cabirs and repair

Tower becomes the desert-themed **Academy**. Gremlins become **Cabirs** and **Cabir Masters**, with new creature artwork and projectiles. **Both forms shoot and carry eight shots.** Both take 50% less Fire-element spell damage and 25% more Water-element spell damage; these elemental interactions are separate from the six hero spell schools.

A Cabir Master stack can use **Repair once per battle**, without spending hero Mana. Repair heals and permanently restores eligible casualties for **10 HP per Cabir Master**, targeting one surviving friendly Stone Gargoyle, Obsidian Gargoyle, Iron Golem or Stone Golem stack. A completely destroyed stack cannot be repaired, and casualties made unavailable for restoration remain unavailable.

### Conflux: Wisps

Conflux gains the **Wisp → Greater Wisp** Core line. Pasis and Monere's creature specialties now follow Wisps rather than Psychic/Magic Elementals.

| Creature | Attack | Defense | Damage | HP | Speed | Initiative | Gold |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: |
| Wisp | 7 | 4 | 2–3 | 12 | 5 | 6 | 160 |
| Greater Wisp | 9 | 6 | 3–4 | 16 | 6 | 7 | 240 |

Both have eight creatures of ordinary weekly growth and a **140 Leadership** requirement. They can pass through occupied creature hexes and use **long-reach melee attacks without retaliation**. They are not shooters. Both have an intrinsic **95% spell-damage reduction**; this protects against damage rather than making them immune to every spell. Their attack uses the Magic Arrow sound.

### The four Elemental lines become Elite

Air, Water, Fire and Earth Elementals and their upgrades have a **250 Leadership** requirement, ordinary weekly growth of **four**, and recruitment prices of **550 Gold** for the base form or **750 Gold** for its upgrade.

| Creature | Attack | Defense | Damage | HP | Speed | Initiative |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| Air Elemental | 12 | 10 | 7–10 | 45 | 10 | 14 |
| Storm Elemental | 13 | 11 | 8–11 | 50 | 10 | 15 |
| Water Elemental | 11 | 12 | 7–10 | 50 | 6 | 9 |
| Ice Elemental | 12 | 14 | 9–12 | 60 | 6 | 10 |
| Fire Elemental | 14 | 9 | 9–12 | 40 | 8 | 12 |
| Energy Elemental | 15 | 10 | 11–14 | 45 | 10 | 13 |
| Earth Elemental | 10 | 14 | 8–11 | 55 | 4 | 6 |
| Magma Elemental | 11 | 16 | 10–13 | 65 | 5 | 7 |

Air emphasizes movement and Initiative; Water favors durability and control; Fire emphasizes offense; Earth favors HP and Defense. Energy Elementals fly. The other base forms and Ice/Magma Elementals move on the ground.

Storm Elementals remain shooters with **16 shots**, ignoring ordinary ranged obstacle penalties while retaining ordinary distance and adjacent-enemy shooting penalties. **Ice Elementals have zero shots and fight in melee.**

#### Freezing Touch and Shatter

- Each successful Ice Elemental melee attack **or retaliation** has a **20% chance** to Freeze its surviving target. Roll once for the stack's resolved attack, not once per creature. Normal damage happens first; Freeze then applies before the defender could retaliate.
- A Frozen stack cannot move, attack, retaliate, Wait, Defend or use creature abilities. It forfeits its next **normal activation**, then thaws. Extra activations do not thaw it.
- The first subsequent ordinary physical creature attack receives **+25% final damage** and Shatters the ice after dealing damage. The victim cannot retaliate against that hit. War machines and indirect damage do not receive the Shatter bonus.
- Direct magical damage also breaks the ice after dealing damage, with **no Shatter bonus**.
- **Cure can remove Frozen; ordinary Dispel cannot.** Freeing a stack before its normal activation lets it act normally when that activation arrives. Freeing it afterward does not recover a forfeited activation.
- Freeze cannot stack or refresh, and the same target can become Frozen **at most once per combat round**, even if it was freed in between.

The choice is tactical: leave the target Frozen to deny an activation, or Shatter it for extra damage while freeing it to act later.

### Griffins gain Elite statistics

Griffins and Royal Griffins are now **Elite**, with **220 Leadership** required for either form. Their ordinary weekly growth is five; recruitment costs are 300 / 450 Gold.

| Creature | Attack | Defense | Damage | HP | Speed | Initiative |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| Griffin | 12 | 12 | 7–10 | 45 | 7 | 7 |
| Royal Griffin | 14 | 14 | 9–12 | 55 | 9 | 9 |

### Genies grant three wishes

Ordinary Genies gain the ability to cast random beneficial spells on allies. Master Genies can **bless allies or curse enemies**, sharing the same **three wishes per battle** between those uses. Choose the target; the particular spell is chosen from the legal pool rather than selected from a spellbook.

The enemy pool contains **Curse, Weakness, Slow, Misfortune, Sorrow and Forgetfulness**, subject to the target's eligibility and existing effects. It does not include arbitrary damage spells or mind control.

| Creature | Attack | Defense | Damage | HP | Gold |
| --- | ---: | ---: | --- | ---: | ---: |
| Genie | 10 | 10 | 8–11 | 35 | 450 |
| Master Genie | 11 | 11 | 9–12 | 40 | 550 |

Both are Elite, have ordinary weekly growth of four, and require **240 Leadership**.

## Hero identities

Individual specialties are separate from the faction Skill shared by a town's heroes. These examples show how heroes now develop distinct strengths. A specialty that improves a Skill or perk does not automatically grant it unless explicitly stated.

| Hero | Specialty | Current effect |
| --- | --- | --- |
| **Daremyth** | **Fortune's Child** | +1 Luck for every five hero levels: +1 at level 5, +2 at level 10, and so on, subject to ordinary Luck limits. |
| **Serena** | **Arcane Preparation** | With Intelligence active, begins combat with temporary Mana Buffer equal to **25% + 5% per level**, capped at 100%, of Intelligence's extra maximum Mana. Round down, with a minimum of one when positive. Unused Buffer expires after combat; it does not raise Normal Mana capacity. |
| **Halon** | **Metamagic Adept** | **One additional Metamagic use per combat** when Metamagic is learned. This is an extra use, not an extra Skill rank. |
| **Rion** | **Field Physician** | Improves **Cure healing, including its fixed healing, and First Aid Tent creature healing by 5% per level**, capped at +100%. Does not add repair, resurrection or extra affliction removal. With default equipment, starts with a First Aid Tent. |
| **Malekith** | **Warlock's Formula** | Improves **Spellcraft's Spell Power bonus to direct magical damage by 5% per level**, capped at +100% of that bonus. It does not double the whole spell or improve fixed damage, healing or duration. |
| **Tiva** | **Swamp Seer** | Improves Learning's core XP bonus by **5% per level**, capped at +100% of that bonus. With Learning active, also gains **250 raw XP for each permanent Knowledge or Spell Power point actually received** from a Garden of Revelation, Star Axis, Library of Enlightenment or School of Magic, before ordinary Learning bonuses. Artifacts, temporary effects and level-up gains do not count. |
| **Vidomina** | **Dark Reclaimer** | After ordinary Necromancy conversions, can turn **5% per level of the remaining Skeleton-equivalent value**, capped at 100%, into Zombies at **three equivalents per Zombie**. Requires an owned Necropolis with a built Walking Dead or Zombie dwelling; no dwelling stock is spent. A complete ordinary raise is retained if the enhanced batch cannot fit. |
| **Sanya / Geon** | **Ecumenical Scholar / Occult Scholar** | Both improve Learning's core XP bonus by **5% per level**, capped at +100% of that bonus. Sanya can teach **one additional eligible Scholar spell**; Geon can receive one. Ordinary Scholar meeting and spell restrictions still apply. |

Percentage increases to a Skill's contribution are not the same as percentage increases to the final result. If Learning normally adds 10 XP, a +50% specialty improvement makes that contribution 15 XP; it does not increase the whole experience award by 50%.

## Presentation and future additions

Academy's desert setting, Cabirs, turbaned Magi and Wisps accompany the gameplay changes. The interface adds Order controls, six-school spellbook presentation, perk choices and specialty icons. Selected new artwork remains provisional; the [short changelog](NEW_HORIZONS_CHANGELOG.md#artwork-and-presentation) summarizes the visual changes.

Governors, regional administration and Caravans are future systems. They are not active mechanics described by this guide.
