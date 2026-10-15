# Roadmap

The source DOCX contains one embedded conceptual illustration at the beginning of the Magic section. It is not reproduced here because its reuse provenance has not been cleared; retain it as reference material only.

### Version 1.0

###### New

- Orders

- Skill Perk System

- Command — Might-exclusive Skill

- Leadership — Secondary Attribute

- Siege — Secondary Attribute

###### Changes

- Magic Schools: 4 → 6

- Primary Attributes rework — deterministic class growth and rating-based scaling

- Skills rework

- Ballistics + Artillery + First Aid → War Machines

- Creature tiers: 7 → Core / Elite / Champion

- Speed split into Speed and Initiative

- Luck and Morale range: −10 to +10

Adventure Spells rework - five neutral strategic spells purchased as fixed Mage Guild unlocks Mage Guilds standardized to Levels I-V for every faction, including new town-screen building art where levels did not previously exist

Town unique-building rebalance - faction identity over artificial symmetry

Adventure-map Movement rework - hero Movement is independent of creature combat Speed and uses the Olden Era-style base/terrain/road model

Tower Elite creature rebalance - Magi/Arch Magi become premium offensive shooters; Genies shift toward cheaper mobile support

Experimental Values register - centralized test numbers for unresolved combat, Leadership, Luck/Morale, Siege, Adventure Magic, artifacts, growth and economy

### Foreseeable Future

- Governors / Regional Administration

- Caravans / Reinforcement Logistics

# Mechanics

### Core Rules

This section is the canonical home for rules that apply across several systems. Detailed sections still define individual Skills, Spells, Orders, and buildings, but global terminology and formulas should agree with the rules collected here.

###### Hero Actions and Creature Activations

Normally, once per combat round, a hero uses one Hero Action to cast one Spell or issue one Order. Exceptional abilities such as Metamagic, Divine Mandate, and Double Command state their additional Spell-only or Order-only opportunity directly; these typed restrictions are contextual allowances, not a general action-token currency or three independent counters. Authoritative validation, spending, expiry, AI projection, logs, and save/load must agree, and a Creature Activation never spends a hero allowance.

A creature stack acts through a Creature Activation. Effects that grant an additional Creature Activation, move a stack in the initiative sequence, or replace an activation must say so explicitly.

###### Adventure-map Movement

New Horizons removes the Heroes III rule that ties a hero's daily movement allowance to the Speed of the slowest creature in the army. Creature combat Speed has no effect on adventuremap Movement. A hero may command slow creatures without becoming strategically immobile. The baseline follows the Olden Era movement model: every hero begins the day with 200 base Movement Points before Skills, artifacts, town effects, and other modifiers. Percentage modifiers to maximum Movement are additive unless an effect explicitly says otherwise. Maximum Daily Movement = floor(200 x (1 + sum of percentage Movement bonuses) + flat Movement bonuses)

Logistics therefore modifies the hero rather than the army: Basic / Advanced / Expert Logistics currently grant +10% / +20% / +30% maximum adventure-map Movement.

###### Movement cost by direction and terrain

A horizontal or vertical step has a base cost of 10 Movement Points. A diagonal step has a base cost of 14. Final tile cost is rounded up after terrain and road modifiers.

|**Condition**|**Movement-cost multiplier**|**Rule**|
|---|---|---|
|Native or ordinary terrain|x1.00|No terrain surcharge.|
|Non-native terrain|x1.40|Applies unless the hero is native to the terrain or the entire army is composed of creatures native to it.|
|Desert|x1.80|No faction treats Desert as native unless a scenario explicitly overrides this.|
|Road|x0.67|Applied to the final terrain-adjusted step cost; roads reduce travel cost by 33%.|



Terrain affinity is an army-composition rule, not a creature-Speed rule. A mixed army does not remove a non-native terrain penalty merely because one native stack is present; either the hero

is native to that terrain or the army as a whole qualifies. Faction-native terrain is data-driven so custom scenarios and future factions can define it cleanly.

Sea travel uses the hero's Movement system rather than creature Speed. Navigation, Lighthouses, embarkation rules, Water Walk, Fly, and other explicit effects modify the relevant costs or maximum Movement.

###### Speed and Initiative

Creature Speed and Initiative are separate combat statistics. Speed determines battlefield movement range during a Creature Activation. Initiative determines when that activation occurs. Neither statistic determines the hero's daily adventure-map Movement. Slow reduces Initiative only; Vengeful Vines may reduce Speed. Mage and Arch Mage both have Speed 5, with Initiative 5 and 7 respectively. The retired Frost Bolt Speed-debuff rule does not transfer to Ice Bolt: the current Ice Bolt explicitly changes neither Speed nor Initiative.

###### Leadership and army capacity

Leadership is a Secondary Attribute. Every army slot is checked independently: Maximum creatures in a stack = floor(Hero Leadership / Creature Leadership Requirement). There is no shared army-wide Leadership budget.

Command does not increase Leadership capacity, and Recruitment never reduces creature Leadership requirements. Percentage discounts to Leadership costs are avoided so stackcapacity breakpoints remain transparent.

###### Skill and perk development

Skills have three ranks: **Basic, Advanced, and Expert** . Each Skill also has its own perk pool. A hero may learn at most **three perks from any one Skill** .

When a hero gains a level, the level-up may present up to **two Skill choices** and up to **two perk choices** . The hero selects one of the offered options.

###### **Normal progression**

Under ordinary level-up advancement, Skill ranks and perks alternate:

**Basic Skill → Basic Perk → Advanced Skill → Advanced Perk → Expert Skill → Expert Perk**

Accordingly:

- A hero must first learn a Skill at Basic rank before perks from that Skill can normally be offered.

- Before Advanced rank can normally be offered, the hero must have learned at least one Basic perk from that Skill.

- Before Expert rank can normally be offered, the hero must have learned at least one Advanced perk from that Skill.

- Expert perks become available only after the Skill has reached Expert rank.

This creates a deliberate rhythm of **learn → specialize → advance → specialize** . Increasing a Skill's rank therefore requires some investment in the Skill's perk structure rather than allowing a hero to rush directly from Basic to Expert while ignoring its perks.

Each Skill has three perk tiers: Basic, Advanced, and Expert. A hero may acquire exactly one perk from each tier. Once a perk has been selected from a tier, the remaining perks of that tier become unavailable to that hero. Skill-rank advancement and perk-tier advancement normally alternate. Exceptional Skill-rank advancement does not bypass the required perk-tier order.

“Exactly one” describes a fully developed Skill: partially developed Skills may have zero, one, or two perks. The ten-perk pool contains alternatives, not ten eventual acquisitions. A hero cannot select the same perk twice or acquire a second perk from an occupied tier. A fully developed Skill therefore has one Basic, one Advanced, and one Expert perk.

Only learned Skills with a legal next perk tier enter the perk-parent lottery. Two eligible parents supply distinct parents sampled without replacement; when only one eligible parent remains, offer one perk from that parent, not two alternatives. Choose the individual perk uniformly from that parent's legal next-tier alternatives. A Basic Skill with its Basic perk has no eligible perk until Advanced rank; an Advanced Skill with its Advanced perk waits for Expert rank. An exceptionally granted Expert Skill without perks must still acquire Basic, Advanced, and Expert perks in order.

###### **Exceptional Skill advancement**

Some external effects may advance a Skill without following the normal level-up sequence. A Witch Hut, scenario reward, map object, scripted effect, or other explicitly defined source may, for example, raise a Skill from Basic to Advanced even if the hero has not yet learned a Basic perk.

Such advancement changes the hero's **Skill rank** , but it does not skip the hero's **perk progression** .

Perks are always offered in tier order:

###### **Basic perks → Advanced perks → Expert perks**

Therefore, if an external effect raises a hero directly to Advanced rank while that hero has no perk from the Skill, the next perk offered from that Skill must be a Basic perk. Only after the hero has learned a Basic perk may Advanced perks from that Skill begin to appear.

Likewise, if an exceptional effect raises a Skill to Expert before the hero has completed its earlier perk progression, missing perk tiers must still be filled in order. An Expert-ranked hero with no perks from that Skill would first be offered a Basic perk, then an Advanced perk, and only then an Expert perk.

External advancement therefore bypasses **rank prerequisites** , not **perk order** .

Every source that teaches or advances a Skill uses the live New Horizons Skill registry and current class, faction, slot, rank, and perk-order legality. When interaction is optional, ask whether the hero wishes to learn or advance the Skill; an already Expert hero receives an explanation instead of a redundant grant. Exceptional sources may advance rank as defined above, but never grant an arbitrary perk or erase a missing perk-tier choice. Building-specific replacements such as House of Wisdom scroll sales take precedence over generic teaching behavior.

###### **Perk eligibility**

Unless a perk explicitly states otherwise:

- Basic perks require at least Basic rank in their Skill.

- Advanced perks require at least Advanced rank.

- Expert perks require Expert rank.

Skill rank determines which perk tiers the hero is capable of learning; perk progression determines which of those eligible tiers may be offered next.

For example, an Expert-ranked hero who has only a Basic perk is mechanically qualified for Basic, Advanced, and Expert-tier content by Skill rank, but their next new perk from that Skill must still come from the Advanced tier. The Expert perk tier remains locked until that step has been completed.

###### **Passive Perk Principle**

Perks are passive modifications to systems the hero or army already possesses.

A perk may **alter, extend, or react to an existing action** , but it must never itself constitute a new action, toggle, mode, targetable ability, or discretionary activation.

A perk may modify a Spell, Order, attack, retaliation, movement, Creature Activation, Wait, Defend, strategic interaction, or other established mechanic. It may also react automatically to a defined event or automatically grant an additional use of an existing action.

The player may make decisions while performing the underlying action. What the perk may not do is introduce a separate decision about whether or how to activate the perk itself.

Perks must not conditionally transform one Spell into another based on cast count, combat state, Mana affordability, target state, or any similar runtime condition. Effects such as “the first Bless each combat becomes Mass Bless” are not permitted.

A perk may instead grant a distinct Spell or Spell variant as a permanent addition to the hero’s normal spellcasting options. The player then casts that Spell through the ordinary spellcasting interface. The perk grants access to the option; it does not decide when another Spell changes into it.

|Type|Example|Status|
|---|---|---|
|Static modifier|“Ranged attacks ignore 20% of the Distance penalty.”|Passive|
|Automatic trigger|“When this stack retaliates, it gains +2 Defense until its next activation.”|Passive|
|Automatic action grant|“After casting with the Hero Action, gain a Spell Action.”|Passive|
|Player-triggered perk|“Once per combat, activate this perk to…”|Not permitted|
|Optional trigger|“After casting a Spell, you may spend 5 Mana to…”|Not permitted|
|Mode or toggle|“Choose Empowered or Extended casting.”|Not permitted|
|New targeted ability|“Select one friendly stack to…”|Not permitted|
|Choice prompt|“When X happens, choose one of…”|Not permitted|



A usage limit does not make an activated effect passive. **“Once per combat”** is acceptable when it limits an automatic trigger; it is not acceptable when it creates a resource that the player chooses when to activate.

The governing distinction is:

###### **The player may make decisions through existing game actions; a perk must not become a new action or decision point of its own.**

###### Magic access and strategic casting

Without a School Skill, a hero may learn Levels 1-2 spells of that school. Basic / Advanced / Expert school proficiency unlocks Levels 3 / 4 / 5. Wisdom reduces the Mana cost of ordinary spells; it does not unlock spell levels.

Adventure Spells are neutral strategic magic. Their fixed Mage Guild unlocks are separate from ordinary spell availability, Wisdom does not discount their Mana cost, and a hero may cast at most one Adventure Spell per day.

###### Rounding and percentage language

Whenever a rule says percentage points, the value is added directly to a percentage. Ordinary percentage modifiers scale a value. Spell Mana costs round up after percentage discounts and may not fall below 1 Mana; Leadership-based stack capacity rounds down. Other mechanics state their rounding rule explicitly when integer conversion matters.

###### Damage mitigation and magic resistance

New Horizons keeps three defensive concepts separate. This mirrors useful distinctions already <u>present in Heroes III rather than collapsing every anti-damage mechanic into one statistic.</u>

|**Mechanic**|**What it does**|**Reference model**|
|---|---|---|
|Physical Damage Reduction|Multiplies incoming physical creature<br>damage after the Creature<br>Attack/Defense calculation. Independent<br>sources stack multiplicatively.|Original Shield / Air Shield and Armorer-<br>style direct reduction.|
|Magical Damage Reduction|Reduces magical damage after a spell or magical ability successfully affects the stack. It does not prevent non-damaging magical effects.|Golem-style spell-damage reduction.|
|Magic Resistance|A percentage chance to reject a hostile spell completely. A resisted spell produces no effect on that stack.|Dwarf/Battle Dwarf-style resistance chance.|



Creature Defense remains the intrinsic defensive statistic in ordinary physical combat. Armor is not a separate universal creature statistic. Effects named Armor Piercer, Armor-Piercing Shot, Shock Assault, and similar mechanics penetrate Creature Defense; Frailty strips Creature Defense. Physical Damage Reduction is a later multiplicative layer.

The current numerical damage formula, stacking limits, penetration rules, and Luck/Morale trigger curve are collected in Experimental Values.

# Orders

### Introduction

Normally, once per round, a hero uses their **Hero Action** to either cast one Spell or issue one Order. Specific abilities may explicitly permit an additional Spell or Order. Different active Orders coexist through their own normal durations, targets, effects, and expiry. Issuing a new different Order never replaces an earlier Order. This rule grants no additional action and does not permit an otherwise forbidden repeat of the same Order; all prerequisites, use limits, trigger timing, and authoritative validation still apply.

The command system should not replace the spell system, nor should it exist in isolation. Instead, both should coexist as two complementary ways for heroes to influence the battlefield. Every hero, regardless of class, would have access to both spells and commands, and once per round would choose between casting a spell or issuing an order. This preserves one of Heroes' defining characteristics: every hero can, in principle, use every core mechanic. What differentiates them is not exclusivity, but specialization.

Magic heroes continue to specialize in spellcasting through Wisdom, which remains their unique class Skill and reduces spell Mana costs, while the six Magic School Skills govern access to Levels 3-5 spells of their respective schools. Their superior Spell Power, Knowledge, and magical development make spells their natural choice in most situations. Might heroes, on the other hand, would possess a unique Command skill that enhances battlefield orders—whether by increasing their potency, duration, or some other aspect still to be determined. Their higher Attack and Defense would primarily be expressed through these commands rather than through passive bonuses.

Commands themselves would not consume mana or any equivalent resource. The only cost is the hero's action for the round. This ensures that the player always has a meaningful decision to make, without introducing another economy to manage. Commands are not magical effects, but military orders: they coordinate troops, alter formations, direct aggression, reinforce discipline, or exploit positioning. By contrast, magic remains responsible for supernatural phenomena such as teleportation, resurrection, summoning, curses, elemental manipulation, and similar effects.

An important design goal is to avoid simply inverting the current Heroes III dynamic. The game should not become one where Magic heroes always cast spells and Might heroes always issue commands. Instead, each system should be attractive enough that both hero archetypes occasionally make use of it. A Knight should sometimes decide that casting Slow or Dispel is tactically superior to issuing an order. Likewise, a Wizard should occasionally conclude that ordering a coordinated Charge or Focus Fire is more valuable than spending mana on another spell. The distinction, therefore, lies not in what heroes are allowed to do, but in what they naturally excel at.

Finally, hero classes themselves should not determine which commands are available. Just as every spellcaster draws from the same spell pool, every commander should draw from the same set of battlefield orders. What personalizes a commander is not a unique command list, but the combination of Skills they develop. Skills such as Offense, Armorer, Leadership, Logistics, Archery, and Tactics can each strengthen different aspects of the command system, allowing players to shape distinct command styles through character development rather than through rigid class restrictions. This keeps the system consistent with Heroes III's philosophy while giving Might heroes an active, engaging gameplay loop that parallels—but does not imitate—the spellcasting of Magic heroes.

|Order|Canonical wording|Formula|
|---|---|---|
|Charge!|Until the end of the round, each friendly stack's first melee attack made after voluntarily moving at least 3 hexes during that activation gains bonus damage. Each hex moved beyond the third before the attack further increases the bonus.|Damage Bonus = 10% + 0.20% × A + 2% × extra hexes beyond 3|
|Focus Fire!|Select one enemy stack. Until the end of the round, friendly shooters deal increased damage to that stack and halve range and obstacle penalties when attacking it.|Damage Bonus = 5% + 0.15% × A|
|Riposte!|Until the end of the round, friendly stacks take reduced melee damage and deal increased retaliation damage.|Damage Reduction = 5% + 0.10% × D • Retaliation Damage Bonus = 5% + 0.15% × A|
|Hold the Line!|Until the end of the round, friendly stacks take reduced physical damage while remaining in the position they occupied when the Order was issued. Leaving that position ends the effect for that stack.|Damage Reduction = 10% + 0.20% × D|
|Brace!|Until the end of the round, when a friendly stack is attacked in melee by an enemy that voluntarily moved at least 3 hexes immediately before attacking, the defender makes a pre-emptive attack before the enemy attack resolves. This does not consume a normal retaliation.|Pre-emptive Attack Damage = (50% + 0.25% × D) of normal damage|
|Protect!|Choose two adjacent friendly stacks: a Protector and a Ward. Until the end of the round, the first melee attack against the Ward is redirected to the Protector. The Protector receives the attack and may retaliate normally. If the two stacks cease to be adjacent before the interception occurs, the effect ends.|Intercepted Damage Reduction = 5% + 0.15% × D|
|Flank!|Select one enemy stack. Until the end of the round, friendly melee attacks against it deal increased damage. The bonus increases as attacks are made against the target from additional distinct sides during the Order's duration.|Damage Bonus = 8% + 0.12% × A + 4% × each additional distinct side|
|Second Wind!|Target one friendly stack that has already completed its normal Creature Activation this round. It immediately receives one additional Creature Activation. Only direct damage dealt during this additional activation is reduced.|Second Wind Damage = 50% + k × Leadership|



# Magic

### Introduction

After several iterations, I settled on six schools of magic that I believe are broad enough to encompass the entire spell system while still giving each faction a distinct magical identity: **Light**, **Nature**, **Sorcery**, **Havoc**, **Shadow**, and **Chaos**.

Rather than organizing magic around the classical elements as Heroes III did, or around the four schools of Heroes V, this system attempts to classify magic according to its underlying nature.

Sorcery represents the study and manipulation of magic itself, encompassing metamagic, teleportation, illusions, arcane enhancement, and other techniques that reshape magical and spatial relationships.

Light governs divine miracles, healing, blessings, protection, and purification. Shadow embodies necromancy, curses, corruption, fear, and the manipulation of life and death. Nature represents the living world—plants, beasts, regeneration, poison, and druidic magic. Havoc is the school of raw destructive forces, including elemental devastation, fire, lightning, earthquakes, and catastrophic magical power.

Chaos governs disorder itself, manipulating minds, emotions, madness, chance, and reality through spells such as Berserk, Blind, Hypnotize, Forgetfulness, and similar effects.

Each town is associated with exactly two schools, and no combination is repeated. Castle combines Light and Sorcery, emphasizing divine magic supported by disciplined arcane study. Rampart draws from Nature and Light, reflecting its druids and affinity with the living world. Tower specializes in Sorcery and Havoc, mastering both pure arcane theory and offensive spellcraft. Inferno wields Chaos and Havoc, representing demonic destruction and madness. Necropolis combines Shadow and Sorcery, focusing on necromancy, despair, and corruption. Dungeon pairs Shadow with Havoc, portraying aggressive warlocks whose destructive magic is rooted in forbidden powers rather than scholarly arcane knowledge. Stronghold combines Nature and Chaos through its shamans, spirits, and primal fury, while Fortress blends Nature and Shadow, expressing the poisonous, decaying, and swamp-like aspects of the natural world. Finally, Conflux unites Nature and Havoc, representing the untamed power of storms, volcanoes, floods, and the elemental forces of the world.

This arrangement results in a balanced distribution. Nature and Havoc each appear in four factions, making them the most widespread traditions. Shadow and Sorcery each appear three times, while Light and Chaos appear twice each. Tower is the most magic-centered town overall: its Sorcery/Havoc identity, hero development, and unique arcane infrastructure should make it the clearest specialist in formal magic. Conflux is its closest rival in magical infrastructure, especially through the House of Wisdom and its spell-scroll economy. Castle remains the principal center of Light magic rather than an equal general-purpose arcane center.

Out of the fifteen possible pairings between six schools, nine are represented by the factions. The six unused combinations are Light–Chaos, Light–Shadow, Light–Havoc, Nature–Sorcery, Sorcery–Chaos, and Shadow–Chaos. These remain available for future expansion, additional factions, or unique hero specializations without forcing overlap among the existing towns. The result is a system in which every faction has a distinct magical identity while leaving room for future growth.

### Spell Schools per town

|Town|Preferred School A|Preferred School B|
|---|---|---|
|Castle|Light|Sorcery|
|Rampart|Nature|Light|
|Tower|Sorcery|Havoc|
|Inferno|Chaos|Havoc|
|Necropolis|Shadow|Sorcery|
|Dungeon|Havoc|Shadow|
|Stronghold|Chaos|Nature|
|Fortress|Nature|Shadow|
|Conflux|Havoc|Nature|



### Spell List

#### ☀️ LIGHT

- Level 1: Cure; Bless; Shield of Faith (new)
- Level 2: Guardian Spirit (temporary HP); Holy Armor (new magical protection, not Defense)
- Level 3: Resurrection; Holy Wrath
- Level 4: Divine Shield (immune to negative magic); Purify (mass dispel + cures)
- Level 5: Guardian Angel; Divine Intervention

#### 🌑 SHADOW

- Level 1: Curse; Disease (new); Life Drain
- Level 2: Blind; Weakness
- Level 3: Animate Dead; Death Ripple; Plague
- Level 4: Vampirism
- Level 5: Soul Reaper; Apocalypse of Death (new)

#### 🌳 NATURE

- Level 1: Regeneration; Entangle; Barkskin
- Level 2: Poison; Summon Wolves
- Level 3: Summon Treants; Thorn Armor
- Level 4: Quicksand; Nature's Wrath
- Level 5: Ancient Awakening; World Tree's Blessing

#### 🔥 HAVOC

- Level 1: Fireball; Ice Bolt; Lightning Bolt
- Level 2: Inferno; Frost Ring
- Level 3: Chain Lightning; Meteor Shower
- Level 4: Implosion; Fire Wall
- Level 5: Armageddon; Cataclysm

#### 💎 SORCERY

- Level 1: Magic Arrow; Dispel; Slow
- Level 2: Storm of Daggers; Transfigure Matter
- Level 3: Teleport; Focus Magic
- Level 4: Phantom Army; Implosion
- Level 5: Time Stop; Spell Lock

#### 🌀 CHAOS¹

- Level 1: Misfortune; Hallucination; Wild Surge (new)
- Level 2: Berserk; Forgetfulness
- Level 3: Confusion; Hypnotize
- Level 4: Puppet Master; Doom
- Level 5: Reality Warp; Wild Magic

¹ https://lordsofmagic.wordpress.com/chaos-spells/

Light

###### ☀️� LIGHT ☀️�

Light specializes in preserving life, protecting the faithful, cleansing corruption, and empowering an army through divine miracles.

Assume:

SP = Hero Spell Power

**Magical Damage Reduction** reduces magical damage after the spell successfully affects the creature. It is distinct from **Spell Resistance** , which may prevent a hostile spell from affecting the target at all.

When multiple applicable effects ignore a fraction of Magical Damage Reduction,
combine their penetration independently, not additively or strongest-only:
`combined penetration = 1 - product(1 - individual penetration)`.
Thus20% and15% combine to32% penetration, leaving68% of the target's original
Magical Damage Reduction. This shared rule applies to Spell Penetration,
Combat Casting, Overwhelming Formula, Focused Pairing, Annihilator and other
effects using the same penetration vocabulary; it does not bypass immunity or
Spell Resistance.

|Level|Spell|Mana|Philosophy / Effect|
|---|---|---|---|
|1|**Cure**|4|Single-target healing + removal of one physical affliction|
|1|**Bless**|5|Target always rolls maximum natural damage|
|1|**Sanctuary**|5|Sacred refuge; protects a passive stack from deliberate<br>targeting|
|2|**Guardian Spirit**|8|Temporary HP against physical creature damage|
|2|**Holy Armor**|8|Strong single-target Magical Damage Reduction|
|3|**Holy Wrath**|11|Divine direct damage; especially effective against<br>Undead and Demons|
|3|**Heavenly Gale**|13|Army-wide protection against ranged attacks|
|4|**Divine**<br>**Retribution**|16|Enemies that harm the protected stack suffer divine<br>judgment|
|4|**Purify**|15|Area-based removal of negative effects|
|5|**Resurrection**|22|Powerful restoration of actual casualties|
|5|Crusade!|24|Massive army-wide Prayer: Attack, Defense, Initiative and Magical Damage Reduction|



###### **Level 1 — Cure**

Target: one friendly living stack. Healing = 25 + 1.5 × SP

Also removes **one physical affliction** chosen by the player.

Examples:

- Poison

- Disease

- Bleeding

- similar bodily conditions

It does not remove magical effects such as Curse, Slow, or Berserk.

It cannot resurrect casualties.

###### **SP Healing**

|SP|Healing|
|---|---|
|20|55|
|50|100|
|100|175|
|150|250|

###### **Level 1 — Bless**

Target: one friendly stack.

The target always rolls its **maximum natural creature damage** . A creature with:

10–15 Damage

always rolls:

15

before subsequent modifiers.

Duration:

Duration = min(4, 2 + floor(SP / 80))

###### **SP Duration**

|SP|Duration|
|---|---|
|0–79|2 rounds|
|80–159|3 rounds|
|160+|4 rounds|

No additional damage multiplier.

###### **Level 1 — Sanctuary**

Target: one friendly stack.

The stack becomes **Sanctified until its next activation** .

While Sanctified:

- enemy creatures cannot deliberately target it with attacks;

- hostile single-target spells cannot select it;

- area spells may still hit it;

- battlefield-wide effects may still affect it;

- indirect effects still function normally.

Sanctuary immediately ends if the creature:

- moves;

- attacks;

- uses an offensive active ability.

Waiting and Defending do not break Sanctuary.

No SP scaling.

##### **Level 2 — Guardian Spirit**

Target: one friendly stack.

Creates a separate protective HP pool:

Spirit HP = 50 + 2 × SP

Guardian Spirit absorbs **physical creature damage** before actual stack HP is lost.

It protects against:

- melee;

- ranged attacks;

- retaliation;

- physical creature abilities.

Spell damage bypasses it.

Duration:

2 rounds

or until exhausted.

###### **SP Spirit HP**

|SP|Spirit HP|
|---|---|
|20|90|
|50|150|
|100|250|
|150|350|

##### **Level 2 — Holy Armor**

Target: one friendly stack.

Provides:

Magical Damage Reduction = min(60%, 30% + 0.20% × SP)

Duration:

2 rounds

###### **SP Magical Damage Reduction**

|SP|Magical Damage Reduction|
|---|---|
|20|34%|
|50|40%|
|100|50%|
|150+|60%|

Holy Armor reduces **magical damage only** .

It does not prevent Curse, Slow, Berserk, Puppet Master, or other non-damaging magical effects.

##### **Level 3 — Holy Wrath**

Target: one enemy stack.

Damage = 40 + 2 × SP

Under version-3 School-rank rules, Light's no-rank / Basic / Advanced /
Expert coefficient (100% / 115% / 130% / 145%) strengthens only the
`2 × SP` term before the final target-classification multiplier. An eligible
Warcasting bonus affects that same Spell Power term. The fixed 40 damage and
the 1.5× classification multiplier do not increase with School rank.

Against **Undead or Demonic** creatures:

Final Damage = Damage × 1.5

For the current creature roster, **Demonic** means an Inferno-origin creature,
including its upgraded form. This is a creature classification, not a test of
the defending hero's faction or of which army currently commands the stack.
Undead is the creature's existing Undead trait. A creature qualifying through
both classifications receives the multiplier only once.
The ordinary per-source damage-received cap, if present, applies after this
classification multiplier, just as it does for other spell-damage bonuses.

###### **SP Normal Undead / Demon**

|SP|Normal|Undead / Demon|
|---|---|---|
|50|140|210|
|100|240|360|
|150|340|510|

No secondary effect.

Holy Wrath gives Light legitimate offensive power without approaching Havoc's general destructive efficiency.

##### **Level 3 — Heavenly Gale**

Target: entire friendly army.

A supernatural wind deflects physical missiles.

Ranged Damage Reduction = min(80%, 50% + 0.15% × SP)

Duration:

2 rounds

###### **SP Ranged Damage Reduction**

|SP|Ranged Damage Reduction|
|---|---|
|20|53%|
|50|57.5%|
|100|65%|
|150|72.5%|
|200+|80%|

Protects against:

- arrows;

- bolts;

- thrown weapons;

- stones;

- physical siege projectiles;

- ordinary physical ranged attacks.

Does not protect against:

- melee attacks;

- spell damage;

- magical beams;

- area explosions;

- non-projectile magical attacks.

It is deliberately powerful but matchup-dependent.

## **Level 4 — Divine Retribution**

Target: one friendly stack.

Duration:

2 rounds

Whenever an enemy creature damages the protected stack, that attacker becomes **Judged** . At the end of the round:

Retribution Damage = min(30% × Damage Dealt, 25 + 1.25 × SP)

Each attacker may trigger Divine Retribution only once per round.

At SP 100:

Maximum Retribution = 150 Holy Damage

Examples:

Enemy deals 200 damage:

200 × 30% = 60 Holy Damage

Enemy deals 800 damage:

800 × 30% = 240

but:

Damage = 150 due to the cap.

Works against melee and ranged creature attacks.

Does not trigger from spells.

Divine Retribution provides **deterrence rather than mitigation** .

## **Level 4 — Purify**

Target: battlefield area.

**Radius:** 2 hexes.

Every friendly stack within the area may have negative effects removed.

Effects Removed = min(2, 1 + floor(SP / 120))

School rank scales only this Spell Power-derived term through the shared
100/115/130/145% ladder: `min(2, 1 + floor(SP × SchoolCoefficientPercent / 12000))`.
The fixed one, cap two, radius and target choices do not change. The table below
shows unranked values; the second choice begins at SP 105/93/83 for
Basic/Advanced/Expert. Purifier's separate physical-affliction removal is unchanged.

###### **SP Effects removed per affected stack**

|SP|Effects removed per affected stack|
|---|---|
|0–119|1|
|120+|2|

The player chooses which eligible effects to remove.

Purify may remove:

- Curse

- Slow

- Blind

- Berserk

- Poison

- Disease

- other temporary negative magical or bodily effects

It does not:

- heal;

- resurrect;

- remove positive effects;

- remove Orders;

- affect units outside the area.

Its efficiency therefore depends heavily on formation and positioning.

## **Level 5 — Resurrection**

Target: one friendly stack, including a completely destroyed stack whose remains are still available.

Restoration Pool = 100 + 4 × SP

The pool first heals the wounded surviving creature and then restores casualties.

The stack cannot exceed its battle-start creature count.

###### **SP Restoration Pool**

|SP|Restoration Pool|
|---|---|
|50|300 HP|
|100|500 HP|
|150|700 HP|
|200|900 HP|

Because Resurrection is now level 5, it deserves substantially stronger scaling than its previous level-3 version.

It restores **real creatures permanently for the battle** .

It cannot affect:

- summoned creatures;

- illusions;

- entities explicitly incapable of resurrection.

A completely destroyed stack reappears at its corpse location.

## **Level 5 — Crusade!**

Target: entire friendly army. The army is filled with divine fervor. All eligible friendly creatures receive:

Attack Bonus = min(6, 3 + floor(SP / 75)) Defense Bonus = min(6, 3 + floor(SP / 75)) Initiative Bonus = min(3, 1 + floor(SP / 100)) Magical Damage Reduction = min(25%, 12% + 0.065% × SP) Duration:

3 rounds

Affected creatures also **cannot suffer negative Morale** while Crusade is active.

School rank strengthens only the Spell Power-derived terms in these formulas:
use the shared 100% / 115% / 130% / 145% coefficient before each term's
final rounding. Fixed bases and caps do not scale. Initiative is a flat bonus,
not a movement-Speed bonus. Preserve fractional Magical Damage Reduction in
basis points; round down only after the scaled `0.065% × SP` component is
calculated. Crusader extends the entire effect to four rounds; its repeated
negative-Morale protection wording does not grant an additional separate bonus.

|SP|Attack|Defense|Initiative|Magical Damage Reduction|
|---|---|---|---|---|
|50|+3|+3|+1|15.25%|
|100|+4|+4|+2|18.5%|
|150|+5|+5|+2|21.75%|
|200|+5|+5|+3|25%|
|225+|+6|+6|+3|25%|



Magical Damage Reduction combines multiplicatively with other sources.

For example:

Holy Armor 50% + Crusade 20%:

Damage × 0.50 × 0.80 = 40%

So the creature suffers 40% of the original magical damage, equivalent to **60% total reduction** , rather than 70%.

Crusade is Light's supreme **empowerment** spell, while Resurrection is its supreme **restoration** spell.

The finished functional profile is therefore:

|**Function**|**Light spell**|
|---|---|
|Healing|**Cure**|
|Damage blessing|**Bless**|
|Tactical refuge|**Sanctuary**|
|Physical damage buffer|**Guardian Spirit**|
|Magical damage protection|**Holy Armor**|
|Direct divine offense|**Holy Wrath**|
|Anti-ranged defense|**Heavenly Gale**|
|Punitive protection|**Divine Retribution**|
|Area cleansing|**Purify**|
|Casualty restoration|**Resurrection**|
|Supreme army<br>empowerment|**Crusade!**|



This version has a particularly good level progression: Light begins by **saving individuals** , progresses toward **protecting formations** , and culminates in miracles that can **restore or exalt an entire war effort** .

Shadow

###### **🌑 SHADOW 🌑**

School of malediction, stolen vitality, supernatural affliction, sacrifice, reanimation, and execution. Shadow rarely competes with Havoc through immediate raw damage. Instead, it weakens creatures, turns their own actions against them, spreads suffering between targets, trades life for power, and becomes increasingly dangerous as casualties accumulate.

The following numbers are prototype values, but the mechanical identities are the important part.

|Level|Spell|Mana|Proposed formula / rules|
|---|---|---|---|
|1|**Curse**|4|Target always rolls minimum creature damage for 3 rounds|
|1|**Life Drain**|5|Damage = 25 + 1.8×SP; heal one friendly stack for 60% of<br>actual damage dealt|
|1|**Sorrow**|4|Morale penalty = min(3, 1 + floor(SP / 70))for<br>3 rounds|
|2|**Hex of Pain**|8|Whenever target attacks or retaliates, it suffers reactive<br>Shadow damage|
|2|**Frailty**|8|Permanently strips a cumulative portion of the target's<br>Creature Defense for the battle|
|3|**Plague**|11|Magical damage-over-time that propagates between adjacent<br>stacks|
|3|**Soul Chain**|12|Links several enemies; damage suffered by secondary victims<br>echoes into the primary victim|
|3|**Shadow**<br>**Gift**|12|Sacrifice part of a friendly stack's vitality to grant a large<br>temporary offensive bonus|
|4|**Vampirism**|15|Target heals from damage it inflicts|
|4|**Re-animate**|16|Restores dead creatures very efficiently, but restored creatures<br>are temporary|
|5|**Soul**<br>**Reaper**|21|Execution spell whose damage increases with the target's<br>missing HP|
|5|**Doom**|25|Extremely powerful single-target compound curse crippling<br>damage, Initiative, movement, retaliation and Morale|



###### **Curse**

The classic spell should remain extremely simple.

For **3/4/4/5 rounds** at no Shadow School rank / Basic / Advanced / Expert,
the affected creature always rolls the minimum value of its normal damage range.
Apply Malediction and other explicit duration extensions afterward, once.
The explicit Mass Curse variant uses the same duration table; School rank does
not itself grant that variant or change its targets. Previously captured rules
without this duration table retain their fixed three-round base.

Thus a creature dealing:

10–20 damage

behaves as though its damage were:

10–10

Curse does not modify Attack, Creature Defense, or any other statistic.

That preserves a distinctive Heroes mechanic while preventing overlap with Orders.

###### **Life Drain**

Select one enemy stack and one friendly stack.

Damage = 25 + 1.8 × SP

The enemy suffers the Shadow damage normally.

The friendly target then recovers:

Healing = 60% of actual damage inflicted

Example at SP 100:

Damage = 205 Healing = 123

Life Drain **cannot restore dead creatures** . It repairs injuries among the surviving members of the stack.

That distinction is important because it prevents Life Drain from becoming a miniature Reanimate.

It works identically on every faction and every creature type. Shadow is stealing vitality, not performing Undead-specific magic.

###### **Sorrow**

Sorrow attacks morale directly.

Morale Penalty = min(3, 1 + floor(SP / 70))

Duration: **3 rounds** .

Thus:

SP 0–69   →−1 Morale SP 70–139 →−2 Morale SP 140+   →−3 Morale

− Sorrow cannot reduce Morale below 10.

Its role is simple: Shadow can attack the supernatural courage and resolve of an army without turning into Chaos-style mind control.

###### **Hex of Pain**

For **3 rounds** , whenever the afflicted creature performs an attack or retaliation, the curse triggers after its attack resolves.

Pain Damage = 15 + 0.7 × SP + 10% of damage just inflicted

At SP 100, a creature that deals 400 damage suffers:

15 + 70 + 40 = 125 Shadow damage

The important part is the decision it creates.

The curse does not prevent the creature from acting.

It merely makes aggression painful.

A powerful enemy stack can continue attacking normally—but the stronger its attacks are, the more painful Hex of Pain becomes.

Damage caused by Hex of Pain cannot itself trigger additional reactive effects recursively.

###### **Frailty**

Frailty permanently erodes the target's physical protection during the current battle.

Each cast removes:

Creature Defense Loss = min(20%, 10% + 0.05% × SP)

of the target's **base Creature Defense** .

At SP 100:

15% base Creature Defense removed per cast

The reduction is cumulative:

1 cast →−15% 2 casts →−30% 3 casts →−45%

Maximum cumulative reduction:

60% of base Creature Defense

There is **no duration counter** . Once stripped, that Creature Defense does not naturally return during the battle.

I would still classify Frailty as a magical curse, meaning Dispel can remove its accumulated effect. “Permanent” here means battle-long rather than intrinsically uncleanseable.

Frailty is therefore particularly attractive when the Shadow caster expects to concentrate attacks on one heavily armored target over several rounds.

###### **Plague**

Plague should indeed spread, but it should not simply be Shadow's version of Nature's Poison.

Nature's Poison is a straightforward biological damage-over-time effect.

###### **Plague is a contagious supernatural affliction.**

Cast it on one stack.

At the end of the afflicted stack's turn:

Plague Damage = 25 + 0.8 × SP

Then Plague attempts to spread to **one adjacent creature stack** that is not already infected.

The spread does not distinguish between friend and foe.

If several valid adjacent stacks exist, the victim can be selected deterministically according to proximity/hex ordering rather than randomly.

Each newly infected stack receives its own normal duration:

Duration = 3 rounds

Example at SP 100:

105 damage per infected stack per round

The potentially explosive value therefore comes from **propagation** , not high individual damage.

It affects all ordinary creature types. The plague is magical corruption rather than literal infection, so Constructs, Elementals, Undead, Demons, etc. do not receive faction-specific immunity merely because of their biology.

This also gives positioning counterplay: separate infected troops before the contagion reaches something important.

###### **Soul Chain**

Soul Chain creates one primary victim and several secondary victims.

Select:

1 primary enemy stack

- + up to 2 secondary enemy stacks

For **2 rounds** , whenever one of the secondary stacks suffers damage, part of that damage is echoed onto the primary target as Shadow damage.

Echo = min(40%, 20% + 0.10% × SP)

At SP 100:

30% Echo

If a chained secondary creature receives 500 damage:

Primary victim additionally suffers 150 Shadow damage

Damage inflicted by Soul Chain itself cannot trigger Soul Chain again.

The secondary victims do not share damage with one another. Everything flows toward the designated primary victim.

That creates the intended tactical pattern:

You want to kill the Dragon? Chain its soul to the two weaker stacks standing beside it and attack them instead.

It is multi-target damage, but distinctly Shadow rather than Havoc: the caster creates a malicious relationship between creatures rather than simply blasting an area.

###### **Shadow Gift**

Shadow Gift should make **maximum vitality an expendable resource** .

Select one friendly stack and choose one of three sacrifice levels:

10% 20% 30%

The chosen percentage is removed from both its **current aggregate HP** and its **maximum aggregate HP** for the remainder of the battle.

The target then receives an offensive enchantment for **3 rounds** .

Damage Bonus =

Sacrifice % × (1.25 + 0.005 × SP)

At SP 100:

|Sacrifice|Damage bonus|
|---|---:|
|10% HP|+17.5%|
|20% HP|+35%|
|30% HP|+52.5%|

The additional damage is treated as **Shadow damage** .

So the player decides how aggressively to bargain with the spell.

A nearly untouched Hydra might sacrifice 30% of its vitality to become terrifying for the next few attacks.

A wounded stack may only risk 10%.

Unlike an ordinary damage buff, Shadow Gift is not free military efficiency. The caster is literally exchanging one combat resource for another.

Maximum HP returns to normal after combat, but HP lost through the sacrifice remains genuine battle damage.

###### **Vampirism**

Vampirism grants universal lifesteal for **3 rounds** .

Lifesteal = min(50%, 25% + 0.15% × SP)

At SP 100:

40% lifesteal

If the enchanted stack deals 600 actual damage:

240 HP restored

The healing cannot restore creatures that have already died.

That rule matters because it keeps Vampirism focused on **sustaining an active attacker** , while Re-animate occupies the casualty-reversal niche.

There should be no Undead privilege here.

A Hydra, Devil, Minotaur, Wyvern, Cerberus, Vampire, or Skeleton all obey exactly the same rule.

###### **Re-animate**

Re-animate is Shadow's answer to Resurrection, but it should not be equivalent to it.

Its immediate battlefield efficiency is **higher** :

Restored HP = 220 + 5 × SP

At SP 100:

720 HP restored

It can restore creatures to a damaged stack or return a completely destroyed stack provided its remains still exist.

However, every creature restored by Re-animate is marked as **Reanimated** .

Reanimated creatures:

- fight normally;

- deal normal damage;

- use their normal abilities;

- may receive buffs and debuffs normally;

- count as part of the stack during combat;

###### ● **disappear when the battle ends** .

Thus:

###### **Resurrection preserves an army.**

###### **Re-animate wins the current battle.**

Because the restoration is temporary, Re-animate can safely restore substantially more HP per Mana than Resurrection.

The spell is also completely species-neutral. It can force a dead Hydra, Minotaur, Efreet, Wyvern, Devil, Skeleton, or any other eligible creature back into battle.

It is not “create Undead.”

It is:

###### **Death has occurred; Shadow refuses to let the body stop fighting yet.**

###### **Soul Reaper**

Soul Reaper is the school's dedicated finisher.

First calculate the target's missing aggregate HP:

− Missing HP = Maximum HP Current HP

Then:

Damage = 60 + 1.4 × SP + 0.40 × Missing HP Example at SP 100:

A creature stack began with 1,000 HP and currently has 400.

Missing HP = 600

Damage = 60 + 140 + 240 = 440 The remaining 400 HP is destroyed. Against the same stack at full health: Damage = 60 + 140

###### = 200

That is intentional.

Soul Reaper should be inefficient as an opening attack and terrifying against something already mauled.

I would also give it a hard execution clause:

If target remains at ≤10% of maximum HP after Soul Reaper's damage, destroy the remaining creatures.

This prevents irritating situations where the supposedly ultimate execution spell leaves one nearly dead creature standing.

Its identity is therefore the inverse of Implosion:

**Implosion prefers healthy, enormous targets.**

**Soul Reaper prefers targets already close to death.**

###### **Doom**

Doom is Shadow's ultimate malediction.

It does not deal direct damage.

Instead, one enemy stack is comprehensively crippled.

First determine:

Crippling Penalty = min(60%, 35% + 0.15% × SP)

At SP 100:

50%

For **3 rounds** , the target suffers:

- 50% damage dealt

- 50% Initiative

- 50% battlefield movement

- 50% retaliation damage

- 3 Morale

at SP 100.

At sufficiently high Spell Power the proportional penalties can reach their 60% cap.

Creature Defense is deliberately not reduced, because Frailty already owns that mechanic.

The spell is expensive:

25 Mana

and only affects **one stack** .

That is what permits the effect to be so extreme.

Doom should feel less like an ordinary debuff and more like choosing the most dangerous creature in the opposing army and saying:

###### **You are no longer allowed to function at full strength.**

Unlike Soul Reaper, Doom is strongest against a target that is still healthy and dangerous.

That gives the two Level 5 spells complementary uses:

**Soul Reaper** — destroy something that has already been broken.

**Doom** — break something that has not been broken yet.

Sorcery

###### 🌳 SORCERY 🌳

Jack-of-all-trades school: direct damage, multi-target damage, dispelling, debuffing, summoning/transmutation, relocation, arcane enhancement, illusionary replication, spatial damage/control, temporal control, and magical sealing.

I would formalize it around the rating system already established: Knowledge is the mana pool, while Spell Power is converted through spell-specific coefficients rather than multiplied into everything uniformly.

The following numbers are prototype values, but the mechanics I would already regard as fairly firm.

|**Level**|**Spell**|**Mana**|**Proposed formula / rules**|
|---|---|---|---|
|1|**Magic Arrow**|4+|Damage = (20 + 2×SP) × (1 +<br>0.15×Overcharge)|
|1|**Dispel**|4|Removes all temporary magical buffs and debuffs from<br>one stack|
|1|**Slow**|4|Initiative reduction = min(50%, 20% +<br>0.20%×SP)for 2 rounds|
|2|**Storm of**<br>**Daggers**|7|Select 1–5 enemies; distributes an expanding damage<br>pool among them|
|2|**Transfigure**<br>**Matter**|8|Converts one physical obstacle into temporary Diamond<br>Golems|
|3|**Teleport**|12|Moves one friendly stack to any legal battlefield position|
|3|**Focus Magic**|11|Enchants one friendly shooter; its ranged attacks place<br>Arcane Breach marks that cause subsequent friendly<br>ranged attacks to ignore Creature Defense.|
|4|**Phantom Army**|15|Creates a full offensive duplicate with limited phantom<br>integrity|
|4|**Implosion**|16|Deals percentage-of-current-HP damage and pulls<br>surrounding creatures|
|5|**Time Stop**|23|Places an area outside time until the beginning of the<br>caster's next Hero Action|
|5|**Spell Lock**|22|Seals a stack against all magic while preserving either its<br>buffs or debuffs|



###### **Magic Arrow**

This is exactly the spell where I would introduce an exceptional casting UI.

Base damage:

Base Damage = 20 + 2 × SP

After selecting the target, a compact centered Overcharge modal appears in the Heroes III leather, red, and gold visual style. It contains a segmented Mana slider, − and + controls, and live previews comparing total Mana cost, final damage, and estimated casualties with and without the selected Overcharge.

Maximum overcharge:

Max Overcharge = min(5, 2 + floor(SP / 50))

Each additional point of Mana increases the base result by 15%:

Final Damage = Base Damage × (1 + 0.15 × O)

where O is additional Mana spent.

Thus, at SP 100:

Base = 220

Maximum overcharge is 4.

```
4 Mana → 220 damage
5 Mana → 253
6 Mana → 286
7 Mana → 319
8 Mana → 352
```

That creates an excellent Sorcery decision. A Havoc spell has a defined destructive output. **Magic Arrow is adjustable.** The wizard decides how much power the situation is worth.

I would not make overcharging completely unlimited, because then Knowledge effectively becomes a second Spell Power statistic and a gigantic mana pool can delete a champion in one cast.

###### **Dispel**

I prefer your simplified version to the magnitude contest I suggested previously.

One target, friend or foe:

###### **Remove every temporary magical buff and debuff.**

No Spell Power formula is necessary.

Crucially, it does not remove Orders, innate creature states, mundane poison, terrain, summoned creatures, or other effects merely because magic was involved in creating them.

So if an enemy has Bless + Haste + Curse + Poison, and Poison is defined as biological rather than magical:

Dispel removes Bless, Haste, and Curse. Poison remains.

This preserves the distinction between magic and the Command system.

###### **Slow**

Since Advance already addresses battlefield movement, Slow should affect **initiative only** .

Reduction = min(50%, 20% + 0.20% × SP)

Duration: **2 rounds** .

− At SP 20: 24% Initiative. − At SP 50: 30%. − At SP 100: 40%. − At SP 150: 50% cap.

I prefer a percentage here because it remains meaningful across creatures with different Initiative ratings.

It cannot reduce Initiative below the system's minimum legal value.

###### **Storm of Daggers**

This should reward selecting several targets without becoming five simultaneous Magic Arrows. Let N be the number of selected enemy stacks, from 1 to 5.

First calculate:

Base Pool = 45 + 1.5 × SP Then: `Total Damage = Base Pool × [1 + 0.15 × (N − 1)]` The pool is divided evenly between the chosen stacks. At SP 100:

|Targets|Total|Per target|
|---:|---:|---:|
|1|195|195|
|2|224|112|
|3|254|85|
|4|283|71|
|5|312|62|

So selecting more targets increases total magical output, but sacrifices concentration.

That is a much better Sorcery identity than a conventional AoE: **the wizard explicitly chooses which creatures the spell recognizes as targets.**

No friendly fire.

The targeting UI should allow clicking up to five enemy stacks, with a small numbered marker showing the current selection and projected damage on each.

###### **Transfigure Matter**

Conceptually, I like this a great deal. It gives Sorcery summoning without stepping into Nature: the wizard does not summon life; he **restructures existing matter into an artificial creature** .

The problem is availability. Some battlefields may simply contain no valid obstacles. I would therefore make sure your battlefield-generation rules provide transfigurable scenery often enough that this is not a dead Mage Guild spell.

For an obstacle occupying S battlefield hexes:

Diamond Golem HP Pool = 80 + 2 × SP + 50 × S

Sorcery rank strengthens only the `2 × SP` component of this HP Pool using
the shared no-rank / Basic / Advanced / Expert coefficient ladder. It does not
change obstacle eligibility, target count, the fixed or footprint HP terms,
or Matter Shaper's separate bonus.

The obstacle disappears and that much total Diamond Golem health is created in its location.

If Diamond Golems eventually have G HP each:

Creature count = ceil(HP Pool / G)

with the final creature allowed to begin partially damaged.

At SP 80, transforming a two-hex obstacle produces:

80 + 160 + 100 = 340 HP

worth of Diamond Golems.

They should be temporary combat summons and disappear afterward. Otherwise Sorcery becomes an adventure-map creature-production engine.

I would exclude city walls, gates, moats, corpses, magical obstacles, and artifacts. Ordinary rocks, statues, rubble, crystal formations, trees if you consider them matter rather than living targets, etc. are valid according to whatever obstacle taxonomy you settle on.

###### **Teleport**

At level 3 and 12 Mana, I would simply make it **battlefield-wide** .

Select friendly stack → select any legal destination.

It ignores intervening creatures, terrain, walls, and obstacles.

It does not grant movement, alter Initiative, refresh retaliation, or grant another Creature Activation.

No Spell Power formula is needed. Its strength is already determined by the importance of the relocated stack.

That also avoids turning every spell into an obligatory coefficient exercise.

###### **Focus Magic**

Focus Magic magically attunes a shooter to the structural weaknesses of whatever it strikes. It does not directly increase ranged damage.

Target:

one friendly stack capable of making ranged creature attacks.

Duration:

###### 3 rounds

Whenever the enchanted stack deals damage with a ranged creature attack, the damaged enemy stack gains one **Arcane Breach** mark after that attack resolves.

Maximum:

3 Arcane Breach marks per stack

Each Arcane Breach causes subsequent friendly ranged creature attacks against that stack to ignore part of its Creature Defense.

###### **Creature Defense Penetration per Mark = min(20%, 10% + 0.05% × SP)**

Under the version-3 School-rank rule, Sorcery's no-rank / Basic / Advanced /
Expert coefficient of 100% / 115% / 130% / 145% strengthens only the
`0.05% × SP` component. The fixed 10% base, 20% per-mark cap, three-mark
limit, and duration do not change. An eligible Warcasting bonus also applies
to that Spell Power component; both coefficients are applied before its final
integer rounding. Older saved magic-rule profiles retain the unranked formula.

Thus:

SP 20 → 11% per mark → 33% at 3 marks SP 50 → 12.5% per mark → 37.5% at 3 marks SP 100 → 15% per mark → 45% at 3 marks

SP 150 → 17.5% per mark → 52.5% at 3 marks SP 200+ → 20% per mark → 60% at 3 marks

Arcane Breach does not reduce the target's actual Creature Defense. It causes qualifying ranged attacks to ignore a portion of that Defense. Melee attacks receive no benefit.

Arcane Breach marks last for 2 rounds. Applying another mark refreshes the duration of all existing Arcane Breach marks on that stack to 2 rounds.

Arcane Breach is a temporary magical debuff and may be removed by Dispel.

If a creature makes several separately resolved ranged attacks, each attack that deals damage may apply one Arcane Breach mark. Existing marks apply before the attack is resolved; the new mark is added afterward.

This means the first shot studies the target rather than receiving a free damage bonus. Repeated fire progressively exposes the target, and the rest of the army's shooters may exploit the weakness the enchanted stack has created.

The spell therefore rewards sustained ranged concentration without duplicating Archery, Focus Fire!, Bless, or Frailty. Focus Magic does not make arrows intrinsically stronger. It makes the enemy progressively easier to shoot through.

###### **Phantom Army**

Phantom Army copies one eligible friendly stack's creature count and offensive
profile, but its temporary body has a separate Integrity pool. Its starting
Integrity is based on the source stack's **current aggregate Health**, not its
maximum or original Health:

Phantom Integrity = max(1, floor(Source current Health × min(40%, 20% + 0.15% × SP)))

Sorcery rank strengthens only the `0.15% × SP` component using the shared
no-rank / Basic / Advanced / Expert coefficient ladder. It does not increase
the fixed 20% base, bypass the 40% cap, change the copied attack profile, or
grant another target. Illusionist's separate +25% Integrity applies **after**
the capped percentage, with final Health rounded down once. The phantom lasts
two rounds unless another explicit effect changes its duration.

###### **Implosion**

This is now much more Sorcery than Havoc.

I would calculate its damage from **current aggregate Health** , rather than original/max Health:

Damage % = min(30%, 12% + 0.10% × SP)

Then:

Damage = Target Current HP × Damage %

At SP 50: 17%.

At SP 100: 22%.

At SP 150: 27%.

At SP 180+: 30% cap.

Using current HP has an elegant property: Implosion is devastating against huge healthy stacks but naturally loses efficiency as they approach death. It therefore does not become a universal execution spell.

After damage resolves, every other creature stack within **3 hexes** is pulled **one hex toward the primary target** , if a legal destination exists.

The primary target does not move.

Forced movement causes no retaliation and does not count as normal movement.

I would resolve the pull closest-first, then clockwise from a deterministic orientation, so that collision resolution is predictable rather than arbitrary.

On **Prime damage** , I would not yet build a full additional damage taxonomy merely for this spell.

Internally I would represent it approximately as:

damageClass = MAGIC school = SORCERY element = NONE

The UI can call that **Prime damage** if you like.

Later, if you decide that creatures need Fire Resistance, Lightning Resistance, Shadow Resistance, Prime Resistance, etc., you can activate those tags mechanically. Until then, introducing six separate magical damage types adds rules without adding decisions.

###### **Time Stop**

Select a battlefield hex.

Ordinary radius = min(2, 1 + floor(SP / 100)). Without a Sorcery School
rank, SP below 100 gives radius 1 and SP 100+ gives radius 2. Chronomancer
raises the maximum radius to 3; it does not add a free radius step.

Under the version-3 School-rank rule, Sorcery's no-rank / Basic / Advanced /
Expert coefficient of 100% / 115% / 130% / 145% scales only the `SP / 100`
term before rounding down. An eligible Warcasting Spell Power bonus applies
to that term as well. The fixed first hex, radius cap, stasis lifetime, and
affected-target rules do not change. Thus, without Warcasting, the first
extra-radius threshold is SP 100 / 87 / 77 / 69 for no rank / Basic /
Advanced / Expert respectively. Older saved rules profiles keep the original
100% coefficient when the spell is available; this rule does not grant Time
Stop to a profile whose saved roster excludes it.

Everything inside enters stasis until the beginning of the caster's next Hero Action.

A creature in stasis cannot move, act, retaliate, receive damage, receive healing, be teleported, receive new effects, or lose duration from existing effects.

Existing timed effects are paused.

The creatures remain physically present and continue blocking their occupied hexes.

This is preferable to “lose X turns” because it is true temporal suspension rather than another disabling debuff.

It has offensive and defensive applications. You can temporarily remove half an enemy formation from the battle, preserve a dying stack, freeze a blocker, or isolate the enemy's support units.

###### **Spell Lock**

This may be the most characteristic Sorcery spell in the book.

Duration:

Duration = min(3, 1 + floor(SP / 80)) rounds

Without Sorcery rank: SP 0–79 → 1 round. SP 80–159 → 2 rounds.
SP 160+ → 3 rounds. Under the version-3 School-rank rule, Sorcery's
no-rank / Basic / Advanced / Expert coefficient of 100% / 115% / 130% / 145%
multiplies only the `SP / 80` term **before** rounding down. It does not
increase the fixed first round, the ordinary three-round cap, or the number
of targets. An eligible Warcasting Spell Power bonus applies to that same term.
Spellbinder adds its round after the ordinary cap (maximum four), then an
eligible Echoed Duration Metamagic follow-up may add one more (maximum five).

###### On a **friendly target** :

All hostile magical effects are immediately removed.

All beneficial magical effects remain active.

Their duration counters stop decreasing.

The target becomes completely immune to all further magic for the duration.

###### On an **enemy target** :

All beneficial magical effects are immediately removed.

All hostile magical effects remain active.

Their duration counters stop decreasing.

The target becomes completely immune to all further magic for the duration.

One subtle rule matters:

###### **Effects continue functioning while their timers are frozen.**

So if an enemy has Slow for one remaining round and you Spell Lock them for two rounds, they remain Slowed throughout those two rounds. When Spell Lock ends, Slow still has its original one round remaining.

Likewise, if your creature has Bless with one round remaining, Spell Lock can preserve it for two additional rounds.

But the target is genuinely sealed. You cannot heal it, Teleport it, buff it further, Dispel it, curse it again, or otherwise affect it magically until the Lock expires.

Orders remain completely unaffected because they are not magic.

That duality is excellent:

###### **Spell Lock on an ally preserves perfection. Spell Lock on an enemy preserves misery.**

And because the spell simultaneously prevents further magical intervention, neither use is simply “buff duration +3.”

The resulting Sorcery school now has a very coherent internal philosophy despite doing almost everything: it doesn't heal like Light, corrupt like Shadow, grow things like Nature, unleash elements like Havoc, or gamble with reality like Chaos. It selects, reshapes, enhances, copies, relocates, converts, suspends, and seals.

That is exactly what “magic manipulating magic and reality through technique” should look like.

Chaos

###### **🌀 CHAOS 🌀**

School of broken predictability: probability manipulation, random displacement, loss of control, suppression of creature abilities, transformation, dangerous collateral damage, domination, exchange of magical states, debuff exploitation, and paradoxical protection.

As with Sorcery, **Knowledge determines the hero's Mana pool** , while **Spell Power is converted through spell-specific coefficients** rather than functioning as a universal multiplier.

The following numbers are prototype values. The spell concepts themselves are considerably firmer than the exact Mana costs, durations, coefficients, and caps.

|Level|Spell|Mana|Proposed formula / rules|
|---|---|---|---|
|1|**Misfortune**|4|Suppresses positive Luck and reduces the probability of<br>favorable random effects|
|1|**Blink**|4|Randomly relocates any stack within a small SP-scaled<br>radius|
|1|**Confusion**|5|Target's next activation becomes Attack / Defend / random<br>movement|
|2|**Berserk**|8|Target attacks the nearest reachable stack regardless of<br>allegiance|
|2|**Forgetfulness**|8|Suppresses ranged attacks and special creature abilities|
|3|**Polymorph**|12|Turns an enemy into a random same-tier creature while<br>conserving its current HP pool|
|3|**Hand of Fate**|12|Heavy single-target damage; 50% of actual HP loss spills<br>onto one random other surviving stack, friend or enemy; its own magical defenses apply|
|4|**Puppet Master**|16|Directly control an enemy stack for one activation|
|4|**Reality Warp**|15|Friendly and enemy target exchange their transferable buffs and debuffs|
|5|Pandemonium|22|Battlefield-wide damage proportional to each stack's number of debuffs|
|5|Shield of Chaos|23|−10 Morale/Luck, but enormous physical and magical damage resistance|

###### **Misfortune**

This should be the purest expression of Chaos as **probability manipulation** . Select one enemy stack.

For the duration, positive Luck cannot trigger.

I would then give the spell a second effect applying to random beneficial creature mechanics. Duration:

Duration = min(4, 2 + floor(SP / 80)) rounds

So:

SP 0–79   → 2 rounds

SP 80–159 → 3 rounds

SP 160+   → 4 rounds

For favorable random creature effects:

− Probability Multiplier = max(25%, 75% − 0.25% × SP)

At SP 20:

70% of normal probability

At SP 100:

50% of normal probability

At SP 200:

25% of normal probability

Suppose a creature normally has a 30% chance to trigger some favorable ability. At SP 100:

30% × 50% = 15%

Misfortune should not alter deterministic abilities. If an ability always activates, Misfortune does not randomly cause it to fail.

It attacks **chance** , not creature functionality.

This distinction will matter enormously once creatures accumulate triggered abilities.

###### **Blink**

Select any creature stack, friendly or enemy.

It is teleported to a random legal position within a radius of its current position.

I would let Spell Power expand the possible displacement:

Radius = min(4, 2 + floor(SP / 100))

Therefore:

SP 0–99   → radius 2

SP 100–199 → radius 3

SP 200+   → radius 4

Every legal destination within the radius has equal probability.

The origin hex is excluded.

If the creature occupies multiple hexes, only destinations capable of accommodating its entire footprint are valid.

Blink ignores intervening creatures, terrain, obstacles, walls, and Zones of Control because this is teleportation rather than movement.

It does not:

- grant another action;

- refresh retaliation;

- change Initiative;

- count as voluntary movement;

- trigger movement-based abilities.

If absolutely no legal destination exists, the spell fails before Mana is spent.

The important distinction from Sorcery's Teleport is precision.

###### **Teleport:**

“I place this creature there.”

###### **Blink:**

“This creature is no longer staying here. Where it ends up is another question.”

That makes them mechanically related without being redundant.

###### **Confusion**

Select one enemy stack.

Its **next activation** is replaced by one randomly selected behavior:

Applying Confusion removes any existing Berserk effect. If negative Morale
forfeits that next activation, Confusion is consumed with it; it does not carry
over to a later usable activation.

- 1/3 → Attack

- 1/3 → Defend

- 1/3 → Wander

###### **Attack**

A random enemy of the confused creature is selected.

The creature attacks that stack using ordinary combat rules.

If several possible attacks exist, the target and legal attack position are chosen randomly.

If the selected enemy cannot be reached this activation, the confused creature moves toward it as far as possible.

If neither a legal attack nor a legal move toward the selected enemy is possible,
Attack resolves as Defend instead; the activation is not discarded without
the normal Defend benefit.

Importantly, Confusion does **not** make the unit attack its allies.

That is Berserk's territory.

###### **Defend**

The creature spends its activation Defending normally.

###### **Wander**

A random legal destination within the creature's movement range is selected.

If no legal movement destination exists, Wander resolves as Defend instead.

The creature moves there and ends its activation.

It does not intentionally engage an enemy.

Confusion therefore creates **loss of tactical intent** , not outright betrayal.

That makes it appropriate for Level 1.

The enemy still owns the creature.

They simply cannot rely on it doing what they need.

###### **Berserk**

Select one enemy stack.

On its next activation, it identifies the **nearest reachable creature stack** , regardless of allegiance, and attacks it.

Distance should be calculated using actual battlefield movement cost rather than raw hex distance.

If several stacks are equally near:

choose randomly among the tied stacks

The berserk creature uses its ordinary movement and attack abilities.

If it can reach the selected target, it attacks.

If it cannot reach any stack during the activation, it moves as far as possible toward the nearest stack.

For shooters, I would still force the creature toward **melee combat** .

Otherwise a Berserked shooter standing beside its own army could simply fire across the battlefield and behave almost rationally.

Berserk represents uncontrolled aggression.

The creature is not confused.

It knows exactly what it wants:

###### **violence against the nearest thing available.**

###### **Forgetfulness**

This is one of the most important anti-creature spells in Chaos.

Select one enemy stack.

For the duration, it forgets how to use its sophisticated combat capabilities.

Duration:

Duration = min(3, 1 + floor(SP / 80)) rounds

So:

SP 0–79   → 1 round

SP 80–159 → 2 rounds

SP 160+   → 3 rounds

While affected, the creature may only:

- Move;

- perform a basic melee attack;

- Wait;

- Defend.

It loses access to:

- ranged attacks;

- activated abilities;

- special attacks;

- triggered combat abilities;

- spellcasting;

- alternate attack modes;

- creature-specific commands.

The creature does **not** literally stop being what it is.

Therefore structural properties should remain:

- occupied hex size;

- creature type;

- faction;

- tier;

- living/undead/construct classification;

- base HP;

- base damage;

- base Speed and Initiative.

This avoids bizarre cases where Forgetfulness makes an Undead creature suddenly living or causes a two-hex creature to physically collapse into one hex.

The clean implementation principle is:

**Forgetfulness removes what the creature knows how to do, not what the creature physically is.**

###### **Polymorph**

Select one enemy stack.

The entire stack transforms into a random creature belonging to the **same creature tier** : Core → random Core

Elite → random Elite

Champion → random Champion

The replacement creature may belong to **any faction** .

Allegiance remains unchanged.

The important balancing rule is that transformation should not secretly heal or damage the stack.

Let:

H = target's current aggregate HP

NHP = HP of one creature of the new species

Then:

New creature count = ceil(H / NHP)

The final creature begins partially wounded if necessary so that:

New aggregate current HP = H

Example:

The target currently has:

1,370 aggregate HP

It transforms into a creature with:

120 HP each

Then:

ceil(1370 / 120) = 12 creatures Eleven are healthy and the twelfth contains the remaining HP.

No health was gained or lost through transformation.

Duration:

###### 2 rounds

When Polymorph expires, surviving aggregate HP is converted back into the original creature type using the same system.

Expiry and Dispel restore the original footprint at its current position or the
nearest legal battlefield position. Exception: if no legal original footprint
exists anywhere, retain the current form, surviving HP and position, show
restoration pending, and retry at later round boundaries. Never overlap another
stack, remove creatures or heal them to force restoration. This exceptional
delay does not renew the spell's normal duration.

For Phantom Army, transform the copied offensive creature body's aggregate HP;
its separate current and initial Integrity remain unchanged. Integrity does not
determine the transformed offensive creature count.

Therefore damage suffered while transformed remains real.

If the transformed stack falls to 410 HP, it returns with approximately 410 aggregate HP worth of its original creatures.

This prevents Polymorph from becoming a disguised healing spell.

The transformed creature receives:

- the new creature's Attack/Defense equivalents;

- movement characteristics;

- damage;

- abilities;

- resistances;

- attack type;

- creature-specific mechanics.

It retains:

- owner;

- allegiance;

- current Initiative position;

- transferable magical effects.

If the replacement creature's footprint cannot occupy the stack's current hex,
relocate the transformed stack to the nearest legal battlefield position. The
replacement is not excluded from the random draw merely because it does not fit
at the original position. Relocation does not change its owner, allegiance or
current position in the Initiative queue.

This spell can produce spectacular outcomes.

A terrifying Champion might become a Champion poorly suited to the current battle.

But it might also become something even worse.

That risk is exactly why this belongs to Chaos rather than being a conventional debuff.

###### **Hand of Fate**

This is Chaos's principal direct-damage spell.

It should hit harder than one expects from a non-Havoc school, but the caster loses control over the consequences.

Primary damage:

Damage = 70 + 2.5 × SP

At SP 20:

120 damage At SP 50: 195 damage At SP 100: 320 damage At SP 150: 445 damage After the target receives damage, calculate the **actual HP lost** . Then: Collateral Damage = 50% × Actual Damage Inflicted One other surviving creature stack on the battlefield is selected randomly. Friendly and enemy stacks are equally valid. Example:

Hand of Fate calculates: 320 damage But the enemy target has only: 170 HP remaining It therefore suffers only: 170 actual damage The secondary hit is: 170 × 50% = 85 damage not 160. This prevents overkill from generating free collateral damage. The primary target cannot be selected again for the secondary strike. If no other stack exists, there is no collateral hit.

No resistance or targeting intelligence should influence which secondary stack is chosen.

The secondary hit starts at half the primary target's actual HP loss, rounded down. Apply the selected recipient's own magical defenses, including immunity, resistance, and magical damage reduction. Do not apply the caster's damage bonuses a second time. Protected surviving stacks remain eligible for selection; if the chosen recipient blocks the hit, it deals no damage and is not rerolled.

That would undermine the spell.

The caster chooses **who Fate strikes first** .

They do not choose who pays the price.

###### **Puppet Master**

Select one enemy stack.

The caster gains complete control over that stack for **one activation** .

When its turn arrives, the caster may use it exactly as though it were friendly:

- move;

- attack;

- shoot;

- Wait;

- Defend;

- activate creature abilities;

- choose targets;

- choose positioning.

The stack does not change allegiance mechanically.

If the selected stack is already Berserk, a successful Puppet Master cast removes
its existing Berserk effect. A rejected or resisted cast does not remove it.

This distinction matters.

Effects that trigger when attacking allies or enemies should still recognize its true owner.

After the controlled activation ends, the creature receives:

###### **Lucidity**

for:

###### 2 rounds

While Lucidity lasts, the stack is immune to:

- Puppet Master;

- Berserk;

- Confusion;

- other effects explicitly tagged as mental domination/control.

It is not immune to:

- Misfortune;

- Blink;

- Polymorph;

- damage;

- ordinary debuffs;

- Reality Warp.

I would make Lucidity fixed rather than Spell Power-scaled.

Otherwise increasing the caster's Spell Power bizarrely makes the victim resistant for longer.

Puppet Master should be immensely powerful, but it should not permit indefinite chain-control of the same Champion.

###### **Reality Warp**

Select:

1 friendly stack

+

1 enemy stack

Every transferable temporary magical buff and debuff affecting those stacks exchanges targets. Suppose:

Friendly stack:

Bless

Haste Shield Curse

Enemy stack:

Slow

Weakness

Misfortune

After Reality Warp: Friendly stack:

###### Slow

Weakness

Misfortune Enemy stack: Bless Haste Shield Curse

The exchange is total.

Reality Warp is not “move the good effects to me.”

It swaps **reality as it currently exists around the two creatures** .

Remaining durations are preserved.

If Haste has two rounds remaining when transferred, it still has two rounds remaining.

The following do not transfer:

- Orders;

-

- intrinsic creature abilities;

- permanent bonuses;

- equipment effects;

- terrain states;

- summoned-creature status;

- transformations such as Polymorph;

- effects explicitly tagged as non-transferable.

Effects requiring an illegal target should remain where they are.

For example, if some future spell can only affect Undead creatures, Reality Warp cannot move it onto a living target.

No Spell Power coefficient is required.

The strength of Reality Warp comes from **what the players have already invested into those two stacks** .

Against unbuffed armies it may be mediocre.

Against two heavily modified stacks it can reverse an entire battle.

###### **Pandemonium**

This should not be another Armageddon.

Its damage comes entirely from the amount of disorder already present on the battlefield. Let:

D = number of active debuffs on the stack

For each stack:

Damage = D × (20 + 0.25 × SP)

Stacks with:

D = 0

take:

0 damage At SP 100:

Damage per debuff = 45

So:

|Debuffs|Damage|
|---:|---:|
|0|0|
|1|45|
|2|90|
|3|135|
|4|180|
|5|225|

At SP 160:

20 + 0.25 × 160 = 60 damage per debuff

A creature carrying four debuffs takes:

240 damage

Pandemonium affects **every stack on the battlefield** , including the caster's own army.

That friendly-fire component is essential.

Chaos should reward a player capable of creating an asymmetrical state:

Enemy army = covered in negative effects

Friendly army = relatively clean

and then detonating that difference.

For implementation, I would create a general status tag:

DEBUFF

Pandemonium counts active effects carrying that tag.

This is preferable to manually listing Curse, Slow, Misfortune, Poison, etc.

It also means future creature abilities can automatically interact with Pandemonium when appropriate.

Permanent creature characteristics and raw statistics do not count.

Low Morale itself, for example, is not a debuff.

− A temporary spell giving Morale can be.

Pandemonium therefore creates a very characteristic Chaos battle plan:

Disrupt

→ Accumulate disorder

→ Exploit disorder

###### **Shield of Chaos**

Select one stack, friendly or enemy.

For the duration:

− Morale = Morale 10

− Luck = Luck 10

At the same time, the target gains enormous resistance to both physical and magical damage. I would calculate both resistances identically: Damage Resistance = min(80%, 50% + 0.15% × SP) At SP 20: 53% At SP 50: 57.5% At SP 100: 65% At SP 150: 72.5% At SP 200: 80% cap Duration: 2 rounds If a target with 65% resistance would normally suffer: 400 physical damage it receives: 140 damage The same applies to magical damage. The resistance affects **damage only** . Shield of Chaos does not make the creature immune to magic. It can still be:

- Dispelled;

- Teleported;

- Blinked;

- controlled;

- debuffed;

- Polymorphed;

- affected by Reality Warp.

That distinction is crucial because otherwise Shield of Chaos overlaps Sorcery's Spell Lock.

Its offensive use is deliberately paradoxical.

Cast it on an enemy:

- 10 Morale

- 10 Luck

but now that enemy is extremely difficult to kill.

Cast it on an ally:

extreme survivability

but that creature becomes catastrophically unreliable.

There should be no clean “correct” target.

That is why the spell belongs at Level 5.

Paradox Shield adds 10 percentage points after Shield of Chaos's own 80% formula
cap. The ordinary total Physical Damage Reduction cap remains 80%; the perk
does not raise or bypass it. Its Magical Damage Reduction can reach 90%, subject
to the ordinary 95% total magical cap. The -10 Morale and -10 Luck penalties
are unchanged.

The resulting Chaos school now has a coherent escalation:

**Level 1:** probability, position, and decision-making become unreliable.

**Level 2:** creatures lose rational behavior and trained capabilities.

**Level 3:** identity and causality become unstable.

**Level 4:** the caster seizes agency and exchanges magical reality between creatures.

**Level 5:** accumulated disorder becomes destructive, while protection itself becomes a curse.

Chaos therefore does not heal like Light, manipulate magic with precision like Sorcery, destroy through elemental force like Havoc, corrupt life and death like Shadow, or command living systems like Nature.

It **misdirects, randomizes, suppresses, transforms, redirects, dominates, exchanges, destabilizes, and weaponizes disorder** .

Nature

###### **🌀 NATURE 🌀**

Nature is the school of living systems and the physical world: regeneration, vegetation, venom, beasts, growth, terrain, and elemental forces. It does not protect or restore through divine miracles like Light, and it does not reproduce the Attack/Defense/retaliation/formation space already occupied by Orders. Instead, Nature changes bodies, creates creatures, restrains movement, alters terrain, and lets natural forces propagate across the battlefield. This follows the existing division in _New Horizons_ : magic is responsible for supernatural phenomena such as summoning and elemental manipulation, while Orders cover military coordination and tactical combat bonuses.

The following numbers are prototypes; the spell identities are the important part.

|Level|Spell|Mana|Proposed formula / rules|
|---|---|---|---|
|1|**Regeneration**|4|Recovers a portion of damage recently suffered by<br>surviving creatures|
|1|**Entangle**|4|Roots one enemy stack in place without preventing<br>attacks|
|1|**Vengeful Vines**|5|Damages creatures along an irregular vine pattern<br>and reduces Speed|
|2|**Poison**|7|Escalating biological damage over time|
|2|**Summon Trolls**|9|Summons a temporary HP pool of Trolls|
|3|**Verdant Prison**|12|Summons Dendroids in a ring around the target|
|3|**Earthquake**|12|Damages fortifications in sieges; fractures terrain in<br>field battles|
|4|**Quicksand**|15|Creates concealed terrain traps that halt ground<br>movement|
|4|**Hydra's Vitality**|16|Increases maximum HP and grants powerful<br>regeneration|
|5|**Nature's Wrath**|23|Chains through up to 17 stacks, damaging enemies<br>and healing allies|
|5|**Elemental**<br>**Convergence**|22|Summons Elementals determined by the battlefield<br>terrain|



###### **Regeneration**

Regeneration should not simply be a slower version of Light's Cure.

Instead, it allows the creature's body to repair **recent wounds** .

For **3 rounds** , whenever the enchanted stack suffers damage, a portion is marked as Regenerable:

Regenerable Damage = min(50%, 25% + 0.15% × SP)

At the beginning of the stack's next activation, that marked HP is restored.

At SP 100:

40% of eligible damage regenerates

If the stack suffers 300 damage:

120 HP becomes Regenerable

At its next activation, surviving creatures restore those 120 HP.

Casualties remain dead.

If the attack killed three creatures, Regeneration does not reconstruct them; it merely repairs wounds among surviving members of the stack.

That keeps the distinction from Light sharp:

**Cure restores damage that already exists immediately.**

**Regeneration changes how the organism responds to future injury.**

###### **Entangle**

Select one enemy ground stack.

Roots and vegetation erupt beneath it.

For the duration:

Speed = 0

The creature may still:

- attack adjacent enemies;

- retaliate;

- shoot;

- Wait;

- Defend;

- use abilities that do not require movement.

It simply cannot voluntarily change position.

Duration:

Duration = min(2, 1 + floor(SP / 100)) rounds Thus:

SP 0–99  → 1 round SP 100+  → 2 rounds

For Entangle, School rank scales only the `SP / 100` term through the
shared 100% / 115% / 130% / 145% coefficient before flooring; the fixed
one-round base and ordinary two-round cap do not scale. Rootcaller adds
one round to this result, capped at three for that perk. Echoed Duration
then adds its separate Metamagic-cast round through the common duration
rule. Rooting sets voluntary movement to zero, not Initiative, and does
not depend on a binding creature remaining alive or adjacent.

Forced displacement breaks Entangle.

Teleportation also removes the creature from the roots.

Entangle therefore attacks **position** , not Initiative and not the creature's combat statistics.

###### **Vengeful Vines**

Vengeful Vines gives Nature a low-level damage spell with a player-selected
footprint of exactly three distinct connected playable hexes. Each hex after
the first must touch at least one previously selected hex. Straight lines,
bent chains and triangles are legal; occupied and obstacle hexes are allowed.

Show the selected hexes and a legal hovered candidate on the battlefield,
with a running selection count. The third legal click casts immediately,
without an orientation choice or separate confirmation. Backspace removes
the last selection; Escape cancels without spending Mana or the Hero Action.
An illegal third click leaves the first two selections intact. The final
footprint must intersect at least one receptive enemy stack. Each intersected
enemy stack is affected once, even if it occupies two selected hexes;
friendly stacks are unaffected. Every affected enemy stack suffers:

Damage = 20 + 1.1 × SP

and:

− Speed 2

for **2 rounds** .

At SP 100:

130 Nature damage

2 Speed

The Speed penalty affects battlefield movement only.

School rank strengthens only the Spell Power-derived damage component
through the shared 100% / 115% / 130% / 145% coefficient. The fixed −2
Speed penalty and its two-round base do not scale with rank. Common
cast-duration extensions such as Echoed Duration apply normally.

It does **not** affect Initiative.

That distinction is important because Sorcery's Slow already owns Initiative manipulation.

Vengeful Vines therefore rewards lining up several targets and then leaves them physically less mobile afterward.

###### **Poison**

Poison is a bodily condition rather than an ordinary magical curse.

Calculate:

Base Poison = 20 + 0.5 × SP

Then:

Round 1 → 1.0 × Base Round 2 → 1.5 × Base Round 3 → 2.0 × Base

At SP 100:

Base = 70

Round 1 → 70 Round 2 → 105 Round 3 → 140

Total = 315

Poison does not spread.

That keeps it distinct from Shadow's Plague.

**Poison intensifies.**

**Plague propagates.**

Once inflicted, Poison is considered a physical affliction rather than a temporary magical effect. This is already consistent with _New Horizons_ , where ordinary Dispel explicitly leaves biological Poison untouched while Light's Cure can remove it.

###### **Summon Trolls**

Nature calls existing Troll creatures onto the battlefield.

The fact that Trolls are relatively high-tier creatures is not a problem. The spell simply summons fewer of them.

Calculate:

Troll HP Pool = 100 + 2.5 × SP

At SP 100:

350 HP worth of Trolls

Convert that HP pool into the existing Troll creature.

Troll Count = ceil(HP Pool / Troll HP)

School proficiency strengthens only the Spell Power component, not the fixed
100 HP. Beastcaller increases the resulting whole pool by 25%. Apply all these
modifiers before rounding the final pool down once to whole HP; do not round
the Spell Power component prematurely. Every cast creates an independent
temporary stack at its chosen legal empty hex, rather than merging into an
earlier summon or imposing an additional once-per-combat restriction.

The final Troll may begin partially wounded so the spell does not gain free HP through rounding. The Trolls:

- appear on a legal empty hex chosen by the caster;

- retain their normal creature abilities, including their intrinsic regeneration;

- enter the ordinary Initiative cycle;

- disappear after combat;

- cannot generate strategic resources or permanent troops.

Nature is therefore using an existing creature rather than requiring a special Wolf asset solely for one spell.

###### **Verdant Prison**

Select one enemy stack.

Dendroids erupt from the ground around it, filling as many legal adjacent hexes as possible. Conceptually:

D D D E D

D D

where:

E = enemy target

D = summoned Dendroid stack

The spell first calculates one shared pool:

Dendroid HP Pool = 180 + 3 × SP

At SP 100:

480 HP

That pool is divided evenly among all Dendroid stacks created.

Use unupgraded Dendroid Guards on distinct legal empty hexes adjacent to the
target's occupied footprint, including both hexes of a wide target. School
proficiency strengthens only the Spell Power component; Verdant Warden
increases the whole resulting pool by 25%. Round the final pool down once to
whole HP, then distribute it evenly. Give any remaining whole HP one at a time
to placements in ascending battlefield-hex order, so the ring retains the exact
pool. Each stack uses `ceil(its HP / effective Dendroid HP)` creatures with a
wounded final creature when necessary. With no legal placement, reject the
cast before spending Mana or a Hero Action.

If six positions are available:

80 HP per Dendroid stack

If only four positions are available:

120 HP per Dendroid stack

The Dendroids are normal temporary combat summons and use their ordinary creature abilities.

They do not disappear merely because the imprisoned creature escapes.

That is important: the spell does not create an abstract immobilization debuff. It creates **actual creatures occupying actual battlefield space** .

The enemy can:

- kill the Dendroids;

- fly over them if capable;

- teleport away;

- exploit an incomplete ring;

- remain trapped and fight.

This makes Verdant Prison battlefield creation rather than another variant of Entangle.

###### **Earthquake**

Earthquake has two distinct applications.

###### **During sieges**

It retains its classic Heroes identity.

The caster selects a section of the fortification.

Earthquake damages:

Affected Fortification Sections = min(4, 2 + floor(SP / 80))

Possible targets include:

- wall sections;

- gate;

- defensive towers.

I would not make the affected sections completely random. The caster chooses the general area, while the exact damaged sections within that area may still vary.

###### **During ordinary battles**

Select a battlefield area.

Radius = 2

All grounded stacks in the area suffer:

Damage = 30 + 0.8 × SP

At SP 100:

110 damage

The affected hexes then become **Fractured Ground** for 3 rounds.

Entering a Fractured Ground hex costs:

+1 Speed / movement point

Flying creatures ignore this additional terrain cost while flying.

Earthquake may also destroy ordinary destructible battlefield scenery inside the area.

It does not reduce Initiative, Attack, Defense, Physical Damage Reduction, or retaliation.

It damages **the battlefield itself** .

###### **Quicksand**

The caster places concealed patches of Quicksand on legal empty ground hexes.

Patches = min(5, 2 + floor(SP / 60))

Thus:

SP 0–59    → 2 patches SP 60–119  → 3 patches SP 120–179 → 4 patches SP 180+    → 5 patches

The caster can see all Quicksand patches.

The opponent cannot see them until triggered.

When a ground creature enters a Quicksand hex:

1. its movement immediately ends; 2. the patch becomes visible;

3. the creature remains trapped until its next activation.

The Quicksand then remains visible for the rest of its duration.

Friendly creatures can also trigger it.

Nature does not magically distinguish allegiance once the terrain has been altered.

Flying creatures ignore Quicksand while airborne.

The difference from Earthquake is therefore clear:

**Earthquake creates broad, visible difficult terrain.**

**Quicksand creates specific concealed traps.**

###### **Hydra's Vitality**

Hydra's Vitality magically exaggerates the target's biological resilience.

Target: one friendly living organic stack, excluding Undead, nonliving
constructs and war machines. The spell never gains a mass version from rank.

For **3 rounds** :

Maximum HP per creature increase = min(50%, 25% + 0.15% × SP)

and at the beginning of every activation:

Regeneration =

10% of enhanced maximum HP per surviving creature

At SP 100:

Maximum HP +40%

A creature with:

100 / 100 HP

becomes:

100 / 140 HP

It does **not** immediately gain those 40 HP.

School proficiency strengthens only the Spell Power contribution to the
percentage, leaving the fixed 25% base and 50% cap unchanged. Round the
resulting maximum HP per creature down once. Recasting refreshes the same
enchantment; it does not compound an earlier Hydra's Vitality capacity bonus.

Instead, it begins filling that newly created capacity through its regeneration.

This distinction prevents the spell from becoming Light-style instant healing.

If it survives long enough, it can eventually reach:

140 / 140 HP

Casualties are not restored.

Regeneration fills each survivor's own missing body HP, rather than adding
aggregate HP and reinterpreting that total as additional creatures. Fractional
tenths of HP carry between genuine activations, so small creatures do not lose
their regeneration to repeated rounding. A spell or Order continuation is not
a creature activation. Empty capacity, current health and surviving count must
remain distinct during damage and ordinary healing as well as regeneration.

When Hydra's Vitality expires, maximum HP returns to normal.

Any current HP exceeding the creature's ordinary maximum is lost.

This is not Guardian Spirit either. Guardian Spirit creates a separate protective HP pool; Hydra's Vitality alters the creature's actual body. Light's current Guardian Spirit explicitly functions as such a separate pool.

###### **Nature's Wrath**

Nature's Wrath sends a massive current of primal energy through the battlefield.

Select the first creature stack.

The spell then chains automatically to the nearest unvisited stack within chaining range.

Maximum:

17 different stacks

A stack may be struck only once.

On an **enemy** :

Nature's Wrath deals Nature damage

###### On a **friendly stack** :

Nature's Wrath restores HP Initial power: Power = 110 + 2 × SP

Every subsequent jump retains: 93% of previous Power

So approximately: Jump 1  → 100% Jump 2  → 93% Jump 3  → 86% Jump 4  → 80% ... Jump 17 → ~31% At SP 100:

Initial Power = 310

If the first target is an enemy:

310 damage

If the second target is an ally:

288 HP restored

If the third is another enemy:

268 damage

and so on.

Healing cannot resurrect casualties.

The path is determined by battlefield proximity rather than being manually selected one target at a time.

That limitation is important.

Nature's Wrath is not a controlled Light miracle that conveniently heals every ally. It is a **living current coursing across the battlefield** , changing effect according to what it encounters.

The spell may therefore create cases where the player deliberately arranges their formation beforehand to influence the chain.

###### **Elemental Convergence**

Nature draws the elemental force inherent in the battlefield itself into physical form.

The terrain determines which Elemental answers.

A provisional mapping might be:

|**Battlefield Terrain**|**Elemental**|
|---|---|
|Lava / volcanic|Fire|
|Snow / water-rich|Water|
|Rocky / subterranean|Earth|
|Highlands / open sky|Air|
|Magical / unusual terrain|Fifth Elemental type|



The current experimental mapping and the five-Elemental Conflux roster are fixed in Experimental Values for testing.

Calculate:

Elemental HP Pool = 250 + 5 × SP

At SP 100:

750 HP worth of Elementals

The pool is converted into the appropriate existing Elemental creature:

Elemental Count =

ceil(HP Pool / Elemental HP)

The final creature may begin partially wounded.

The summoned Elementals:

- appear on a legal battlefield position chosen by the caster;

- use their complete normal creature abilities;

- remain until destroyed or combat ends;

- disappear after battle.

Unlike Sorcery's Transfigure Matter, the spell does not convert an existing object into an artificial creature.

Nature simply calls forth the force already latent in the environment.

The resulting progression is now quite coherent:

|Level|Nature's progression|
|---:|---|
|1|Bodies recover, roots restrain, vines lash outward|
|2|Biology becomes poisonous and living creatures answer the call|
|3|Nature physically populates and breaks the battlefield|
|4|Terrain becomes treacherous and organisms exceed their normal limits|
|5|The whole battlefield becomes part of a natural current, and the elements themselves take form|

And critically, the school does not need generic +Attack , +Defense , +Initiative , retaliation bonuses, or army-wide stat boosts—the exact territory already occupied by Orders and Light.

Havoc

###### **🌀 HAVOC 🌀**

Havoc is the school of raw magical destruction: fire, frost, lightning, bombardment, and catastrophic force. Unlike the other schools, it does not need elaborate secondary systems. Its variety comes from **how damage is delivered** —single targets, explosions, rings, traps, persistent zones, chains, bombardments, and battlefield-wide devastation.

That is consistent with _New Horizons_ , where Havoc is defined around elemental devastation and catastrophic magical power.

The following numbers are prototype values; the spell identities and damage patterns are the important part.

|Level|Spell|Mana|Proposed formula / rules|
|---|---|---|---|
|1|Fireball|5|Small-area explosion: Damage = 25 + 0.8×SP|
|1|Ice Bolt|5|Efficient single-target damage: Damage = 45 + 1.0×SP|
|1|Lightning Bolt|5|High-scaling single-target damage: Damage = 20 + 1.5×SP|
|2|Frost Ring|8|Damages a ring around a safe central hex: Damage = 55 + 1.1×SP|
|2|Land Mine|8|Places hidden hostile-triggered mines: Mine Damage = 60 + 1.1×SP|
|3|Fire Wall|12|Creates a persistent line of damaging fire|
|3|Inferno|13|Large-area explosion: Damage = 70 + 1.2×SP|
|4|Chain Lightning|17|Powerful lightning jumps between nearby stacks with decreasing damage|
|4|Meteor Shower|18|Heavy area bombardment: Damage = 110 + 1.5×SP|
|5|Armageddon|24|Enormous battlefield-wide indiscriminate damage|
|5|Disintegrate|25|Supreme single-target damage; destroyed creatures leave no remains|



###### **Fireball**

Fireball is Havoc's basic area-damage spell.

Select one battlefield hex.

Every stack occupying the selected hex or an adjacent hex suffers:

Damage = 25 + 0.8 × SP

At SP 100:

105 Fire damage

The damage is identical throughout the affected area.

Fireball does not burn, reduce Creature Defense, lower Morale, or create lingering fire.

It simply explodes.

This makes it the basic choice when several creatures can be caught together.

###### **Ice Bolt**

Ice Bolt is the efficient low-level single-target spell.

Damage = 45 + 1.0 × SP

At SP 100:

145 Frost damage

It has a relatively high base value but only moderate Spell Power scaling.

It does **not** reduce Speed or Initiative.

That distinction matters because Vengeful Vines already attacks Speed and Sorcery's Slow attacks Initiative.

Ice Bolt is simply concentrated destructive cold.

###### **Lightning Bolt**

Lightning Bolt uses the opposite scaling philosophy.

Damage = 20 + 1.5 × SP

At SP 100:

170 Lightning damage

At low Spell Power, Ice Bolt is stronger.

At high Spell Power, Lightning Bolt overtakes it.

For example:

|**SP**|**Ice Bolt**|**Lightning Bolt**|
|---|---|---|
|20|65|50|
|50|95|95|
|100|145|170|
|150|195|245|



That gives two Level-1 single-target spells legitimate reasons to coexist.

**Ice Bolt rewards efficiency and low-SP casters.**

**Lightning Bolt rewards magical specialization.**

No stun, no chaining, no secondary effect.

###### **Frost Ring**

Select one battlefield hex.

The central hex is completely safe.

Every stack occupying the surrounding ring suffers:

Damage = 55 + 1.1 × SP

At SP 100:

165 Frost damage

Conceptually:

X X X  O  X X X

Where:

O = safe center X = damaged surrounding hexes

The spell may therefore be deliberately centered underneath a friendly creature to damage enemies surrounding it without harming that ally.

That targeting geometry is the spell.

It requires no additional debuff.

###### **Land Mine**

Land Mine creates concealed magical explosives on empty battlefield hexes.

Number created:

Mines = min(4, 2 + floor(SP / 100))

Thus:

SP 0–99    → 2 mines SP 100–199 → 3 mines SP 200+    → 4 mines

Each mine deals:

Mine Damage = 60 + 1.1 × SP

At SP 100:

170 Fire damage

The caster chooses the mine locations.

The mines are:

- visible to the caster;

- invisible to the opponent;

- triggered when a hostile ground creature enters the hex;

- consumed immediately after exploding;

- ignored by flying creatures while airborne.

Unlike Nature's Quicksand, Land Mines are magical weapons rather than transformed terrain.

Therefore friendly creatures do **not** trigger their own mines.

The distinction is clean:

**Quicksand:** hidden terrain that traps anything stepping into it.

**Land Mine:** hidden hostile-triggered destruction.

###### **Fire Wall**

Select a starting hex and orientation.

A line of magical flame appears across several connected battlefield hexes.

Base length:

3 hexes

Duration:

3 rounds

Whenever a creature enters a Fire Wall hex or begins its activation inside one:

Damage = 40 + 1.0 × SP

At SP 100:

140 Fire damage

A creature can suffer Fire Wall damage only **once per activation** , regardless of how many burning hexes it crosses.

That restriction prevents deliberately walking a large creature through three wall hexes and receiving three full damage instances.

Fire Wall affects friend and foe alike.

Unlike Land Mine, it is completely visible.

Its identity is persistent destruction:

The spell does not hurt where the enemy is now. It makes part of the battlefield dangerous to occupy.

###### **Inferno**

Inferno is Havoc's first major conventional area spell.

Select one battlefield hex.

All stacks within a broad radius suffer:

Damage = 70 + 1.2 × SP

At SP 100: 190 Fire damage Unlike Fireball, Inferno covers a substantially larger area. There is no secondary effect.

No burning.

No persistent terrain.

No displacement.

Inferno exists for one reason:

###### **Several things are standing close together and you would like all of them to stop doing that.**

That simplicity is appropriate for Havoc.

###### **Chain Lightning**

Select the first target.

The lightning then automatically jumps to the nearest unstruck creature within chaining range. Maximum targets: 5 stacks Initial damage: Initial Damage = 130 + 1.8 × SP At SP 100: 310 damage

Successive jumps deal:

Jump 1 → 100% Jump 2 → 70% Jump 3 → 50% Jump 4 → 35% Jump 5 → 25%

At SP 100:

Target 1 → 310 Target 2 → 217 Target 3 → 155 Target 4 → 109 Target 5 → 78

Only the first target is chosen by the caster.

Afterward, Chain Lightning chooses the nearest valid unstruck stack **regardless of allegiance** .

So a poorly positioned friendly creature can conduct the lightning.

That makes Chain Lightning powerful without making it a perfectly controllable multi-target spell.

Nature's Wrath differs fundamentally: it can change between healing and damage according to allegiance and can traverse almost the entire battlefield.

Chain Lightning has one purpose:

###### **conduct destruction through nearby bodies.**

###### **Meteor Shower**

Select a battlefield area.

Every stack within the impact zone suffers:

Damage = 110 + 1.5 × SP

At SP 100:

260 damage

Meteor Shower damages friend and foe alike.

Ordinary destructible battlefield objects inside the affected area are also destroyed:

- trees;

- rocks;

- rubble;

- wooden obstacles;

- similar scenery.

Fortifications may also suffer reduced Meteor Shower damage if they occupy the impact zone.

Meteor Shower does not leave a persistent crater or movement penalty.

The environment is destroyed because enormous rocks fell out of the sky—not because Meteor Shower is secretly a terrain-control spell.

Compared with Inferno:

**Inferno** covers more space efficiently.

**Meteor Shower** delivers much heavier concentrated devastation.

###### **Armageddon**

Armageddon does not require a target.

The entire battlefield is engulfed in catastrophic magical destruction.

Every creature stack suffers:

Damage = 150 + 1.8 × SP

At SP 100:

330 damage

At SP 150:

420 damage

The spell affects:

- enemies;

- allies;

- summoned creatures;

- neutral battlefield entities that can receive damage.

Ordinary destructible obstacles are destroyed.

Siege fortifications also suffer damage.

There is no built-in protection for the caster's army.

If the player wants to exploit Armageddon using resistant or immune creatures, artifacts, Magical Damage Reduction, or other systems, that is an army-building decision rather than a special rule of the spell.

Armageddon's identity should remain uncompromised:

###### **Everyone gets hit.**

###### **Disintegrate**

Disintegrate is the strongest conventional single-target damage spell in the game.

It does not depend on missing HP like Soul Reaper.

It does not depend on current HP like Sorcery's Implosion.

It does not apply a curse.

It does not spread.

It simply destroys one target with overwhelming magical force.

Damage = 180 + 2.5 × SP

At SP 100:

430 damage At SP 150: 555 damage

At SP 200:

680 damage

Its defining secondary rule follows directly from what **disintegration** means:

Creatures killed by Disintegrate leave no usable remains.

Therefore casualties caused specifically by Disintegrate cannot later be restored through effects requiring a corpse or physical remains, including Resurrection or Re-animate.

Suppose a stack contains ten creatures and Disintegrate kills four.

Those four are marked as:

Disintegrated

The surviving six remain perfectly normal.

If the other six later die conventionally, those six may still leave ordinary remains.

This requires casualty provenance to be tracked, but the mechanic is worth it because it gives Havoc's ultimate single-target spell a consequence appropriate to its name without turning it into another debuff.

The distinction between the Level-5 spells is therefore absolute:

**Armageddon:** maximum breadth.

**Disintegrate:** maximum concentration.

The complete Havoc progression becomes:

###### **Level**

|Level|Destructive principle|
|---:|---|
|1|Explosion, cold, and lightning establish the three basic damage forms|
|2|Destruction acquires unusual geometry and delayed traps|
|3|Damage persists in space or expands across large areas|
|4|Destruction propagates between creatures or falls from the sky|
|5|Either the entire battlefield is devastated or one target is annihilated|

That gives Havoc an intentionally pure identity:

**Light preserves. Shadow condemns. Nature grows. Sorcery manipulates. Chaos destabilizes. Havoc destroys.**

# Adventure Magic

### Design principle

Adventure Spells remain part of New Horizons because strategic magic is part of the identity of Heroes, but their original unrestricted forms can erase geography, logistics, and map structure. The system is therefore kept deliberately small: exactly five Adventure Spells, one associated with each Mage Guild level.

Adventure Spells are neutral strategic magic. They do not belong to Light, Shadow, Nature, Havoc, Sorcery, or Chaos; they require no Magic School Skill; and School proficiency does not modify them. A hero may cast at most one Adventure Spell per day. This is a hard shared limit across all five spells, preventing chaining such as repeated Dimension Door casts or Fly followed by Town Portal on the same day.

Adventure Spells consume normal Mana but are intentionally expensive. Wisdom and ordinary combat-spell Mana discounts do not reduce their Mana cost unless an effect explicitly states otherwise. The Mana values below are prototype balance values and may be tuned after testing.

### Mage Guild Adventure Spell unlocks

Each Mage Guild level provides one fixed Adventure Spell unlock. Constructing the Guild level does not grant that Adventure Spell automatically: the town must pay its listed Gold and resource cost to unlock it. This is a fixed purchase, not a reroll system. The unlock is permanent for that town rather than globally shared across the kingdom.

A hero who visits a town whose Mage Guild has unlocked that Adventure Spell learns it permanently. There is no random roll, no separate Adventure Magic Skill, and no ordinary combat-spell research mechanic in New Horizons. Mage Guild combat spells are determined by the normal town spell-generation rules; they are not replaced, rerolled, or researched.

|Guild Level|Adventure Spell|Prototype Mana|Core restriction|
|---|---|---|---|
|I|Summon Boat|20|Summon an available boat to<br>an adjacent valid water tile.|
|II|Water Walk|30|Lasts until end of day; water<br>movement is deliberately<br>expensive and the hero may<br>not end the day on water.|
|III|Town Portal|50|Teleport to the nearest controlled town only; the destination is not freely chosen; casting ends the hero's movement for the day.|
|IV|Fly|60|Lasts until end of day; flying movement is deliberately expensive and cannot bypass scenario-defined protected barriers.|
|V|Dimension Door|80|One short-range teleport to a visible legal tile; cannot bypass protected barriers; casting ends the hero's movement for the day.|



**Guild Level Adventure Spell Prototype Mana Core restriction** One short-range teleport to a visible legal tile; cannot V Dimension Door 80 bypass protected barriers; casting ends the hero's movement for the day.

### Spell restrictions

Summon Boat is a quality-of-life strategic spell, not a source of free naval movement. It requires a legal adjacent water destination and an available boat to summon.

Water Walk remains useful for rivers, short crossings, and awkward coastlines, but it must not replace boats. Water-walking steps cost 1.50x the normal land cost and the hero must finish the day on a legal land tile.

Town Portal is a return spell rather than universal redeployment. It always sends the hero to the nearest controlled town according to the game's deterministic adventure-map distance calculation. The player does not freely select another town. Casting Town Portal consumes the hero's remaining Movement for the day.

Fly permits traversal over normally impassable terrain for the remainder of the day. Every flying step costs 1.50x the normal step cost; the hero must finish the day on a legal ordinary destination. Scenario-defined or map-editor-protected barriers remain impassable. Dimension Door is the strongest Adventure Spell and therefore the most constrained. It teleports to a visible legal tile within 8 adventure-map tiles, cannot cross scenario-defined protected barriers, and consumes the hero's remaining Movement for the day. The shared oneAdventure-Spell-per-day rule prevents teleport chains.

# Town Magical Infrastructure

### Universal Mage Guild depth

Every New Horizons faction may now construct all five Mage Guild levels. Maximum Guild depth is no longer used to make Might factions artificially less magical; faction identity is instead expressed through Primary Attributes, Skill probabilities, school pairings, spell availability, and unique buildings. This universal five-level structure is also required so every faction can eventually unlock the complete Adventure Spell progression.

|**Town**|**New Horizons Mage Guild**|**Change from Heroes III Complete**|
|---|---|---|
|Castle|I–V|Adds Level V|
|Rampart|I–V|No maximum-level change|
|Tower|I–V|No maximum-level change|
|Inferno|I–V|No maximum-level change|
|Necropolis|I–V|No maximum-level change|
|Dungeon|I–V|No maximum-level change|
|Stronghold|I–V|Adds Levels IV and V|
|Fortress|I–V|Adds Levels IV and V|
|Conflux|I–V|No maximum-level change|

### Tower and Conflux magical identity

Tower is the most magic-centered town overall. Its Sorcery/Havoc school pairing, Metamagic, Library, Mana infrastructure, artifact economy, and scholarly buildings should make it the clearest civilization of formal magical study. Conflux is the closest rival, but expresses magical breadth through the House of Wisdom's spell-scroll economy and elemental identity rather than duplicating Tower's institutions.

Unique buildings are balanced for faction identity rather than artificial cross-faction symmetry. Equal strategic value is desirable; identical building categories are not. Some factions train heroes, others manipulate resources, information, logistics, recruitment, or battlefield preparation.

### Tower creature rebalance

#### Academy visual identity

The supplied October 2026 Academy art handoff replaces Tower's snowy town
presentation with a Persian/Central Asian scholarly civilization: warm sandstone,
astronomy, alchemy and magical engineering. Academy is the displayed faction
name; the existing Tower faction identity, hero classes, creature identities,
Sorcery/Havoc pairing and Metamagic remain unchanged. Integrate the authored town,
guild, recruitment, construction, adventure-map, siege and puzzle resources by
their existing semantic roles, preserving the Mage/Genie dwelling changes below.
The art handoff is not permission to restore its bundled upstream gameplay
configuration. Academy's native terrain is Sand, replacing Tower's Snow
affinity; this applies to the faction's normal native-terrain mechanics.
Changing that affinity does not repaint terrain on existing authored maps.
Retained purchaser-original imagery must remain external game-resource references,
not redistributed raster copies. Construction stages, ownership colours, clicking
and damage states must remain functional rather than becoming a flattened scene.

Magi and Arch Magi return to the stronger offensive role associated with Heroes II. They are intended to be premium Elite shooters: more dangerous and more expensive than Liches in focused ranged combat, while Liches retain their own area-pressure identity. Genies are correspondingly shifted downward in raw offense and toward mobility/support. These are prototype values pending the full creature-stat pass.

Both Magi and Arch Magi use ordinary shooter melee penalties; neither has No
Melee Penalty. For presentation, Magi retain their existing robe colours, while
Arch Magi wear grey robes inspired by Heroes II. Both fire red rather than green
magic, and the Arch Magi's staff-top glow is red. These colour changes do not
change their other abilities, animation timing or creature identities. Retained
original sprites remain external references; colour treatments must not require
redistributing extracted purchaser artwork.

New Horizons renames Tower's Alchemist hero class to Battle Mage. Mage and Genie dwelling levels, building-card positions, costs, prerequisites, recruitment presentation, and upgrade dependencies are swapped as one coherent town change; both creature lines remain Elite. The Library stays associated with Mage and Arch Mage growth and follows the Mage dwelling's resulting position and prerequisites rather than the old Genie slot.

New Horizons Solmyr remains a Wizard and begins with Metamagic, Havoc Magic, and Stormcaller. He begins with Master Chain Lightning instead of ordinary Chain Lightning and cannot learn the ordinary version. Master Chain Lightning retains ordinary Chain Lightning's Mana cost and first-target damage, loses less damage on later jumps, and improves jump retention as Solmyr gains levels. Legacy-mode Solmyr is unchanged. This is the first authored three-starting-development profile; do not invent third starting choices for other heroes before their profiles are authored.

|Creature|Prototype stats / economy|Role and abilities|
|---|---|---|
|Mage|Attack 13; Defense 9; Damage 11-14; HP 30; Shots 16; Growth 3; 600 Gold|Ignores ranged distance penalty. While at least one Mage/Arch Mage stack is present, ordinary combat Spells cost 2 less Mana after Wisdom, minimum 1; this does not stack across Mage stacks. Magi retain the ordinary shooter melee penalty.|
|Arch Mage|Attack 15; Defense 10; Damage 13-17; HP 35; Shots 20; Growth 3; 800 Gold|Ignores ranged distance and obstacle penalties. Retains the same non-stacking 2-Mana ordinary combat-Spell discount and the ordinary shooter melee penalty.|
|Genie|Attack 10; Defense 10; Damage 8-11; HP 35; Growth 4; 450 Gold|Mobile Elite support creature. Raw offense is intentionally below the Mage line.|
|Master Genie|Attack 11; Defense 11; Damage 9-12; HP 40; Growth 4; 550 Gold|Keeps beneficial spellcasting/support identity while remaining less efficient as a pure damage dealer than Arch Magi.|



The intended contrast is simple: Lich = cheaper, tougher area-pressure shooter; Arch Mage = expensive precision magical artillery. Genie = mobile support rather than the numerically superior next Elite.

### Unique-building rebalance

The following is the current faction-identity pass. Permanent visiting-hero bonuses are tracked per physical building, not once per faction or once per scenario: a hero may benefit once from each individual qualifying building they visit. Conquering and developing additional towns can therefore create additional training opportunities.

A unique building that provides a bonus only while defending its town should also provide a meaningful strategic, economic, training, or visiting effect so it remains worth constructing even when the town is never attacked. Growth-only structures remain growth structures and will receive final numbers during the creature-growth balance pass.

###### Castle

|Building|New Horizons effect|
|---|---|
|Brotherhood of the Sword|Defending creatures gain +2 Morale. Each individual Brotherhood grants a visiting hero +100 permanent Leadership once. There is no faction-wide or scenario-wide cap.|
|Stables|A hero who begins the day in this town gains +20% maximum land Movement for that day. This uses the hero-based Movement system and cannot be increased by dropping slow creatures.|
|Lighthouse|Heroes embarking from this town pay no embarkation Movement penalty and gain +20% sea Movement for that day.|
|Griffin Bastion|Retains its Griffin-growth role; exact growth is set in the creature-economy pass.|

###### Rampart

|Building|New Horizons effect|
|---|---|
|Treasury|At the start of each week, produces 10% interest on current Gold, capped at +2,000 Gold per Treasury. The cap prevents runaway exponential scaling while preserving Rampart prosperity.|
|Mystic Pond|At the start of each week, produces 2 random precious resources. The result is revealed immediately when the week begins.|
|Fountain of Fortune|Defending creatures gain +3 Luck. A visiting hero also gains +2 Luck for the next combat; each Fountain can grant this visiting blessing to a given hero once per week.|
|Growth structures|Miners' Guild / Dendroid Saplings and similar structures remain factional creature-growth infrastructure; exact numbers are deferred to the growth pass.|

###### Tower

|Building|New Horizons effect|
|---|---|
|Library|Increases weekly Mage / Arch Mage growth by +1. The Library no longer adds Mage Guild spells. It is deliberately a very expensive late-game magical-education investment; exact experimental cost and prerequisites are listed in Experimental Values.|
|Arcane Reservoir|Uses the existing Wall of Knowledge graphics. Once per week, the Reservoir grants one visiting hero +50 Buffer Spell Points. It does not refill missing Normal Spell Points, alter Knowledge, or change maximum capacity.|
|Astronomy Tower|Replaces the Lookout Tower function. Reveals the next Astrology Week one week before it begins, giving Tower advance strategic information without altering the result.|
|Artifact Merchants|Retained as part of Tower's magical equipment economy.|

###### Inferno

|Building|New Horizons effect|
|---|---|
|Castle Gate|Teleports a hero between controlled Inferno towns that both contain Castle Gates. Usable once per hero per day; using it consumes all remaining Movement for that day.|
|Order of Fire|Each individual Order of Fire grants a visiting hero +5 permanent Spell Power once.|
|Brimstone Stormclouds|Defending hero gains +20 Spell Power during a siege. The building also produces +1 Sulfur per day, so it remains useful outside defense.|
|Birthing Pool / Cages|Retain creature-growth roles; exact growth values are deferred to the creature-economy pass.|

###### Necropolis

|Building|New Horizons effect|
|---|---|
|Necromancy Amplifier|A visiting Necropolis hero gains +10 percentage points to Necromancy raising for 7 days. Multiple Amplifiers do not stack; visiting another refreshes the duration.|
|Skeleton Transformer|Conversion pools aggregate sacrificed HP across all selected stacks in one transaction. Prototype output: floor(50% of sacrificed aggregate HP / Skeleton HP) whole Skeletons; leftover HP is discarded. A zero-output conversion is rejected without changing the army. Fill existing Skeleton stacks within their Leadership limits, then use slots freed by the sacrifice; if any resulting Skeletons still cannot fit, reject the entire transaction without losing any creatures.|
|Cover of Darkness|Retains information denial and re-shrouding. It is more strategically relevant because Dimension Door requires a visible legal destination.|
|Unearthed Graves / growth structures|Retain Undead-growth functions; exact growth values are deferred.|

###### Dungeon

|Building|New Horizons effect|
|---|---|
|Astral Nexus|Uses the existing Mana Vortex graphics: the visual remains a luminous, cosmic swirling phenomenon rather than becoming a brick-built hall. Whenever a hero visits, the Astral Nexus automatically restores that hero's Normal Spell Points to the current maximum. It grants no Buffer Spell Points and has no per-hero or weekly use limit.|
|Battle Scholar Academy|Each individual Academy may train a hero once, granting Experience equal to 25% of the amount currently required to reach the next level.|
|Portal of Summoning|Once each week, choose one owned external dwelling. Its current recruit pool becomes accessible from the Dungeon, but recruitment through the Portal deducts those creatures from the external dwelling; it never duplicates troops.|
|Artifact Merchants|Retained as part of Dungeon's occult-artifact economy.|

###### Stronghold

|Building|New Horizons effect|
|---|---|
|Hall of Valhalla|Each individual Hall grants a visiting hero +5 permanent Attack once.|
|Ballista Yard|Sells Ballistae through the shared town War Machine shop; the Ballista appears only once and uses the lowest applicable configured price. A visiting hero gains +20 Siege until the end of the current week; revisiting refreshes but does not stack the bonus. Buying a Ballista neither grants nor consumes the visit effect.|
|Freelancer's Guild|Retains creature-for-resource trading as a martial-economy tool.|
|Escape Tunnel|Retains the ability to retreat from a siege where retreat would normally be impossible.|
|Mess Hall / growth structures|Retain creature-growth functions; exact growth values are deferred.|

###### Fortress

|Building|New Horizons effect|
|---|---|
|Blood Obelisk|Defending hero gains +20 Attack during a siege. A visiting hero may also receive a battle blessing once per week from that Obelisk: friendly creatures deal +10% physical damage in the hero's next combat.|
|Glyphs of Fear|Defending hero gains +20 Defense during a siege. In addition, enemy heroes within 8 adventure-map tiles of the Fortress suffer -1 Morale while they remain in the area.|
|Captain's Quarters|Retains its creature-growth role; exact growth values are deferred.|
|Shipyard|Retained.|

###### Conflux

|Building|New Horizons effect|
|---|---|
|House of Wisdom|Each House of Wisdom generates a persistent stock of six distinct eligible combat-spell scrolls. Visiting heroes may buy an offered scroll for 1,000 Gold per spell level; a purchased scroll is removed from that House's stock. It teaches no Skill and never offers Adventure Spells, removed spells, or spells prohibited by the saved roster or map.|
|Artifact Merchants|Retained.|
|Garden of Life / Vault of Ashes|Retain creature-growth roles; exact growth values are deferred.|
|Shipyard|Retained.|

The four classical Elemental Altars are independent Elite recruitment lines.
Each base Altar requires Mage Guild level 1, not another Elemental Altar. Each
upgrade requires its own base Altar and Mage Guild level 2, with no cross-line
Altar prerequisite. These requirements do not change the Pixie, Wisp or Phoenix
dwelling rules.

|Altar line|Base cost|Upgrade cost|
|---|---|---|
|Air / Storm|2,500 Gold, 5 Wood, 5 Ore, 2 Gems|2,500 Gold, 5 Wood, 5 Ore, 3 Gems|
|Water / Ice|2,500 Gold, 5 Wood, 5 Ore, 2 Mercury|2,500 Gold, 5 Wood, 5 Ore, 3 Mercury|
|Fire / Energy|2,500 Gold, 5 Wood, 5 Ore, 2 Sulfur|2,500 Gold, 5 Wood, 5 Ore, 3 Sulfur|
|Earth / Magma|2,500 Gold, 5 Wood, 5 Ore, 2 Crystal|2,500 Gold, 5 Wood, 5 Ore, 3 Crystal|

Building all four base Altars costs 10,000 Gold, 20 Wood, 20 Ore and 2 of each
rare resource. Including all four upgrades, the cumulative cost is 20,000 Gold,
40 Wood, 40 Ore and 5 of each rare resource; these totals exclude Mage Guild
construction.



###### Asset continuity

Unique-building mechanics may change without unnecessarily swapping town-screen assets. Tower's existing Wall of Knowledge art becomes Arcane Reservoir. Tower's existing Lookout Tower art becomes Astronomy Tower. Dungeon's existing Mana Vortex art becomes Astral Nexus. The Wall of Knowledge and Mana Vortex graphics are not swapped between factions.

# Attributes

###### Terminology

To avoid the old and confusing distinction between _Primary Skills_ and _Secondary Skills_ , New Horizons uses three separate terms:

- **Primary Attributes — Attack, Defense, Spell Power, and Knowledge. These are the hero's fundamental class-defined values and grow automatically with level.**

- - **Secondary Attributes — measurable current capabilities such as Mana, Movement, Leadership, Siege, Luck, and Morale. They may be derived from Primary Attributes, class, level, equipment, Skills, or other systems, depending on the Secondary Attribute.**

- **Skills** — learned disciplines such as Offense, Armorer, Logistics, Wisdom, Command, and similar abilities chosen during hero development.

This section concerns Primary Attributes only.

# Primary Attributes

##### **Introduction**

The redesign abandons Heroes III's opaque level-up probability tables in favor of deterministic class growth while preserving a controlled amount of variation through Skills.

Every hero class has a fixed four-value **growth profile** , written in the order:

Attack / Defense / Spell Power / Knowledge

The four growth values always total **20 points per level** . A class may be highly specialized, balanced between two Attributes, or broadly hybridized. Ties are intentional: a class does not need a unique "primary", "secondary", "tertiary", and "quaternary" Attribute.

For example:

- Knight: 6 / 9 / 2 / 3

- Ranger: 7 / 7 / 3 / 3

- Wizard: 1 / 1 / 9 / 9

- Barbarian: 11 / 7 / 1 / 1

- Warlock: 3 / 1 / 12 / 4

The broader scale allows class strengths and weaknesses to be expressed much more clearly. A value of 1 represents an Attribute a class barely develops naturally, while values above 10 are reserved for truly defining extremes.

This makes natural development completely predictable. A Knight will always develop Defense most strongly, with Attack as a clear secondary strength. A Barbarian develops almost entirely as a martial hero, while a Wizard develops Spell Power and Knowledge equally and gains almost nothing in either martial Attribute. A Warlock reaches exceptional Spell Power at the cost of extremely poor Defense.

Randomness is not removed; it is relocated into specialization. Relevant Skills may independently award bonus Attribute points when the hero levels up. At **Basic / Advanced / Expert** , the associated Skill has a **10% / 20% / 30%** chance to grant +1 to its associated Attribute. Each Skill rolls independently, so one level-up may grant several bonus points or none at all.

These bonus rolls are additional to the class's fixed 20-point growth and do not alter its underlying growth profile.

The result is a system in which **class identity is deterministic while individual heroes still diverge through specialization** . Two Knights follow the same natural trajectory, but one who invests heavily in offensive Skills may gradually acquire additional Attack while another develops further toward Defense.

##### **Starting Attributes and Growth**

New Horizons uses a single mathematical relationship between a class's starting Attributes and its growth profile:

###### **Starting Attribute = 5 × Growth**

Equivalently:

###### **Growth = Starting Attribute ÷ 5**

Because every growth profile totals **20** , every hero begins with exactly **100 total Attribute points** and gains exactly **20 deterministic Attribute points per level** .

This means the starting profile and long-term development are two expressions of the same class identity. A class that begins exceptionally strong in an Attribute will also continue developing that Attribute rapidly, while a starting weakness remains a natural weakness throughout progression unless the player deliberately compensates for it through Skills, artifacts, or other effects.

For any Attribute with growth value g , its base value at level L is:

###### **Attribute(L) = g × (L + 4)**

before Skill bonus rolls, artifacts, scenario modifiers, or other external effects.

This produces the following universal Attribute scale:

|Growth|Level 1|Level 10|Level 20|Level 30|
|---:|---:|---:|---:|---:|
|1|5|14|24|34|
|2|10|28|48|68|
|3|15|42|72|102|
|4|20|56|96|136|
|5|25|70|120|170|
|6|30|84|144|204|
|7|35|98|168|238|
|8|40|112|192|272|
|9|45|126|216|306|
|10|50|140|240|340|
|11|55|154|264|374|
|12|60|168|288|408|
|1|5|14|24|34|
|2|10|28|48|68|
|3|15|42|72|102|
|4|20|56|96|136|
|5|25|70|120|170|
|6|30|84|144|204|
|7|35|98|168|238|
|8|40|112|192|272|
|9|45|126|216|306|
|10|50|140|240|340|
|11|55|154|264|374|
|12|60|168|288|408|



The growth values therefore have a stable design meaning:

- **1** — defining weakness

- **2–3** — weak

- **4–5** — secondary capability

- **6–7** — strong

- **8–9** — major specialization

- **10** — extreme specialization

- **11–12** — defining class extreme

A 1 is an intentional weakness, not merely the bottom of the scale. Likewise, values of 11 or 12 are reserved for Attributes that fundamentally define how a class develops.

###### <u>Attribute Advancement Sheet</u>

##### **Class Attribute Profiles**

|**Faction**|**Class**|**Type**|**Start A / D / SP / K**|**Growth A / D / SP / K**|
|---|---|---|---|---|
|Castle|**Knight**|Might|**30 / 45 / 10 / 15**|**6 / 9 / 2 / 3**|
|Castle|Cleric|Magic|**10 / 15 / 30 / 45**|**2 / 3 / 6 / 9**|
|Rampart|Ranger|Might|**35 / 35 / 15 / 15**|**7 / 7 / 3 / 3**|
|Rampart|Druid|Magic|**5 / 10 / 30 / 55**|**1 / 2 / 6 / 11**|
|Tower|Battle Mage|Might|**30 / 20 / 20 / 30**|**6 / 4 / 4 / 6**|
|Tower|Wizard|Magic|**5 / 5 / 45 / 45**|**1 / 1 / 9 / 9**|
|Inferno|Tyrant|Might|**55 / 20 / 20 / 5**|**11 / 4 / 4 / 1**|
|Inferno|Cultist|Magic|**20 / 5 / 50 / 25**|**4 / 1 / 10 / 5**|
|Necropolis|Death Knight|Might|**45 / 20 / 30 / 5**|**9 / 4 / 6 / 1**|
|Necropolis|Necromancer|Magic|**5 / 20 / 50 / 25**|**1 / 4 / 10 / 5**|
|Dungeon|Overlord|Might|**50 / 25 / 20 / 5**|**10 / 5 / 4 / 1**|
|Dungeon|Warlock|Magic|**15 / 5 / 60 / 20**|**3 / 1 / 12 / 4**|
|Stronghold|Barbarian|Might|**55 / 35 / 5 / 5**|**11 / 7 / 1 / 1**|
|Stronghold|Shaman|Magic|**45 / 5 / 30 / 20**|**9 / 1 / 6 / 4**|
|Fortress|Beastmaster|Might|**35 / 55 / 5 / 5**|**7 / 11 / 1 / 1**|
|Fortress|Witch|Magic|**5 / 15 / 20 / 60**|**1 / 3 / 4 / 12**|
|Conflux|Planeswalker|Might|**35 / 20 / 30 / 15**|**7 / 4 / 6 / 3**|
|Conflux|Elementalist|Magic|**5 / 5 / 60 / 30**|**1 / 1 / 12 / 6**|



No two classes share the same growth profile.

Unlike the earlier system, the roster is not constrained to a small number of predefined profile families. Every class instead follows the same mathematical rules while receiving the distribution appropriate to its identity:

- Starting Attributes total **100** .

- Growth totals **20 per level** .

- Every starting Attribute is exactly **five times its growth value** .

- Every class has a unique growth profile.

- Values of 1 represent deliberate weaknesses.

- Values above 10 are reserved for defining extremes.

- Might and Magic describe the class's general orientation, but do not impose a rigid statistical template.

This allows conventional archetypes such as the Knight, Barbarian, and Wizard to coexist with more unusual hybrids such as the Battle Mage, Death Knight, Shaman, and Planeswalker.

##### **Attributes as Ratings**

The new Attribute scale is substantially larger than original Heroes III advancement, so the original combat formulas cannot be retained unchanged.

Attack, Defense, Spell Power, and Knowledge retain their familiar names, but mechanically they function as **ratings** interpreted by the systems that consume them.

A level-30 Knight, before Skill bonus rolls, artifacts, scenario modifiers, or other external effects, has:

- Attack: **204**

- Defense: **306**

- Spell Power: **68**

- Knowledge: **102**

Those values are intentionally large by Heroes III standards. The mistake would be to interpret 200 Attack or 300 Defense as though those values meant the same thing under the original game's combat formulas.

The relevant question is not the absolute size of an Attribute rating, but how strongly the mechanic using that rating converts it into an actual effect.

###### **Knowledge**

Knowledge determines the hero's normal Spell Point capacity directly:

###### **Maximum Normal Spell Points = effective Knowledge**

Spell Points are a Statistic, not an Attribute. Normal and Buffer Spell Points are tracked separately but spent as one currency.

This makes Knowledge particularly transparent: without Intelligence, one point of effective Knowledge equals one point of normal Spell Point capacity.

Total Available Spell Points = Normal Spell Points + Buffer Spell Points. Normal Spell Points never exceed the current maximum; Buffer Spell Points may exceed it and are ordinary spendable Spell Points rather than a second currency.

Spell costs consume Buffer first and then Normal. Granting Buffer never restores missing Normal Spell Points. Ordinary restoration fills Normal Spell Points only and neither consumes nor replaces Buffer unless an effect explicitly says otherwise.

Capacity changes are event-driven. Increasing Knowledge or equipping a capacity artifact creates empty capacity rather than restoring points. Whenever an effect reducing maximum capacity is removed, excess Normal Spell Points are lost immediately; Buffer is unaffected. Reequipping restores capacity, not the discarded points. Saves defensively normalize this state after the complete bonus tree is loaded.

Magic Spring restores Normal Spell Points to maximum and grants +25 Buffer Spell Points. It never doubles the current pool or maximum. Town and Mage Guild rest, Magic Wells, immediate-refill buildings, regeneration, Mysticism-style effects, and ordinary restoration affect Normal only unless they explicitly grant Buffer.

The familiar display shows total spendable points over normal maximum, followed by a yellow Buffer annotation already included in the total—for example, 310 / 460  +50 Buffer. Expanded help explains both pools and Buffer-first spending without disclosing hidden enemy capacity.

The broader growth scale creates substantial differences in magical endurance between classes.

For example:

- A Knight begins with **15 Mana** and reaches **72 Mana** at level 20.

- A Wizard begins with **45 Mana** and reaches **216 Mana** at level 20.

- A Witch begins with **60 Mana** and reaches **288 Mana** at level 20.

Knowledge therefore determines how much magical activity a hero can sustain rather than how powerful an individual spell is.

Because the revised Attribute scale produces much larger Mana pools than the earlier system, spell costs must be calibrated against the new scale rather than inherited unchanged.

###### **Spell Power**

Spell Power is a rating consumed by spell-specific formulas rather than a universal multiplier.

Different spells use different coefficients, caps, thresholds, durations, percentage relationships, or non-linear rules according to their intended function.

For example, Sorcery's **Magic Arrow** may use:

###### **Base Damage = Base Damage + coefficient × Spell Power**

before applying its separate Overcharge mechanic.

A large-area spell may use relatively gentle Spell Power scaling, while a focused single-target spell, summon, barrier, duration effect, or percentage-based spell may convert Spell Power differently.

There should be no universal rule such as "+1% effect per Spell Power."

The larger rating scale provides additional numerical granularity. Spell-specific coefficients determine how much of that rating is actually converted into battlefield power.

###### **Attack and Defense**

Attack and Defense are not passively added to creature statistics.

Their value is projected through mechanics explicitly designed to consume those ratings, principally **Orders** and relevant **Faction Skills** .

A high-Attack hero therefore does not automatically increase all creature damage merely by being present. Attack represents offensive military potential; the player decides when and how to project that potential through battlefield actions.

Likewise, Defense represents defensive command potential rather than a permanent army-wide damage-reduction bonus.

For example, the current **Charge!** structure is:

###### **Damage Bonus = Base + Attack coefficient × Attack + movement component**

while **Hold the Line!** follows:

###### **Damage Reduction = Base + Defense coefficient × Defense**

The exact coefficients remain balance parameters.

This is particularly important under the new scale. A coefficient calibrated around the previous 10-point growth system cannot simply be carried over when a level-30 Knight now possesses **306 Defense** instead of 136. The formulas must be recalibrated around the new rating ranges.

The systemic rule, however, remains unchanged:

###### **Attributes feed formulas; they are not transferred raw into creature statistics.**

##### **Design Principle**

The purpose of the Attribute system is to separate **class identity** from **build specialization** .

A hero's class determines a completely predictable natural trajectory through its fixed growth profile. Because starting Attributes are directly derived from that same profile, the class's identity is already visible at level 1 and remains structurally consistent throughout development. Skills then provide controlled long-term divergence through specialization and bonus Attribute rolls. Artifacts, scenario modifiers, and other systems may further modify final values without changing the hero's underlying class identity.

Thus, a Barbarian naturally remains an extreme martial hero, a Wizard remains an extreme magical generalist, and a Warlock remains an exceptional Spell Power specialist. Player development modifies those identities rather than replacing them.

The important balancing unit is therefore no longer "one point of Attack" or "one point of Spell Power" in isolation.

It is the amount by which one point of an Attribute changes the final effect of the **Order, spell, Faction Skill, or other mechanic that consumes that rating** .

The Attribute scale provides granularity and establishes class identity.

The consuming mechanics determine actual power.

# Secondary Attributes

Secondary Attributes describe current measurable capabilities rather than class-defined natural growth. They include Mana, Movement, Leadership, Siege, Luck, and Morale. Unlike Primary Attributes, they do not share one universal progression rule; each Secondary Attribute is defined by the system that uses it.

###### Luck and Morale

Luck and Morale are Secondary Attributes with a fixed legal range from −10 to +10. A value of 0 is neutral. Any effect that raises or lowers either value is clamped to this range.

The legal range remains -10 to +10. The current test curve and trigger consequences are defined in Experimental Values; they are deliberately isolated there so they can be tuned without changing the rest of the Luck and Morale architecture.

# Leadership

Leadership is a Secondary Attribute that limits the size of each creature stack a hero may command. It is an army-capacity rating, not a summed army-wide resource.

###### Core rule

Each of the seven army slots is checked independently against the hero's Leadership. There is no summed Leadership budget across the whole army.

Maximum creatures in a slot = floor(Hero Leadership ÷ Creature Leadership Requirement) Removing creatures from one slot does not create capacity in another slot. If a hero can command 29 Pikemen in one slot and 1 Angel in another, both stacks may be present at the same time if both are individually legal.

###### Progression and tuning

Starting Leadership and per-level Leadership growth remain independent balance parameters. The current class-based experimental progression is defined in Experimental Values and follows each class's Attack + Defense growth polarity.

The experimental formula gives more Leadership to naturally martial classes without involving Command or percentage discounts. Exact class values are tabulated in Experimental Values. Every creature line now has an experimental Leadership Requirement in Experimental Values. Upgraded creatures use a 20% higher requirement, rounded to the nearest 10, unless a creature is explicitly listed as an exception.

Repeated creature types are a separate army-composition rule. If repetition is permitted, every repeated stack is checked independently. Leadership can tune stack size, but it cannot by itself prevent a player from filling several slots with the same creature type.

###### Relationship with Command

Command does not reduce Creature Leadership Requirements and does not directly increase Leadership capacity. Command specializes Orders; Leadership governs army capacity. This separation prevents one Might-exclusive Skill from simultaneously controlling both battlefield Orders and the size of the army.

###### Implementation

Keep starting Leadership, per-level growth, Creature Leadership Requirements, and an optional global leadershipScale as separate configuration values. Recalculate Leadership from level rather than accumulating rounded gains.

If leadershipScale is used: Effective Leadership = floor([Starting Leadership + Growth × − (level 1)] × leadershipScale). Creature requirements remain unchanged when leadershipScale changes.

Second Wind! uses the current experimental scaling: Second Wind Damage = min(100%, 50% + 0.015% x Leadership). This coefficient belongs to Experimental Values and may move during army-scale testing.

# Siege

##### **Secondary Attribute — Siege**

**Siege** is a Secondary Attribute representing the hero's mastery of war machines, military engineering, and fortified defenses.

Base Siege is 0 unless modified by a Skill, artifact, hero specialty, town effect, scenario rule, or another explicit source. War Machines is the principal Skill that raises Siege.

Conceptually, Siege serves the same role for war machines that **Spell Power** serves for spells.

Each applicable war machine or defensive structure converts the hero's Siege rating through its own coefficient:

Effect = Base Effect + k × Siege

There is no universal Siege multiplier. Different machines use different coefficients according to their function and balance requirements.

Siege affects, among other things:

- **Ballista damage**

- **Catapult damage against fortifications**

- **First Aid Tent healing**

- **Defensive tower damage when defending a fortified town**

- future war machines or engineering devices with numerical effects

For example:

Ballista Damage = B_b + k_b × S Catapult Damage = B_c + k_c × S Tent Healing = B_h + k_h × S Tower Damage = B_t + k_t × S

where S is the hero's Siege rating.

The distinction between **Siege** and the **War Machines Skill** is important:

###### **Siege determines how powerful a war machine is.**

**War Machines determines how effectively and flexibly the hero can use war machines.**

Siege should therefore primarily modify quantitative effects such as damage, healing, and repair. It should not inherently determine targeting, additional attacks, special ammunition, casualty restoration, or other qualitative abilities. Those belong to Skills and perks.

When defending a fortified town, the defending hero's Siege rating is used by the town's defensive towers. A town without a defending hero still uses its normal base tower damage; the hero adds their Siege-derived contribution.

Fortification durability itself—wall HP, gate HP, number of towers, moat characteristics, and similar properties—is primarily determined by the town and its buildings rather than the hero's Siege rating.

# Skills

New Horizons treats Skills as broad learned disciplines. Every Skill has Basic, Advanced, and Expert ranks plus a ten-perk pool; a hero may learn at most three perks from any one Skill. Unless a perk says otherwise, Basic perks require Basic Skill, Advanced perks require Advanced Skill, and Expert perks require Expert Skill. All perks follow the Passive Perk Principle defined in Core Rules: a perk may modify or automatically react to existing mechanics, but may not create a separate player-activated ability, toggle, mode, target-selection step, or discretionary trigger.

The numerical values in this section are canonical prototypes: the mechanical boundaries and ownership of each effect are intentional, while coefficients may still be tuned during implementation and playtesting.

### Canonical Skill roster

|**Skill**|**Domain**|**Availability**|
|---|---|---|
|Offense|Martial|All heroes|
|Armorer|Martial|All heroes|
|Archery|Martial|All heroes|
|Battlecraft|Martial|All heroes|
|War Machines|Martial|All heroes|
|Discipline|Martial|All heroes|
|Recruitment|Martial|All heroes|
|Command|Martial|Might-exclusive|
|Light Magic|Magic|All heroes|
|Shadow Magic|Magic|All heroes|
|Nature Magic|Magic|All heroes|
|Havoc Magic|Magic|All heroes|
|Sorcery Magic|Magic|All heroes|
|Chaos Magic|Magic|All heroes|
|Spellcraft|Magic|All heroes|
|Wisdom|Magic|Magic-exclusive|
|Warcasting|Hybrid|All heroes|
|Logistics|Strategic|All heroes|
|Diplomacy|Strategic|All heroes|
|Estates|Strategic|All heroes|
|Learning|Strategic|All heroes|
|Luck|Strategic|All heroes|
|Divine Mandate|Faction|Castle heroes only|
|Sylvan Luck|Faction|Rampart heroes only|
|Metamagic|Faction|Tower heroes only|



|**Skill**|**Domain**|**Availability**|
|---|---|---|
|Shroud of Malassa|Faction|Dungeon heroes only|
|Demonic Gating|Faction|Inferno heroes only|
|Necromancy|Faction|Necropolis heroes only|
|Bloodrage|Faction|Stronghold heroes only|
|Bulwark of the Mire|Faction|Fortress heroes only|
|Elemental Rebirth|Faction|Conflux heroes only|



### Legacy Heroes III Skill migration

|**Original skill**|**New Horizons destination**|
|---|---|
|Air / Earth / Fire / Water Magic|Replaced by Light, Shadow, Nature, Havoc, Sorcery, and<br>Chaos Magic.|
|Archery|Retained as Archery.|
|Armorer|Retained as Armorer.|
|Artillery / Ballistics / First Aid|Merged into War Machines.|
|Diplomacy|Retained and made deterministic.|
|Eagle Eye|Learning perk.|
|Estates|Retained as Estates.|
|Intelligence|Wisdom perk.|
|Leadership|Renamed Discipline; Leadership is reserved for the Secondary<br>Attribute.|
|Learning|Retained and expanded.|
|Logistics|Retained and expanded.|
|Luck|Retained as Luck.|
|Mysticism|Wisdom perk.|
|Navigation|Logistics perk.|
|Necromancy|Necropolis Faction Skill.|
|Offense|Retained as Offense.|
|Pathfinding|Logistics perk.|
|Resistance|Warcasting perk: Spellward.|
|Scholar|Learning perk.|
|Scouting|Logistics perk with +5 sight radius.|
|Sorcery (old secondary skill)|Rebuilt as the generic Spellcraft Skill. Sorcery remains the<br>name of one magic school.|
|Tactics|Battlecraft perk.|
|Wisdom|Magic-exclusive Skill reducing spell Mana costs and<br>developing Knowledge; School Skills govern Level 3-5 spell<br>access.|



### Magic proficiency and casting variants

Spell level access belongs to the relevant Magic School Skill. Wisdom does not unlock spell levels.

|**School proficiency**|**Maximum spell level that may be acquired through ordinary learning**|
|---|---|
|No School Skill|Level 2|
|Basic|Level 3|
|Advanced|Level 4|
|Expert|Level 5|



Ordinary school-spell learning sources obey this gate: Mage Guilds, scrolls, hero-to-hero teaching, Eagle Eye, Arcane Memory, scenario rewards, and similar effects may only grant a school spell the hero is currently qualified to learn. An explicitly granted starting or temporary inscription, such as Spellbinder's Hat, may place a higher-level spell in the book; once legitimately inscribed, that spell may be cast regardless of School proficiency, subject to all other casting rules. Adventure Spells are neutral and follow their separate fixed Mage Guild unlock rules.

Wisdom owns generic spell-cost efficiency. For an ordinary spell: Final Mana Cost = max(1, ceil(Listed Mana Cost x (1 - Wisdom Discount)))

Optional extra Mana that is intrinsic to a Spell itself - such as Magic Arrow Overcharge - is not reduced by Wisdom unless an effect explicitly says otherwise. Perks do not create optional Mana surcharges or paid casting toggles. Flat cost reductions from perks are applied after Wisdom and may never reduce a spell below 1 Mana.

Mass casting is not an automatic consequence of Expert school proficiency. A Mass variant exists only when a specific perk grants it. A perk-granted Mass variant is a distinct Spell entry in the hero’s spellcasting interface, not a toggle on the base Spell and not a conditional transformation of an ordinary cast. It is selected and cast normally like any other Spell. Unless its definition states otherwise, a Mass variant uses the same school, spell level, effect, duration, and scaling as its base Spell; its targeting scope and Mana cost are defined by the variant itself. Mass variants are granted by their perk and are not learned separately from Mage Guilds, scrolls, or other spell-learning sources.

Current perk-granted Mass variants:

Mass Bless — affects every eligible friendly stack; Mana cost = 3× Bless’s listed Mana cost before Wisdom.

Mass Curse — affects every eligible enemy stack; Mana cost = 3× Curse’s listed Mana cost before Wisdom.

Mass Sorrow — affects every eligible enemy stack; Mana cost = 3× Sorrow’s listed Mana cost before Wisdom.

Mass Regeneration — affects every eligible friendly living stack; Mana cost = 3× Regeneration’s listed Mana cost before Wisdom.

Mass Slow — affects every eligible enemy stack; applies 60% of Slow’s normal Initiativereduction magnitude; Mana cost = 3× Slow’s listed Mana cost before Wisdom.

### Class Skill offer weights

New Horizons uses per-class relative Skill weights rather than fixed displayed percentages. Every class stores a weight for every Skill, and a level-up Skill offer is drawn from the currently

eligible pool. The weight is not itself a percentage. Primary Attributes constrain the class's overall Might/Magic tendency, while faction and class identity determine how that tendency is distributed among individual Skills.

For an eligible Skill s: P(s) = weight(s) / sum(weights of all currently eligible Skills). If two Skill choices are offered at the same level-up, draw without replacement. A weight of 0 means the Skill cannot appear through ordinary level-up for that class. Class restrictions, faction restrictions, already-maxed Skills, and other legality checks are applied before the draw. Perk offers reuse these class weights through the separate compressed parent-weight transformation below, not the ordinary Skill lottery.

For perk-parent weights, take the smallest and largest strictly positive weights, w_min and w_max, from the complete class distribution, including unlearned, Expert and perk-exhausted Skills. With configurable maximum preference ratio R = 2.0, set gamma = ln(R) / ln(w_max / w_min) and W(w) = (w / w_min)^gamma. Uniform positive distributions or R = 1 give every parent weight 1. A legitimately learned zero-weight Skill receives weight 1 without changing ordinary acquisition or class/faction restrictions. R must be finite and at least 1.

Filter illegal parents before sampling and renormalize over the remaining weights. Eligibility changes the probability denominator, not the reference bounds or relative transformed weights. The number of eligible perks within a parent does not increase its parent weight. Ordinary Skill weights, perk-tier eligibility, and the mathematical preference ratio are separate rules; this transformation changes neither the XP curve nor rank prerequisites.

The authored class tables below are the authoritative relative Skill weights. The former formula derived from ten-point Primary growth and a fixed 50-point Might/Magic budget is retired; changing the current 20-point growth vectors does not silently recalculate these tables. Primary growth still informs class identity, but every weight change must be authored explicitly. Ordinary offers use the eligible weighted pool without a forced Wisdom or Magic School cadence. Strategic Skills remain independently weighted by class fantasy. The hero's own Faction Skill has weight 10; all other Faction Skills are ineligible. Command is ineligible for Magic classes and Wisdom is ineligible for Might classes.

|**Class**|**Off**|**Arm**|**Arc**|**Bat**|**WM**|**Dis**|**Rec**|**Cmd**|**WarC**|
|---|---|---|---|---|---|---|---|---|---|
|Knight|4|6|3|5|3|5|4|5|4|
|Cleric|1|3|1|2|2|4|2|0|4|
|Ranger|4|4|6|5|2|3|2|4|7|
|Druid|1|1|2|1|1|2|2|0|1|
|Battle<br>Mage|3|2|3|4|5|2|1|5|10|
|Wizard|1|1|1|1|3|2|1|0|1|
|Tyrant|8|3|2|6|2|3|5|6|4|
|Cultist|3|1|2|3|2|2|2|0|4|
|Death<br>Knight|6|4|2|5|2|4|2|5|7|
|Necromanc<br>er|1|3|1|2|3|2|3|0|4|
|Overlord|6|5|3|6|3|3|3|6|4|
|Warlock|3|1|2|3|2|1|3|0|4|
|Barbarian|8|4|5|8|2|3|5|5|1|
|Shaman|6|1|4|6|1|3|4|0|10|
|Beastmast<br>er|4|8|5|7|2|6|4|4|1|
|Witch|1|4|2|2|1|3|2|0|4|
|Planeswalk<br>er|4|2|4|6|2|2|1|4|10|
|Elementali<br>st|1|1|1|1|2|2|2|0|1|



Abbreviations: Off Offense; Arm Armorer; Arc Archery; Bat Battlecraft; WM War Machines; Dis Discipline; Rec Recruitment; Cmd Command; WarC Warcasting. Off through Cmd are the pure Might pool; WarC is hybrid and does not consume either polarity budget.

|**Class**|**SpC**|**Wis**|**Light**|**Shadow**|**Nature**|**Havoc**|**Sorcery**|**Chaos**|
|---|---|---|---|---|---|---|---|---|
|Knight|2|0|5|1|1|1|4|1|
|Cleric|6|7|8|2|2|2|6|2|
|Ranger|3|0|5|1|7|2|1|1|
|Druid|7|8|7|2|10|2|2|2|
|Battle Mage|7|0|1|1|1|7|7|1|
|Wizard|9|9|1|1|1|8|10|1|
|Tyrant|2|0|1|1|1|5|1|4|
|Cultist|6|7|1|2|1|8|2|8|
|Death Knight|4|0|1|6|1|2|3|3|
|Necromance<br>r|6|7|1|9|1|2|6|3|
|Overlord|2|0|1|4|1|5|1|1|
|Warlock|7|7|1|7|1|10|1|1|
|Barbarian|1|0|1|1|2|1|1|3|
|Shaman|4|5|1|1|6|1|1|6|
|Beastmaster|1|0|1|2|3|1|1|1|
|Witch|6|8|1|8|8|1|1|2|
|Planeswalke<br>r|7|0|1|1|7|7|1|1|
|Elementalist|9|9|1|1|8|10|1|1|



Abbreviations: SpC Spellcraft; Wis Wisdom. SpC, Wis, Light, Shadow, Nature, Havoc, Sorcery, and Chaos form the pure Magic pool. The total of these weights follows the class's Spell Power + Knowledge growth.

|**Class**|**Log**|**Dip**|**Est**|**Lrn**|**Luck**|**Faction Skill**|
|---|---|---|---|---|---|---|
|Knight|6|8|7|3|5|Divine Mandate<br>(10)|
|Cleric|4|8|6|7|5|Divine Mandate<br>(10)|
|Ranger|9|5|3|4|8|Sylvan Luck (10)|
|Druid|5|5|3|8|10|Sylvan Luck (10)|
|Battle Mage|5|4|6|7|3|Metamagic (10)|
|Wizard|3|4|7|10|2|Metamagic (10)|



|**Class**|**Log**|**Dip**|**Est**|**Lrn**|**Luck**|**Faction Skill**|
|---|---|---|---|---|---|---|
|Tyrant|7|1|5|2|4|Demonic Gating<br>(10)|
|Cultist|4|2|4|7|4|Demonic Gating<br>(10)|
|Death Knight|6|2|4|4|2|Necromancy (10)|
|Necromancer|4|1|6|9|1|Necromancy (10)|
|Overlord|8|2|5|3|5|Shroud of<br>Malassa (10)|
|Warlock|5|1|5|8|4|Shroud of<br>Malassa (10)|
|Barbarian|9|1|2|2|6|Bloodrage (10)|
|Shaman|7|3|2|6|8|Bloodrage (10)|
|Beastmaster|6|2|3|2|5|Bulwark of the<br>Mire (10)|
|Witch|5|3|4|8|7|Bulwark of the<br>Mire (10)|
|Planeswalker|10|4|3|6|9|Elemental Rebirth<br>(10)|
|Elementalist|5|4|4|9|8|Elemental Rebirth<br>(10)|



Abbreviations: Log Logistics; Dip Diplomacy; Est Estates; Lrn Learning. These strategic/universal Skills are weighted independently of Might/Magic polarity. The Faction Skill is always weight 10 for the class's own faction and is otherwise unavailable.

###### Attribute specialization

Only Skills explicitly listing an Attribute-growth chance make a bonus Primary Attribute roll at level-up. Multiple qualifying Skills roll independently.

###### No percentage discounts for army capacity

Recruitment never reduces creature Leadership requirements, and no Recruitment perk reduces creature gold costs. Recruitment works through discrete availability, training, and Muster rules.

###### Action-economy wording

A hero normally uses one Hero Action per round to cast one Spell or issue one Order. Metamagic, Divine Mandate, Double Command, and similar mechanics state their exact exception directly rather than creating action tokens.

###### Faction Skills

Faction Skills are faction-locked but not class-locked: both the Might and Magic class of a faction may develop the faction's unique Skill. They use the same Basic/Advanced/Expert and three-perk-maximum framework.

### Martial and Command Skills

###### Offense

Offense represents learned expertise in physical melee combat. It improves melee attacks and retaliations without turning the Attack Primary Attribute into a passive army-wide damage bonus.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Melee attacks and retaliations deal +10% physical<br>damage. At subsequent level-ups, Offense has a 10%<br>chance to grant +1 Attack.|
|Advanced|Melee attacks and retaliations deal +20% physical<br>damage. Attack-growth chance becomes 20%.|
|Expert|Melee attacks and retaliations deal +30% physical<br>damage. Attack-growth chance becomes 30%.|



###### Offense perk pool

|Perk|Requires|Effect|
|---|---|---|
|Shock Assault|Basic|Attacks benefiting from Charge! ignore 25% of the target's Creature Defense.|
|Encirclement|Basic|Flank! gains +7% damage per additional distinct attack side instead of +4%.|
|Pursuit|Basic|After destroying an enemy stack in melee, the attacker's Creature Activation remains open for movement using its unused movement. It gains no additional attack.|
|Executioner|Basic|Melee attacks deal +20% damage against stacks below 40% of maximum HP.|
|Armor Piercer|Advanced|All melee attacks ignore 20% of the target's Creature Defense.|
|Breakthrough|Advanced|Melee attacks ignore 50% of damage reduction granted by Defend and other explicitly mundane defensive states.|
|Cleave|Advanced|After destroying a stack in melee, automatically strike the adjacent enemy stack with the highest current aggregate HP for 50% normal damage. Ties use deterministic hex order. Once per activation; Cleave cannot trigger itself.|
|Vengeance|Advanced|While Riposte! is active, each affected stack gains one additional retaliation for the round.|
|Relentless Assault|Expert|Consecutive activations attacking the same enemy stack gain +10% damage, stacking to +30%. Attacking another target resets the bonus.|
|No Quarter|Expert|If a melee attack leaves an enemy below 25% maximum HP, it loses all remaining retaliations for the round and suffers -2 Morale until the end of its next activation.|



###### Armorer

Armorer is learned expertise in surviving physical creature combat. Defense remains a Primary Attribute projected through explicit formulas; Armorer is the army's trained protection discipline.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Physical creature attacks and retaliations against the<br>hero's army deal 5% less damage. At subsequent level-<br>ups, Armorer has a 10% chance to grant +1 Defense.|
|Advanced|Physical creature-attack reduction increases to 10%.<br>Defense-growth chance becomes 20%.|
|Expert|Physical creature-attack reduction increases to 15%.<br>Defense-growth chance becomes 30%.|



###### Armorer perk pool

|Perk|Requires|Effect|
|---|---|---|
|Shield Master|Basic|Protect! may intercept the first two qualifying melee attacks against the Ward each round instead of only the first.|
|Iron Discipline|Basic|Hold the Line! also reduces magical damage by half of its current physical damage-reduction value.|
|Countercharge|Basic|The pre-emptive attack granted by Brace! deals an additional +25 percentage points of normal damage, up to 100%.|
|Pavise|Basic|When a friendly stack Defends, ranged physical creature damage against it is reduced by an additional 25%.|
|Formation Fighting|Advanced|A stack adjacent to at least one friendly stack cannot be flanked and receives an additional 10% physical damage reduction.|
|Unyielding|Advanced|Friendly stacks that are Defending or affected by Hold the Line! cannot be forcibly displaced by non-magical effects.|
|Veteran|Advanced|At the beginning of its activation, a stack restores 15% of physical creature damage suffered since its previous activation, limited to surviving creatures.|
|Defiant|Advanced|The first non-magical enemy effect each round that would prevent a friendly stack from retaliating is ignored.|
|Last Stand|Expert|Once per combat, the first friendly stack that would be completely destroyed by a physical creature attack instead survives with one creature at 1 HP and immediately Defends. Protects ordinary stack health only, not clones or Phantom Integrity. If lethal retaliation triggers this on the acting attacker, its current activation ends.|
|Bastion|Expert|While Defending or under Hold the Line!, the first physical creature attack received each round deals 30% less final damage.|



###### Archery

Archery is the ranged counterpart to Offense: marksmanship, firing discipline, target coordination, and the ability to keep missile troops effective under pressure.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Ranged creature attacks deal +10% physical damage. At<br>subsequent level-ups, Archery has a 10% chance to grant<br>+1 Attack.|
|Advanced|Ranged creature attacks deal +20% physical damage.<br>Attack-growth chance becomes 20%.|
|Expert|Ranged creature attacks deal +30% physical damage.<br>Attack-growth chance becomes 30%.|



###### Archery perk pool

|Perk|Requires|Effect|
|---|---|---|
|Target Caller|Basic|While Focus Fire! is active, friendly shooters gain an additional +5 percentage points of damage against the designated target and ignore all obstacle penalties.|
|Skirmisher|Basic|A shooter may move up to half its Speed and then make a ranged attack at 75% normal damage during the same activation.|
|Point-Blank Shot|Basic|Shooters may use their ranged attack against adjacent enemies without the ordinary adjacent-target ranged penalty.|
|Counterfire|Basic|Once per round, after a shooter suffers ranged creature damage, it automatically makes a ranged attack against the attacker at 50% normal damage if the attacker is a legal target.|
|Armor-Piercing Shot|Advanced|Ranged attacks ignore 20% of the target's Creature Defense.|
|Suppression|Advanced|The first ranged hit made by a stack during its activation reduces the target's Speed by 1 until the target's next activation.|
|High Arc|Advanced|Ranged attacks ignore obstacle penalties and halve ordinary range penalties.|
|Crossfire|Advanced|A ranged attack deals +15% damage if another friendly shooter has already damaged the same target this round.|
|Deadeye|Expert|The first ranged attack made by each friendly shooter each round rolls maximum creature damage and ignores 25% Creature Defense.|
|Rain of Arrows|Expert|Once per activation after a ranged attack, the enemy stack adjacent to the primary target with the highest current aggregate HP suffers 35% of the actual damage dealt to the primary target. Ties use deterministic hex order; if no adjacent enemy exists, there is no secondary damage.|



###### Battlecraft

Battlecraft governs the universal tactical language of combat: Waiting, Defending, deployment, reaction timing, and movement through friendly formations. It does not replace Offense, Armorer, or Archery; it improves how troops use the battlefield itself.

Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|After a friendly stack Waits, its next attack or retaliation<br>before the end of the round deals +5% physical damage. A<br>stack that Defends gains an additional 5% physical<br>creature-damage reduction until its next activation.|
|Advanced|The conditional Wait damage bonus and Defend reduction<br>become 10%.|
|Expert|The conditional Wait damage bonus and Defend reduction<br>become 15%.|



###### Battlecraft perk pool

|Perk|Requires|Effect|
|---|---|---|
|Tactics|Basic|The army's deployment area extends two additional battlefield rows forward, subject to scenario and siege restrictions.|
|Reserve|Basic|When a stack takes its delayed activation after Waiting, it gains +2 Speed for that activation.|
|Entrench|Basic|The additional physical damage reduction granted by Battlecraft when Defending increases by +5 percentage points.|
|Overwatch|Basic|A shooter that Waits automatically makes one 50%-damage ranged reaction against the first enemy that voluntarily moves into its range before the delayed activation, if that enemy is a legal target. Once per round.|
|Pre-emptive Strike|Advanced|The first melee attack each round against a Defending friendly stack triggers a 50%-damage pre-emptive attack. This does not consume normal retaliation.|
|Redeployment|Advanced|After both armies complete initial deployment, this hero's normal deployment interface remains open for one final relocation of a friendly stack to another legal deployment hex before the first Creature Activation. No separate perk action is created.|
|Passing Lines|Advanced|Friendly stacks may move through hexes occupied by friendly stacks, provided they end movement in a legal empty position.|
|Rapid Response|Advanced|Once per round after an enemy Creature Activation ends, if friendly stacks are still Waiting, the waiting friendly stack scheduled latest in the current initiative order automatically takes its delayed activation next.|
|Grand Tactics|Expert|The hero completes initial deployment after the opponent's eligible initial deployment phase. If both heroes have Grand Tactics, initial deployment keeps the normal attacker-then-defender order. This changes initial deployment only; the later Redeployment stage is unchanged. A holder without Tactics uses the ordinary one-row area, not Tactics' two additional rows. Scenario restrictions and the ordinary deployment confirmation flow remain in force.|
|Battlefield Mastery|Expert|The first eligible ordinary friendly stack each round to Wait or Defend receives double the normal Battlecraft rank bonus for that action. An ineligible War Machine neither receives nor consumes this opportunity. Other perk contributions are not doubled.|



###### War Machines

War Machines combines the former Artillery, Ballistics, and First Aid disciplines. Siege determines machine output; War Machines raises Siege and unlocks qualitative machine capabilities.

###### War Machine availability and prices

Every town Blacksmith sells every ordinarily purchasable War Machine: the Ballista, Ammo Cart, and First Aid Tent. The Catapult remains siege equipment and is not ordinary shop inventory. Availability is therefore universal; towns differ through price rather than exclusive stock.

Each faction retains its associated Blacksmith machine for economic identity. Castle, Dungeon, and Conflux favor the Ballista; Tower, Inferno, and Stronghold favor the Ammo Cart; Rampart, Necropolis, and Fortress favor the First Aid Tent. For the first balance pass, an associated machine costs 75% of its ordinary base price, rounded to the nearest 50 Gold. Every other machine uses its ordinary configured price. The complete inventory and faction price modifiers are saved, data-driven rules shared by human purchasing and AI valuation.

A separate town building may also offer a machine. The shop presents that machine only once and uses the lowest applicable configured price. Stronghold's Ballista Yard therefore coexists with the universal Blacksmith inventory without duplicating the Ballista. Visiting the Yard grants +20 Siege until the end of the current week; revisiting refreshes but does not stack the bonus. Buying a Ballista neither grants nor consumes that visiting effect.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|+20 Siege. Grants direct control over the hero's war<br>machines.|
|Advanced|+40 Siege total.|
|Expert|+60 Siege total.|



###### War Machines perk pool

|Perk|Requires|Effect|
|---|---|---|
|Master Gunner|Basic|The Ballista fires twice during each activation. The second shot deals 60% normal damage and may target a different enemy.|
|Precision Bombardment|Basic|The Catapult may target a specific wall section, gate, or defensive tower. An accuracy miss deals no damage instead of redirecting to another part; remaining shots stop once the selected part is destroyed. Accuracy and damage-quality rolls are unchanged.|
|Surgeon|Basic|First Aid Tent healing also removes one physical affliction. Removal priority is Poison, then Disease, then Bleeding, then other eligible physical afflictions in order of application.|
|Quartermaster|Basic|Once per combat, while the Ammo Cart survives, the first allied war machine other than the Ammo Cart to complete an activation immediately receives one additional activation at 50% effectiveness.|
|Piercing Bolts|Advanced|Ballista attacks ignore 50% of the target's Creature Defense.|
|Breachmaker|Advanced|When a hero-controlled Catapult strike destroys a wall or gate section, half of excess final structural damage, rounded down, carries once into the surviving adjacent outer-line fortification section with the lowest positive current structural HP. Ties use fixed fortification order. No recursive overflow or second Siege scaling occurs.|
|Battlefield Medic|Advanced|After the First Aid Tent heals surviving creatures in its target stack, up to 50% of the Tent's calculated healing is automatically applied to restore casualties. The stack may not exceed its battle-start count.|
|Field Workshop|Advanced|The First Aid Tent may repair surviving damaged allied war machines or friendly defended-town fortifications using its full normal Siege-scaled healing value, capped by missing HP. It does not restore fallen machines or rebuild destroyed structures. Normal troop healing remains unchanged.|
|Fortification Engineer|Expert|When defending a fortified town, defensive towers use 125% of the hero's Siege rating and may be manually targeted.|
|Counter-Battery|Expert|Ballistae and defensive towers may deliberately target enemy war machines and deal increased damage against them.|

Precision Bombardment adds selected-part locking to Basic War Machines' existing manual control; it does not guarantee a hit. Unselected and captured legacy behavior remains unchanged.

Breachmaker's outer-line adjacency is bottom tower — bottom wall — below gate — gate — over gate — upper wall — upper tower. The central keep is not an outer neighbor. Carry uses damage exceeding the struck part's positive pre-hit HP. Primary tower hits, spells, creature abilities, zero excess and non-Catapult damage do not trigger it. Destroying a neighboring tower also removes its defensive shooter through the ordinary battle-state path.

Field Workshop uses full Tent output before target missing-HP caps and preserves existing reduced-output extra-activation rules. Structural tower targeting takes precedence over ordinary creature healing at that hex. Repairs do not cleanse physical afflictions or restore casualties.



###### Discipline

Discipline replaces the old Leadership Skill. Leadership is reserved for the Secondary Attribute that limits formation size; Discipline governs Morale, cohesion, steadiness, and resistance to panic.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Friendly army receives +1 Morale.|
|Advanced|Friendly army receives +2 Morale total.|
|Expert|Friendly army receives +3 Morale total.|



###### Discipline perk pool

|Perk|Requires|Effect|
|---|---|---|
|Inspirational Leader|Basic|When positive Morale triggers for a friendly stack, that stack deals +10% damage until the end of its activation.|
|Steadfast|Basic|Morale penalties applied by enemy effects are reduced by 1, to a minimum penalty of 0.|
|Rally|Basic|Once per combat, the first negative Morale trigger affecting a friendly stack is automatically canceled.|
|Esprit de Corps|Basic|Army-composition Morale penalties are reduced by 1.|
|Standard Bearer|Advanced|A friendly stack adjacent to at least one other friendly stack receives +1 Morale. This bonus does not stack with itself.|
|Fearless|Advanced|Friendly stacks are immune to explicitly non-magical fear effects.|
|Veteran Cohesion|Advanced|The first time a friendly stack falls below 50% of its maximum HP during combat, it gains +2 Morale for the rest of that combat.|
|Hold Fast|Advanced|Friendly stacks that Defend or are affected by Hold the Line! treat negative Morale as 0 until their next activation.|
|Unbreakable|Expert|The first negative Morale trigger against the army each round is ignored.|
|Heroic Spirit|Expert|After an ordinary Order, positive hero Morale may grant one additional Order-only Hero Action. Bonus Orders cannot trigger further Morale.|

Heroic Spirit checks the issuing hero's positive Morale after an accepted ordinary Order. A successful trigger grants exactly one Order-only Hero Action, not a creature activation, retaliation or unrestricted spell action. Bonus Orders, including the granted Order, cannot generate another Morale grant. The trigger uses the same current Morale curve and dice as the ordinary combat rules, with the captured Morale override where present.



###### Recruitment

Recruitment governs mustering, training, and replenishment. It deliberately does not reduce recruitment gold costs or Leadership requirements; it changes discrete availability and training instead, avoiding percentage-rounding pathologies.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Muster once per week in a friendly town: add 2 Core<br>creatures to one Core dwelling's recruitment pool.|
|Advanced|Muster once per week: add either 4 Core creatures to one<br>Core dwelling or 1 Elite creature to one Elite dwelling.|
|Expert|Muster once per week: add either 6 Core creatures, 2 Elite<br>creatures, or 1 Champion creature to one corresponding<br>dwelling pool.|



Muster is hero-limited and location-limited: one hero cannot multiply the same settlement repeatedly, and several Recruitment heroes cannot stack Muster on the same town or dwelling during the same week.

###### Recruitment perk pool

|Perk|Requires|Effect|
|---|---|---|
|Volunteer Network|Basic|When Muster targets a Core dwelling, add 2 additional Core recruits.|
|External Recruiter|Basic|The hero may spend the weekly Muster use at an owned external Core dwelling instead of a town, adding 2 Core recruits there.|
|Drill Sergeant|Basic|Core and Elite creatures recruited directly by this hero gain +1 Morale during their first actual combat from recruitment day through day+6 inclusive. Eligibility is consumed at that combat even if immunity or a cap prevents the benefit.|
|Broad Muster|Basic|When Muster targets Core creatures, its generated recruits may be split between two Core dwellings in the same town.|
|Elite Draft|Advanced|When Muster targets an Elite dwelling, add 1 additional Elite recruit.|
|Field Instructor|Advanced|Surviving original Core and Elite troops recruited directly by this hero gain +1 Creature Attack after completing their first combat under the recruiter, including retained retreat or surrender armies. The bonus lasts only while they remain continuously in that hero's army.|
|Recruiter's Contacts|Advanced|Once per week on visiting an external dwelling already owned at entry, replenish its first empty stored recruitment row with positive normal weekly growth, in stored order, using that dwelling's ordinary growth modifiers. Base and upgraded forms share the row. Consume the hero's weekly use only on a successful positive grant; capture during this visit, zero growth, no eligible row or a failed receipt does not consume it.|
|Reinforcement Drill|Advanced|Once per hero per week, the lowest original army slot among eligible newly recruited stacks entering combat under the recruiter gains +2 flat Initiative during round 1 only. Champions qualify; accepted battle entry consumes the use.|
|Champion's Call|Expert|When Expert Muster targets a Champion dwelling, add 2 Champions instead of 1.|
|Master Recruiter|Expert|The hero may use Muster twice per week, but never twice in the same town or external dwelling. A town or dwelling may still benefit from Muster only once per week.|

These three training perks apply to the whole resulting strategic stack after positive direct recruitment, including recruitment into an existing matching stack. Paid recruitment and genuine free external-dwelling recruitment qualify. Transfers, rewards, restoration, Diplomacy joins, Necromancy, summons and merely enlarging a dwelling pool do not. Category, Leadership, payment and pool validation remain unchanged.

Same-hero split, merge and slot reordering preserve compact training receipts without multiplying bonuses. Merging retains the later pending Drill Sergeant deadline. Actual cross-army movement permanently clears Field Instructor and Reinforcement Drill eligibility or benefits on moved troops; Drill Sergeant follows the troops. Temporary internal detachment is not a transfer. Later recruitment may arm a resulting stack again, never stacking the same training bonus.



###### Command

###### **Might-exclusive Skill.**

Command improves how efficiently a hero projects Attack, Defense, Leadership, and other explicitly tagged ratings through Orders. It does not increase Leadership capacity.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Attribute-derived components of Orders operate at 110%<br>Command Efficiency.|
|Advanced|Attribute-derived components operate at 120% Command<br>Efficiency.|
|Expert|Attribute-derived components operate at 130% Command<br>Efficiency.|



###### Command perk pool

|Perk|Requires|Effect|
|---|---|---|
|Aggressive Commander|Basic|Attack-derived components of Orders receive an additional +20 percentage points of Command Efficiency.|
|Defensive Commander|Basic|Defense-derived components of Orders receive an additional +20 percentage points of Command Efficiency.|
|Iron Will|Basic|Friendly stacks affected by an Order that normally expires at the end of the round retain that Order through their next Creature Activation.|
|Battle Plan|Basic|Once per combat, immediately before the first Creature Activation of round 1, issue one Order without consuming the hero's Hero Action for that round.|
|Veteran Commander|Advanced|Leadership-derived components of Orders receive an additional +25 percentage points of Command Efficiency.|
|Combined Arms|Advanced|Focus Fire! grants friendly melee attacks 50% of its damage bonus against the designated target. Flank! grants friendly ranged attacks 50% of its Attack-derived damage bonus; shooters do not add Flank sides.|
|Commanding Presence|Advanced|Friendly stacks currently affected by one of the hero's Orders treat negative Morale as 0 while that stack's Order benefit remains active. The floor ends immediately when its benefit is spent or broken, even if the Order snapshot remains until round end; examples include a consumed Charge, a broken or exhausted Protect pair, and the end of Second Wind's extra activation.|
|Crisis Command|Advanced|Once per combat, when a friendly stack is completely destroyed, immediately issue one Order without consuming the Hero Action.|
|Double Command|Expert|The first time each combat the hero uses the Hero Action to issue an Order, immediately issue one additional different Order. The same Order cannot be issued twice in that sequence.|
|Seize Initiative|Expert|The first time each combat the hero uses the Hero Action to issue an Order, the friendly stack scheduled latest in the current initiative order among those that have not completed their normal Creature Activation becomes the next friendly stack to activate after the current activation ends.|



### Magic Skills

###### Shared School-rank spell progression

The six Magic School Skills govern both ordinary acquisition and the strength of
applicable spells. A legitimately inscribed combat spell remains castable without
its School Skill; lacking the rank gives the unmodified spell rather than locking
it. Basic, Advanced, and Expert never turn a single-target spell into a Mass
spell. Mass versions require the explicit perk or other effect that grants them.

For a damaging spell with a Spell Power coefficient, School rank multiplies
**that coefficient**, not its flat base damage: no rank 100%, Basic 115%,
Advanced 130%, Expert 145%. Apply the percentage before the spell's usual final
integer rounding. Costs, target shape, damage type, mitigation, caps, and other
spell-specific rules do not change merely because of School rank. If a spell
belongs to multiple schools, use the highest applicable School rank **once**;
the schools do not stack. School-neutral Adventure Spells are unaffected.

For non-damage spells, strengthen an authored Spell Power-derived component
where that is meaningful—for example Cure's healing, but not its fixed healing
base or its affliction-removal choice. Discrete effects such as dispelling,
movement, and target selection need individually authored rank benefits; do not
multiply a boolean effect or invent an implicit Mass version. During the
incremental rollout, a spell without an authored rank benefit keeps its normal
effect, while its School rank still governs ordinary acquisition. This is an
implementation gap to close spell by spell, not a permanent exemption for an
entire school.

Individually authored rank-neutral exceptions are Sanctuary (its protection ends
at the next activation), Dispel (already removes every eligible effect on its one
target), Teleport (already reaches any legal battlefield destination), Confusion
(exactly one replaced activation with equal behavior probabilities), Berserk
(exactly one forced activation), Polymorph (a deliberately risky same-tier change
whose longer duration is not reliably beneficial), Puppet Master (one controlled
activation with its settled Lucidity limit), and Reality Warp (complete legal
exchange preserving remaining durations). Their separate perks remain effective.
These named exceptions do not exempt a School or silently cover other unauthored
discrete benefits. Curse gains its authored duration table; Purify scales its
authored removal-count term.

###### Light Magic

Light preserves: healing, blessing, divine protection, purification, restoration, and holy retaliation.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may acquire Level 3 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 115% Basic multiplier.|
|Advanced|The hero may acquire Level 4 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 130% Advanced multiplier.|
|Expert|The hero may acquire Level 5 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 145% Expert multiplier.|



Without this School Skill, ordinary sources can teach only Level 1-2 spells of the school. Basic / Advanced / Expert unlock ordinary acquisition of Levels 3 / 4 / 5 respectively. Explicit starting or temporary inscriptions may grant higher-level spells; a legitimately inscribed combat spell remains castable regardless of School rank, subject to all other casting rules.

###### Light Magic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Healer|Basic|Cure and Guardian Spirit receive +20% to their Spell Power-derived healing or temporary-HP component.|
|Benediction|Basic|Bless lasts 1 additional round.|
|Sanctuary Keeper|Basic|A friendly stack protected by Sanctuary also gains +2 Morale while Sanctuary remains active.|
|Guardian|Basic|Guardian Spirit creates 25% more temporary HP.|
|Aegis|Advanced|Holy Armor and Heavenly Gale receive +20% to their Spell Power-derived protection component.|
|Litany|Advanced|Grants Mass Bless.|
|Purifier|Advanced|Purify also removes one physical affliction from each affected friendly stack.|
|Retributionist|Advanced|Divine Retribution deals +20% reactive damage.|
|Miracle Worker|Expert|Resurrection restores 25% more casualties than its normal calculated amount.|
|Crusader|Expert|Crusade! lasts 1 additional round and affected creatures cannot suffer negative Morale while it remains active.|



###### Shadow Magic

Shadow condemns: curses, stolen vitality, supernatural affliction, sacrifice, reanimation, and execution.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may acquire Level 3 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 115% Basic multiplier.|
|Advanced|The hero may acquire Level 4 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 130% Advanced multiplier.|
|Expert|The hero may acquire Level 5 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 145% Expert multiplier.|



Without this School Skill, ordinary sources can teach only Level 1-2 spells of the school. Basic / Advanced / Expert unlock ordinary acquisition of Levels 3 / 4 / 5 respectively. Explicit starting or temporary inscriptions may grant higher-level spells; a legitimately inscribed combat spell remains castable regardless of School rank, subject to all other casting rules.

###### Shadow Magic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Malediction|Basic|Curse and Sorrow last 1 additional round.|
|Blood Drinker|Basic|Life Drain heals the friendly target for 75% of actual damage dealt instead of 60%.|
|Painweaver|Basic|Hex of Pain receives +20% to its Spell Power-derived damage component.|
|Withering Touch|Basic|Frailty strips an additional 5 percentage points of base Creature Defense per cast; its cumulative cap is unchanged.|
|Plaguebearer|Advanced|Plague may propagate one additional time beyond its normal limit.|
|Soul Binder|Advanced|Soul Chain echoes an additional 15 percentage points of secondary-victim damage into the primary target.|
|Dark Gift|Advanced|Shadow Gift requires 25% less sacrificed HP for the same offensive effect.|
|Night Feeder|Advanced|Vampirism returns an additional 15 percentage points of damage as healing.|
|Reanimator|Expert|Re-animate restores 25% more temporary casualties.|
|Grand Malediction|Expert|Grants Mass Curse and Mass Sorrow.|



###### Nature Magic

Nature governs living systems and the physical world: regeneration, vegetation, venom, beasts, terrain, and elemental convergence.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may acquire Level 3 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 115% Basic multiplier.|
|Advanced|The hero may acquire Level 4 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 130% Advanced multiplier.|
|Expert|The hero may acquire Level 5 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 145% Expert multiplier.|



Without this School Skill, ordinary sources can teach only Level 1-2 spells of the school. Basic / Advanced / Expert unlock ordinary acquisition of Levels 3 / 4 / 5 respectively. Explicit starting or temporary inscriptions may grant higher-level spells; a legitimately inscribed combat spell remains castable regardless of School rank, subject to all other casting rules.

###### Nature Magic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Herbalist|Basic|Regeneration marks an additional 10 percentage points of eligible damage as Regenerable, subject to its normal cap.|
|Rootcaller|Basic|Entangle lasts 1 additional round, to a maximum of 3 rounds.|
|Venomancer|Basic|Poison's Base Poison value is increased by 20%.|
|Beastcaller|Basic|Summon Trolls creates 25% more Troll HP.|
|Verdant Warden|Advanced|Dendroids created by Verdant Prison receive 25% more HP.|
|Geomancer|Advanced|Earthquake deals 25% more structural damage and Fractured Ground lasts 1 additional round.|
|Mire Shaper|Advanced|Quicksand creates one additional patch.|
|Verdant Communion|Advanced|Grants Mass Regeneration.|
|Worldroot|Expert|Nature's Wrath may chain through two additional valid stacks and its Spell Power-derived effect is increased by 10%.|
|Elemental Conjurer|Expert|Elemental Convergence creates 30% more Elemental HP. Elementals summoned by the spell also gain +2 Initiative during their first round on the battlefield.|



###### Havoc Magic

Havoc destroys through fire, frost, lightning, bombardment, traps, and catastrophic force.

Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may acquire Level 3 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 115% Basic multiplier.|
|Advanced|The hero may acquire Level 4 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 130% Advanced multiplier.|
|Expert|The hero may acquire Level 5 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 145% Expert multiplier.|



Without this School Skill, ordinary sources can teach only Level 1-2 spells of the school. Basic / Advanced / Expert unlock ordinary acquisition of Levels 3 / 4 / 5 respectively. Explicit starting or temporary inscriptions may grant higher-level spells; a legitimately inscribed combat spell remains castable regardless of School rank, subject to all other casting rules.

###### Havoc Magic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Pyromancer|Basic|Fireball, Fire Wall, and Inferno receive +15% to their Spell Power-derived damage components.|
|Cryomancer|Basic|Ice Bolt and Frost Ring receive +20% to their Spell Power-derived damage components.|
|Stormcaller|Basic|Lightning Bolt and Chain Lightning receive +15% to their Spell Power-derived damage components.|
|Demolitionist|Basic|Havoc spells deal +50% damage to destructible obstacles and fortifications when they can affect them.|
|Mine Layer|Advanced|Land Mine creates one additional mine.|
|Conductor|Advanced|Chain Lightning jump multipliers become 100% / 75% / 55% / 40% / 30%.|
|Meteorologist|Advanced|Meteor Shower deals +25% damage to battlefield objects and fortifications inside its impact area.|
|Controlled Blast|Advanced|When Fireball, Inferno, or Meteor Shower is centered on a hex occupied by a friendly stack, that stack is excluded from the spell's damage. Frost Ring already has a safe center. Armageddon remains explicitly indiscriminate.|
|Cataclysm|Expert|Armageddon receives +20% to its Spell Power-derived damage component and destroys ordinary magical obstacles as well as physical ones.|
|Annihilator|Expert|Disintegrate ignores 20% of the target's Magical Damage Reduction; casualties it destroys remain unusable remains as normal.|



###### Sorcery Magic

Sorcery manipulates magic and reality through technique: overcharge, dispelling, relocation, arcane enhancement, illusion, gravity, time, and magical sealing.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may acquire Level 3 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 115% Basic multiplier.|
|Advanced|The hero may acquire Level 4 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 130% Advanced multiplier.|
|Expert|The hero may acquire Level 5 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 145% Expert multiplier.|



Without this School Skill, ordinary sources can teach only Level 1-2 spells of the school. Basic / Advanced / Expert unlock ordinary acquisition of Levels 3 / 4 / 5 respectively. Explicit starting or temporary inscriptions may grant higher-level spells; a legitimately inscribed combat spell remains castable regardless of School rank, subject to all other casting rules.

###### Sorcery Magic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Overcharger|Basic|Magic Arrow gains +1 maximum Overcharge and each Overcharge point increases base damage by 17.5% instead of 15%.|
|Selective Dispel|Basic|Dispel automatically removes only hostile temporary magical effects from a friendly target and only beneficial temporary magical effects from an enemy target.|
|Temporalist|Basic|Slow lasts 1 additional round.|
|Matter Shaper|Basic|Transfigure Matter creates 25% more Diamond Golem HP.|
|Teleporter|Advanced|A stack moved by Teleport gains +2 Speed during its next Creature Activation.|
|Arcane Ballistics|Advanced|When an enemy stack has 3 Arcane Breach marks from Focus Magic, friendly ranged creature attacks against it also ignore 25% of its Physical Damage Reduction.|
|Illusionist|Advanced|Phantom Army receives 25% more phantom integrity.|
|Temporal Field|Advanced|Grants Mass Slow.|
|Chronomancer|Expert|Time Stop's maximum radius increases by 1.|
|Spellbinder|Expert|Spell Lock's ordinary duration lasts 1 additional round, to a maximum of 4 rounds. Echoed Duration is then applied separately to an eligible Metamagic cast, so both perks together may produce a 5-round Spell Lock.|



###### Chaos Magic

Chaos destabilizes probability, position, identity, control, and the relationship between beneficial and harmful magical states.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may acquire Level 3 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 115% Basic multiplier.|
|Advanced|The hero may acquire Level 4 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 130% Advanced multiplier.|
|Expert|The hero may acquire Level 5 spells from this school through ordinary learning. When a School-rank spell benefit is authored, applicable Spell Power coefficients use the provisional 145% Expert multiplier.|



Without this School Skill, ordinary sources can teach only Level 1-2 spells of the school. Basic / Advanced / Expert unlock ordinary acquisition of Levels 3 / 4 / 5 respectively. Explicit starting or temporary inscriptions may grant higher-level spells; a legitimately inscribed combat spell remains castable regardless of School rank, subject to all other casting rules.

###### Chaos Magic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Misfortune Weaver|Basic|Misfortune reduces favorable random-effect probability by an additional 10 percentage points of the normal probability multiplier, respecting its normal minimum.|
|Blinkmaster|Basic|Blink generates two random legal destinations and automatically uses the destination farther from the target's origin hex. Ties use deterministic hex order.|
|Confounder|Basic|Confusion cannot produce the same resolved behavior twice consecutively on the same target when a different legal behavior is available. If its random result matches that target's previous Confusion result, reroll until a different legal result is obtained. If only one legal behavior exists, allow that result even if it repeats; never reroll indefinitely. Trapped Wander resolves as Defend.|
|Frenzied Curse|Basic|A Berserked creature gains +2 Speed during its forced activation.|
|Mindbreaker|Advanced|Forgetfulness also suppresses the target's passive offensive creature abilities for its duration.|
|Shapeshifter|Advanced|Polymorph independently draws two random same-tier forms with replacement and automatically applies the form with the lower Army Value of the whole transformed stack, using its exact HP-converted creature count. Ties use canonical creature order.|
|Fate Dealer|Advanced|Hand of Fate independently draws two random legal spill targets with replacement. If exactly one of them is hostile to the caster, that hostile stack automatically receives the spill damage; otherwise a fair coin selects one of the two draws, including duplicates.|
|Reality Breaker|Advanced|Reality Warp may target any two legal stacks rather than requiring one friendly and one enemy stack.|
|Pandemonium Master|Expert|Pandemonium deals +25% damage per counted debuff.|
|Paradox Shield|Expert|Shield of Chaos gains +10 percentage points to both damage resistances, without changing the -10 Morale and -10 Luck penalties.|



###### Spellcraft

Spellcraft is general magical execution independent of school. It is the successor to the old generic Sorcery secondary skill; Sorcery itself remains one of the six magic schools.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Spell Power-derived numerical components of spells<br>operate at 110% Spellcraft Efficiency. At subsequent level-<br>ups, Spellcraft has a 10% chance to grant +1 Spell Power.|
|Advanced|Spell Power-derived components operate at 120%. Spell<br>Power-growth chance becomes 20%.|
|Expert|Spell Power-derived components operate at 130%. Spell<br>Power-growth chance becomes 30%.|



###### Spellcraft perk pool

|Perk|Requires|Effect|
|---|---|---|
|Cross-School Formula|Basic|After an accepted combat spell cast by the hero, the next accepted cast in the same or next round receives +10% to its Spell Power-derived numerical component if the two spells' saved school-membership sets are nonempty and disjoint. Each accepted cast becomes the new previous spell.|
|Spell Penetration|Basic|Hostile spells ignore 20% of the target's Magical Damage Reduction.|
|Concentration|Basic|A hero combat spell with exactly one unique prospective stack recipient receives +15% to its Spell Power-derived numerical component. Count recipients before resistance rolls or random additional-target draws, not only stacks that ultimately receive an effect.|
|Arcane Focus|Basic|The first spell cast in each combat receives +20% to its Spell Power-derived numerical component.|
|Extend Spell|Advanced|Once per round, the first accepted hero combat spell with a positive round-based temporary lifetime lasts 1 additional round. Permanent, combat-long and action-bound effects do not qualify.|
|Precise Casting|Advanced|When a conventional area spell with a selected central hex is centered on a hex occupied by a friendly stack, that stack is excluded from the spell's effect. Spells explicitly defined as indiscriminate, such as Armageddon, cannot benefit.|
|Empower Spell|Advanced|Spells whose final Wisdom-adjusted Mana cost is 12 or higher receive +25% to their Spell Power-derived numerical component.|
|Counterpressure|Advanced|After an enemy hero casts a spell that affects your army, your next spell before the end of the next round receives +20% to its Spell Power-derived component.|
|Grand Formula|Expert|The first Level 4 or Level 5 spell cast in each combat uses 150% of its normal Spell Power-derived numerical component before other multipliers.|
|Overwhelming Formula|Expert|The first hostile spell each combat that deals magical damage to one or more stacks with Magical Damage Reduction ignores 50% of each affected target's current Magical Damage Reduction.|

Cross-School Formula uses the battle's captured school catalog, not a chosen School label or current global data. Accepted casts update its history even when countered or resisted; rejected and non-hero casts do not. A same-school cast refreshes the previous-spell reference without receiving the bonus.

Concentration and Cross-School Formula modify only genuine Spell Power-derived numerical terms, composed once with existing multipliers before the final component floor. Fixed terms, costs and ordinary caps remain unchanged. Concentration's prospective count does not waive final value-dependent target legality.

Extend Spell uses one accepted-cast receipt per hero per round. Rejected and non-hero casts do not consume it; an accepted qualifying cast consumes it even when its effects are negated. Round-timed obstacles and other saved round lifetimes qualify, but permanent traps, action-bound Time Stop or control, and combat-long summons do not.



###### Wisdom

###### **Magic-exclusive Skill.**

Wisdom is the Magic hero's general spell-efficiency discipline. It reduces the Mana cost of spells and develops Knowledge; access to Levels 3-5 belongs to the corresponding Magic School Skill, not to Wisdom.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Listed spell Mana costs are reduced by 10%. At<br>subsequent level-ups, Wisdom has a 10% chance to grant<br>+1 Knowledge.|
|Advanced|Listed spell Mana costs are reduced by 20%. Knowledge-<br>growth chance becomes 20%.|
|Expert|Listed spell Mana costs are reduced by 30%. Knowledge-<br>growth chance becomes 30%.|



###### Wisdom perk pool

|Perk|Requires|Effect|
|---|---|---|
|Intelligence|Basic|Maximum Normal Spell Points equal floor(1.30 × effective Knowledge). Intelligence is the sole general multiplicative capacity effect.|
|Mysticism|Basic|At the start of each day, recover Mana equal to the greater of 5 or 10% of maximum Mana.|
|Arcane Memory|Basic|After a genuine scroll-sourced cast successfully completes, permanently learn the spell only if captured ordinary acquisition rules and its School requirement permit it. Current paid Adventure Guild unlocks are not bypassed; captured older rules retain their ordinary behavior. Book or Tome sources take priority over scrolls, and reusable scrolls over charged artifacts. Rejected, canceled or pending casts and unused carried scrolls do not teach; reusable scrolls remain reusable.|
|Prepared Caster|Basic|After Wisdom calculates the spell's percentage discount, the first spell cast in each combat costs 2 additional Mana less, to a minimum of 1.|
|Sage|Advanced|On the hero's first visit to each built Mage Guild, reveal one previously undisplayed eligible spell from the catalog of its saved per-level school labels and built levels, respecting map bans and ordinary learning rules. Choose the highest eligible level, with canonical spellbook order breaking ties. Reveal it globally in a separate additional row without changing ordinary fixed slots or research. The first built-Guild visit is consumed even without the perk or a spellbook; visiting a town with no Guild does not consume it.|
|Meditation|Advanced|If the hero ends the day with at least 25% of maximum Movement unspent, recover an additional 15% of maximum Mana.|
|Mana Conservation|Advanced|After combat, recover 20% of the Mana spent during that combat, up to 20 Mana.|
|Deep Knowledge|Advanced|Wisdom's chance to grant +1 Knowledge at level-up increases by 10 percentage points.|
|Archmage|Expert|After Wisdom calculates its percentage discount, the first Level 4 or Level 5 spell cast in each combat costs 3 additional Mana less, to a minimum of 1.|
|Arcane Reservoir|Expert|Maximum Mana increases by a flat 25 in addition to Knowledge and other modifiers.|



### Hybrid Skill

###### Warcasting

Warcasting rewards heroes who alternate naturally between military command and magic. It contains no class check and no formula asking whether a hero is mathematically “hybrid”; the Skill becomes valuable organically when both the hero's Orders and spells are worth using.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|After using the Hero Action to cast a Spell, the next Order<br>issued before the end of the next round gains +10<br>percentage points of efficiency on its attribute-derived<br>components. After using the Hero Action to issue an Order,<br>the next Spell cast before the end of the next round gains<br>+10% to its Spell Power-derived numerical component.|
|Advanced|Both alternating bonuses become 20% / +20 percentage<br>points.|
|Expert|Both alternating bonuses become 30% / +30 percentage<br>points.|



###### Warcasting perk pool

|Perk|Requires|Effect|
|---|---|---|
|Martial Channeling|Basic|The Spell-to-Order Warcasting bonus gains an additional +10 percentage points of efficiency.|
|Arcane Channeling|Basic|The Order-to-Spell Warcasting bonus gains an additional +10% to the spell's Spell Power-derived component.|
|Spellward|Basic|Friendly stacks receive 10% less magical damage. This is the successor to the old Resistance secondary skill.|
|Battle Meditation|Basic|Once per round, after an Order-to-Spell Warcasting bonus is consumed, recover 3 Mana.|
|Enchanted Command|Advanced|A friendly stack affected by an Order empowered by Warcasting gains +1 Morale until its next activation. Protect grants this only to its Protector/Ward pair; Second Wind grants it only to its selected stack, not the entire army.|
|Combat Casting|Advanced|A hostile spell empowered by Warcasting ignores 15% of the target's Magical Damage Reduction.|
|Tactical Weaving|Advanced|An armed Warcasting bonus remains available for two rounds instead of expiring at the end of the next round.|
|Reactive Weave|Advanced|After an enemy hero casts a spell that affects your army, your next Order before the end of the next round receives half your normal Spell-to-Order Warcasting bonus.|
|Perfect Rhythm|Expert|If the hero's last three Hero Actions alternate Spell / Order / Spell or Order / Spell / Order, the third action receives double the normal Warcasting bonus.|
|Master Synthesis|Expert|The first Warcasting bonus consumed each combat is automatically treated as +50% / +50 percentage points instead of its normal value.|



### Strategic and Universal Skills

###### Logistics

Logistics governs strategic movement: roads, difficult terrain, sea travel, embarkation, forced marches, and map visibility. Scouting, Pathfinding, and Navigation are perks rather than separate Skills.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Adventure-map Movement increases by 10%.|
|Advanced|Adventure-map Movement increases by 20%.|
|Expert|Adventure-map Movement increases by 30%.|



###### Logistics perk pool

|Perk|Requires|Effect|
|---|---|---|
|Pathfinding|Basic|Difficult-terrain movement penalties are reduced by 50%.|
|Navigation|Basic|Sea Movement increases by 25%, and embark/disembark Movement costs are halved.|
|Scouting|Basic|Adventure-map sight radius increases by 5 hexes.|
|Forced March|Basic|Once per day after exhausting normal Movement, gain additional Movement equal to 10% of maximum Movement. The army begins its next combat that day with -1 Morale during round 1.|
|Roadmaster|Advanced|Movement cost while travelling on roads is reduced by an additional 25% relative to the normal road cost.|
|Mountaineer|Advanced|Rough and Subterranean terrain impose no additional movement penalty. Impassable Rock remains impassable; other terrain penalties are unchanged.|
|Rapid Embarkation|Advanced|Embarking or disembarking costs only 10% of maximum daily Movement.|
|Pursuit March|Advanced|After winning a combat, recover 10% of maximum daily Movement. Once per day.|
|Wayfarer|Expert|Passable terrain can never cost more than 125% of its base clear-terrain movement cost.|
|Master Logistician|Expert|Up to 15% of unused Movement at the end of a day carries over to the next day.|



###### Diplomacy

Diplomacy turns neutral-creature interaction into a deterministic negotiation system rather than a save-scummable random roll. Scripted or explicitly hostile encounters may be marked ineligible.

Map-authored guaranteed free joins remain exceptions to the normal Diplomacy
threshold and Gold-payment rules. After an accepted join, troops left in the
army-transfer window when it closes are permanently dismissed; they do not
remain as a neutral stack on the map. Present this consequence before closure.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|An eligible neutral stack with Army Value no greater than<br>25% of the hero's current army may offer to join for its<br>normal recruitment gold cost.|
|Advanced|The deterministic joining threshold becomes 50% of the<br>hero's Army Value.|
|Expert|The deterministic joining threshold becomes 75% of the<br>hero's Army Value.|



###### Diplomacy perk pool

|Perk|Requires|Effect|
|---|---|---|
|Negotiator|Basic|Increase the Diplomacy joining threshold by 15 percentage points.|
|Peacemaker|Basic|The first eligible neutral stack encountered each week that would otherwise initiate combat instead allows the hero to pass without fighting and remains on the map. The hero may still deliberately attack it through the ordinary encounter interaction.|
|Envoy|Basic|From up to 5 hexes away, reveal whether a visible neutral stack is willing to negotiate and the gold required if it joins.|
|Mercenary Captain|Basic|Creatures recruited through Diplomacy gain +1 Morale for their first three combats under this hero.|
|Common Cause|Advanced|Neutral creatures of the hero's faction count as half their normal Army Value when checking the Diplomacy threshold.|
|Tribute|Advanced|Once per week, the first eligible neutral stack that refuses to join automatically accepts Gold equal to its normal recruitment value and leaves the map if the kingdom can pay that amount. If the treasury is insufficient, normal combat rules apply.|
|Loyal Mercenaries|Advanced|Creatures recruited through Diplomacy do not contribute to mixed-faction Morale penalties while commanded by this hero.|
|Recruitment Pact|Advanced|After successfully recruiting a neutral stack, the next neutral stack contacted within 7 days is treated as 15% lower Army Value for the joining threshold.|
|Grand Diplomat|Expert|Increase the Diplomacy joining threshold by a further 25 percentage points, to a maximum of 100% of the hero's Army Value.|
|Legendary Reputation|Expert|Once per calendar month, the first eligible neutral stack that qualifies to join through Diplomacy joins without a gold payment.|

Peacemaker's selected stack permits that hero to pass through its guarded area
until the current calendar week ends. The stack remains on the map and other
heroes do not gain this protection. Deliberately attacking it ends the protection
without refunding that week's Peacemaker use. When Peacemaker and Tribute both
apply to an encounter, resolve Peacemaker's free passage first; do not charge
Gold or consume Tribute for that encounter. Tribute consumes its weekly use
only when its payment succeeds. An unaffordable refusal does not consume the
use and follows ordinary encounter rules.



###### Estates

Estates is the hero's economic specialization: rents, land management, mine exploitation, markets, and treasury finance. Its values are deliberately discrete where practical.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The kingdom receives +125 Gold per day while the hero is<br>active.|
|Advanced|Daily income from Estates increases to +250 Gold.|
|Expert|Daily income from Estates increases to +500 Gold.|



###### Estates perk pool

|Perk|Requires|Effect|
|---|---|---|
|Tax Collector|Basic|Gain +50 Gold per day for each owned town, capped at +500 Gold per day from this perk.|
|Land Surveyor|Basic|The first mine captured by this hero each week immediately produces three days of its normal output.|
|Prospector|Basic|The first owned mine visited by this hero each week immediately produces +2 of its common resource or +1 of its rare resource, according to the mine's normal output type.|
|Merchant Prince|Basic|While this hero is in a town, that town's Marketplace exchange rates behave as if the kingdom owned two additional Marketplaces.|
|Steward|Advanced|If the hero ends the day in an owned town, that town produces +250 Gold on the next day.|
|Investor|Advanced|At the start of each week, this hero's Estates daily income for that week increases by +50 Gold for each full 5,000 Gold currently in the treasury, capped at +250 Gold per day.|
|Resource Broker|Advanced|While this hero is in a town, Marketplace exchanges of Wood or Ore for rare resources in that town use a rate 20% more favorable than the ordinary Marketplace rate.|
|Estate Network|Advanced|At the start of each week, gain +1 Wood and +1 Ore for every three owned towns, rounded down, minimum 1 if at least one town is owned.|
|Financier|Expert|At the start of each week, gain 1% of the current Gold treasury as interest, capped at 1,000 Gold.|
|Magnate|Expert|At the start of each week, the most recently visited owned town from the previous week produces +500 Gold per day for the next 7 days. If no owned town was visited, the perk has no effect that week.|



###### Learning

Learning covers experience, teaching, study, spell exchange, and the acquisition of knowledge from the world. Scholar and Eagle Eye are absorbed as perks. New Horizons has no ordinary Mage Guild Spell Research mechanic.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Experience gained by the hero increases by 10%.|
|Advanced|Experience gained increases by 20%.|
|Expert|Experience gained increases by 30%.|



###### Learning perk pool

|Perk|Requires|Effect|
|---|---|---|
|Scholar|Basic|When two allied heroes meet, each automatically teaches the other the highest-level spell they know that the recipient does not know and could legally learn. Ties use canonical spellbook order. Once per hero pair per week.|
|Eagle Eye|Basic|After combat, automatically learn the highest-level Level 1-3 spell cast by the enemy hero during that combat that this hero does not know and could legally learn. If several qualify at the same highest level, learn the one cast earliest in the combat.|
|Historian|Basic|Adventure-map objects whose primary reward is Experience grant 50% more Experience.|
|Mentor|Basic|The first lower-level allied hero met each week automatically gains Experience equal to 250 times the mentor's level.|
|Academic Study|Advanced|The first time the hero visits each town, gain 250 Experience for each Mage Guild level already constructed in that town.|
|Quick Study|Advanced|At every fifth hero level, the initial level-up offer is automatically rerolled once before the choices are shown to the player.|
|Field Study|Advanced|Gain +25% additional Experience from defeating enemy heroes or wandering armies whose Army Value exceeded your army's at battle start.|
|Archivist|Advanced|When the hero acquires a spell scroll, permanently learn its spell immediately if legally eligible.|
|Sage|Expert|On the hero's first visit to each built Mage Guild, after ordinary Guild teaching and any Wisdom Sage reveal, personally learn the highest-level eligible unknown spell from the catalog of its saved per-level school labels and built levels, including undisplayed spells. Respect map bans and ordinary learning rules; ties use canonical spellbook order. The first built-Guild visit is consumed even without the perk or a spellbook; visiting a town with no Guild does not consume it.|
|Master Teacher|Expert|Mentor automatically triggers for the first two different lower-level allied heroes met each week and grants Experience equal to 500 times the mentor's level to each.|



###### Luck

Luck governs favorable and unfavorable chance in combat. The exact -10 to +10 trigger curve is defined by the Luck/Morale system; this Skill shifts the hero's army toward favorable outcomes.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Friendly army receives +1 Luck.|
|Advanced|Friendly army receives +2 Luck total.|
|Expert|Friendly army receives +3 Luck total.|



###### Luck perk pool

|Perk|Requires|Effect|
|---|---|---|
|Opportunist|Basic|After a positive Luck trigger on an attack, the stack's Creature Activation remains open for movement only, allowing up to 2 hexes of movement using its remaining movement. It gains no additional attack.|
|Second Chance|Basic|The first negative Luck trigger against the army each combat is ignored.|
|Lucky Aim|Basic|A ranged attack that triggers positive Luck ignores 25% of the target's Creature Defense.|
|Fortune's Favor|Basic|Positive Lucky Strike damage multipliers increase by +0.25x.|
|Serendipity|Advanced|If no friendly positive Luck trigger occurred during the previous round, the first friendly attack of the new round receives +2 Luck for that attack.|
|Lucky Day|Advanced|An accepted Order may trigger positive hero Luck to double its intrinsic potency. Duration, targets, activation counts and companion-perk bonuses are unchanged.|
|Erratic Mana|Advanced|An accepted paid hero spell may trigger positive hero Luck to refund 50% of actual spent Mana, rounded down, preserving the original Mana pools. A negated spell remains eligible.|
|Chain of Fortune|Advanced|Once per round after a friendly positive Luck trigger, the next different friendly stack to attack gains +1 Luck for that attack.|
|Druidic Luck|Expert|An accepted damaging hero spell may trigger positive hero Luck to double numerical HP and structural damage, including its captured damage-over-time and zones. Healing, counts and instant kills are unchanged.|
|Perfect Fortune|Expert|The first eligible attack made by the hero's army each combat triggers positive Luck automatically.|

Chain of Fortune cannot benefit another strike by the stack that supplied its
positive Luck trigger. The once-per-round restriction limits triggering; it
does not expire an unused benefit. That benefit waits for the next different
friendly stack to attack, even if that attack occurs in a later round.

These three generic perks use the acting hero's positive Luck and the existing Luck trigger curve and dice. They replace the former generic Lucky Recovery, Gambler and Twist of Fate effects; the separate Sylvan Luck Lucky Recovery remains unchanged. Their internal identifiers remain `luck.luckyRecovery`, `luck.gambler` and `luck.twistOfFate` to preserve saved perk identity, and their ranks remain Advanced, Advanced and Expert.

Lucky Day multiplies only the intrinsic numerical potency of the accepted Order. It does not extend duration, add targets, grant more activations, duplicate companion-perk bonuses or turn an Order-only continuation into a spell action. Druidic Luck multiplies numerical HP and structural damage authored by the accepted hero spell, including damage captured for its later damage-over-time or zone effects; it does not multiply healing, summon/restoration counts, status duration or instant-kill counts.

Erratic Mana refunds the configured fraction of Mana actually charged for an accepted paid hero spell, rounded down. Restore the original source pools in reverse charge order: normal Mana first, then permanent Spell Buffer, then temporary Spell Buffer. No refund may move Mana between origins or create an unrelated permanent reserve. A successfully admitted and paid spell remains eligible even when resistance or another effect negates its output; a rejected or uncharged action does not earn a refund.

An accepted action captures its Luck result once. Internal echoes and mirrors inherit the parent's result and cannot obtain a fresh Erratic Mana refund. An independently admitted follow-up checks its own hero Luck. Delayed damage retains the originating cast's result rather than rolling again when a damage-over-time tick or zone fires.


### Faction Skills

Each faction has one unique Skill available to both of its hero classes. Faction Skills follow the same ten-perk pool and maximum-three-perks rule as generic Skills. Their perks are primarily combat-facing here; future Governor interactions belong to the separate Future Design section.

###### Castle — Divine Mandate

###### **Faction-locked Skill.**

Castle unites sacred intervention and military command. Divine Mandate creates a precise exception to the normal Spell-or-Order choice without introducing a second action currency.

Divine Mandate follows the same opportunity principles as Metamagic. The paired Light Spell or Order becomes available automatically through its ordinary control and may be used until the end of the current round. Other combat actions do not cancel the opportunity. A use is consumed only when the paired follow-up is performed; if it expires unused, no use is consumed and a later qualifying Hero Action may create a new opportunity. The follow-up cannot recursively trigger Divine Mandate. It grants no general Hero Action, carries into no later round, and never locks Creature Activations or requires an Activate/Decline prompt.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Once per completed pair per combat, when the hero uses<br>the Hero Action to cast a Light Spell, gain one Order-only<br>follow-up usable until round end; or after using the Hero<br>Action to issue an Order, gain one Light-Spell-only<br>follow-up usable until round end.|
|Advanced|Divine Mandate may be used twice per combat.|
|Expert|Divine Mandate may be used three times per combat.|



###### Castle — Divine Mandate perk pool

|Perk|Requires|Effect|
|---|---|---|
|Sacred Command|Basic|An Order issued through Divine Mandate gains +10 percentage points of efficiency on its attribute-derived components.|
|Consecrated Casting|Basic|A Light Spell cast through Divine Mandate gains +10% to its Spell Power-derived numerical component.|
|Shared Purpose|Basic|If the paired Light Spell and Order both affect the same friendly stack, that stack gains +1 Morale until its next activation.|
|Chaplain's Reserve|Basic|After the first Divine Mandate sequence each combat resolves, recover 3 Mana.|
|Divine Discipline|Advanced|An Order issued through Divine Mandate that would normally end at round end remains separately on its original affected friendly recipients through their first genuine completed Creature Activation after the issuing round. Wait and same-activation continuations do not consume this carry.|
|Purifying Mandate|Advanced|When a Light Spell cast through Divine Mandate removes a negative magical effect, it also removes one physical affliction from that target if present.|
|Knightly Sequence|Advanced|If the Order is performed first, the paired Light Spell costs 2 less Mana; if the Light Spell is performed first, the paired Order gains +5 additional percentage points of efficiency.|
|Royal Standard|Advanced|Friendly stacks affected by an Order issued through Divine Mandate treat negative Morale as 0 for that Order's duration.|
|Mandate of Heaven|Expert|The first Divine Mandate sequence each combat does not count against the Skill's normal per-combat usage limit.|
|Crown and Altar|Expert|The second action of a Divine Mandate pair automatically gains +20% to its rating-derived numerical components on each friendly stack also affected by the first action. This applies in either Spell-then-Order or Order-then-Spell order. It requires no extra button or target-preselection step and does not retroactively change the first action.|

Divine Discipline extends effective unspent, unbroken benefits; it does not revive spent Charge, broken Protect or completed Second Wind. Reissuing the same Order replaces its prior recipient snapshot, including carried benefits. Only the Divine Mandate Order follow-up receives this lifetime extension.

Crown and Altar uses the actual first action's friendly recipients and the second action's recipient intersection. It strengthens only rating-derived numerical terms before existing floors and caps, not fixed terms, costs, target shapes, durations or action budgets. The first action remains unchanged.



###### Rampart — Sylvan Luck

###### **Faction-locked Skill.**

Rampart turns fortune into a faction identity. Sylvan Luck increases both the frequency and the payoff of favorable Luck while keeping the universal Luck system intact.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|+1 Luck. Lucky Strike damage multiplier becomes 2.25x.|
|Advanced|+2 Luck total. Lucky Strike multiplier becomes 2.60x.|
|Expert|+3 Luck total. Lucky Strike multiplier becomes 3.00x.|



###### Rampart — Sylvan Luck perk pool

|Perk|Requires|Effect|
|---|---|---|
|Elven Precision|Basic|Lucky ranged attacks ignore 25% Creature Defense.|
|Forest's Favor|Basic|The first positive Luck trigger for each friendly stack in a combat grants +2 Speed for the remainder of that activation.|
|Serendipity|Basic|Until a friendly stack has triggered positive Luck once in the current combat, it counts as having +1 additional Luck for trigger chance only.|
|Lucky Recovery|Basic|A melee Lucky Strike restores surviving creatures for 10% of actual damage dealt.|
|Shared Fortune|Advanced|After a friendly stack triggers positive Luck, adjacent friendly stacks gain +1 Luck until their next activation.|
|Nature's Providence|Advanced|The first negative Luck trigger against the army each round is ignored.|
|Fortunate Aim|Advanced|Friendly shooters gain +1 Luck when attacking the target designated by Focus Fire!.|
|Wild Chance|Advanced|Temporary creatures summoned by Nature spells inherit the hero's Sylvan Luck bonus and Lucky Strike multiplier.|
|Perfect Moment|Expert|The first eligible attack each combat made by a friendly stack with +5 or greater current Luck triggers positive Luck automatically.|
|Cascading Fortune|Expert|When a Lucky Strike destroys an enemy stack, the next friendly stack to act gains +3 Luck for its activation.|



###### Tower — Metamagic

###### **Faction-locked Skill.**

Tower specializes in manipulating the act of spellcasting itself. Metamagic does not provide a separate ability to activate. When the hero casts a Spell with the Hero Action while a Metamagic use remains, the hero automatically gains a Spell Action that may be spent until the end of the current round.

A Metamagic use is consumed only when the additional Spell Action is spent. If the Spell Action expires unused, no use is consumed, and a later Hero Action spell may grant another opportunity. Spell Actions never carry into a later round.

A Spell Action may be used to cast one ordinary combat Spell without consuming the Hero Action. The Spell pays its normal Mana cost and follows all ordinary targeting and casting rules.

A Spell Action can cast a Spell, but cannot issue an Order or recursively trigger Metamagic.

The Spell cast with the Hero Action and any Spell subsequently cast through the Spell Action it generated constitute a Metamagic sequence. A sequence counts as used only after at least one additional Spell is cast.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|The hero may complete 1 used Metamagic sequence per<br>combat.|
|Advanced|The hero may complete 2 used Metamagic<br>sequences per combat.|
|Expert|The hero may complete 3 used Metamagic sequences per<br>combat.|



###### Tower — Metamagic perk pool

|Perk|Requires|Effect|
|---|---|---|
|Spell Sequencing|Basic|A Spell cast with a Metamagic Spell Action gains +15% to its Spell Power-derived numerical component if it belongs to a different school from the Spell that triggered Metamagic.|
|Arcane Economy|Basic|Spells cast with Metamagic Spell Actions cost 2 less Mana, to a minimum of 1.|
|Focused Pairing|Basic|If a hostile Spell cast with a Metamagic Spell Action targets the same stack as the triggering Spell, it ignores 20% of that stack's Magical Damage Reduction.|
|Arcane Acquisition|Basic|If Focus Magic is cast with a Metamagic Spell Action, each living enemy stack struck by the enchanted shooter while it currently has no Arcane Breach marks receives 2 marks instead of 1. A target that already has one or more marks receives the normal 1 mark. Check each target when it is damaged and retain the 3-mark cap.|
|Echoed Duration|Advanced|A temporary effect created by a Spell cast with a Metamagic Spell Action lasts 1 additional round.|
|Split Focus|Advanced|If a Spell cast with a Metamagic Spell Action targets a different stack from the triggering Spell, it gains +10% to its Spell Power-derived numerical component.|
|Formula Reserve|Advanced|After each Metamagic sequence in which at least one additional Spell was cast is completed, restore 3 Normal Spell Points once.|
|Spell Buffer|Advanced|Once per combat, when a Spell Action granted by Metamagic expires unused at the end of the round, gain 6 Buffer Spell Points. The expired opportunity does not consume a Metamagic use.|
|Grand Metamagic|Expert|When the first additional Spell of the third used Metamagic sequence is cast, automatically grant one further Spell Action. The sequence therefore permits two additional Spells in total. Both opportunities expire at the end of the round, neither can trigger Metamagic, and the further action does not consume another Metamagic use.|
|Perfect Sequence|Expert|Each Spell cast with a Metamagic Spell Action gains +20% to its Spell Power-derived numerical component if it is a different Spell from every earlier Spell in that Metamagic sequence.|



###### Dungeon — Shroud of Malassa

###### **Faction-locked Skill.**

Dungeon warfare is positional predation. The Shroud lets friendly stacks pass through occupied hexes while moving and rewards attacks delivered from distinct sides and exposed positions.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Friendly stacks gain Ghost Walk: they may move through<br>occupied creature hexes but may not end movement on an<br>occupied hex. Flanking melee attacks deal +25% damage.|
|Advanced|Ghost Walk remains active. Flanking melee bonus<br>becomes +40%.|
|Expert|Ghost Walk remains active. Flanking melee bonus<br>becomes +60%, and flanking melee attacks deny normal<br>retaliation.|



###### Dungeon — Shroud of Malassa perk pool

|Perk|Requires|Effect|
|---|---|---|
|Veiled Movement|Basic|Ghost Walk movement does not trigger explicitly movement-based reaction attacks.|
|Backstab|Basic|A melee attack from the target's rear-facing side gains an additional +15% damage.|
|Ambusher|Basic|The first flanking attack made by each friendly stack in a combat deals +20% damage.|
|Shadow Assault|Basic|The first flanking attack against each enemy stack ignores 25% Creature Defense.|
|No Escape|Advanced|After suffering a flanking melee attack, the target loses 2 Speed until its next activation.|
|Evasive Shroud|Advanced|After making a flanking attack, the attacker receives 15% physical damage reduction until its next activation.|
|Deep Flank|Advanced|Friendly ranged attacks against a target currently attacked from at least two distinct melee sides gain half the Shroud's current flanking damage bonus.|
|Night Prowler|Advanced|A stack that uses Ghost Walk to pass through an enemy stack's occupied hexes gains +10% damage on its next attack that activation.|
|Encircled Doom|Expert|Each additional distinct melee attack side beyond the first adds +10% damage to the Shroud flanking attack.|
|Vanish|Expert|After destroying an enemy with a flanking melee attack, the attacker may move up to half its Speed. It gains no additional attack.|



###### Inferno — Demonic Gating

###### **Faction-locked Skill.**

Inferno heroes may bring actual reserve troops onto the battlefield through gates. Gating uses a Demonic Reserve of owned Inferno creatures outside the seven active combat slots; creatures are not created for free. The hero screen permits depositing and withdrawing the whole selected reserve stack subject to ordinary army legality. Casualties suffered by reserve troops are permanent unless a rule such as Endless Legion restores them.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|A friendly Inferno stack may spend its Creature Activation<br>to open a Gate on a legal empty hex within 3 hexes. The<br>server reserves the arriving stack's complete footprint,<br>including both hexes for a double-wide creature; reserved<br>hexes are impassable until arrival or cancellation. A flame-<br>only marker shows the reserved footprint without a yellow<br>selection outline. At the start of the next round, one Core<br>stack from the Demonic Reserve arrives there, and the<br>successful arrival plays the Devil movement flame sound.|
|Advanced|Gating may bring Core or Elite reserve stacks.|
|Expert|Gating may bring Core, Elite, or Champion reserve stacks.|



Surviving gated creatures return to the Demonic Reserve after combat unless another rule explicitly transfers them into the active army. A failed or interrupted Gate request never removes its selected reserve stack; validation and reservation are authoritative and atomic.

###### Inferno — Demonic Gating perk pool

|Perk|Requires|Effect|
|---|---|---|
|Swift Gate|Basic|A gated stack arrives at the end of the current round instead of the start of the next round.|
|Wide Gate|Basic|Gate placement range increases from 3 to 5 hexes from the gating stack.|
|Hellfire Arrival|Basic|When a gated stack arrives, adjacent enemy stacks suffer Fire damage equal to 15% of the gated stack's current aggregate HP, divided evenly among them.|
|Reinforced Gate|Basic|A gated stack gains temporary HP equal to floor(20% of its current aggregate HP) until combat ends. This pool is consumed before creature HP, creates no creatures, cannot be healed or resurrected, and is serialized in battle saves.|
|Mobile Gate|Advanced|As one combined Creature Activation, a stack may optionally move along a legal path up to floor(half its Speed) and then Gate. The complete request is validated before commitment; choosing no movement is legal, and interrupted or rejected movement does not consume or remove the selected reserve stack.|
|Infernal Beacon|Advanced|If the Gate is placed adjacent to another friendly Inferno stack, the gated stack gains +2 Initiative during its first actionable round. A delayed arrival never consumes the bonus before the gated stack can act.|
|Chain Gate|Advanced|After a gated stack destroys an enemy stack, the next Gate opened during that combat resolves one timing step sooner.|
|Reserve Discipline|Advanced|Gated stacks treat negative Morale as 0 during the round in which they arrive.|
|Master Gate|Expert|The first Gate opened each combat does not consume the gating stack's Creature Activation. After opening the Gate, that stack proceeds with its normal Creature Activation.|
|Endless Legion|Expert|After a victorious combat, 50% of casualties suffered specifically by gated Core and Elite reserve stacks are restored to the Demonic Reserve, rounded down.|



###### Necropolis — Necromancy

###### **Faction-locked Skill.**

Necromancy converts eligible enemy casualties into permanent Undead after victory. It creates strategic army growth without consuming the hero's Hero Action in combat.
###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|After victory, raise Skeletons equal to 10% of eligible living<br>enemy creature casualties, rounded down.|
|Advanced|Raise Skeletons equal to 20% of eligible casualties.|
|Expert|Raise Skeletons equal to 30% of eligible casualties.|



###### Necropolis — Necromancy perk pool

|Perk|Requires|Effect|
|---|---|---|
|Bone Collector|Basic|Necromancy raises an additional +5 percentage points of eligible casualties as Skeletons.|
|Corpse Preservation|Basic|Casualties caused by ordinary magical damage become eligible for Necromancy. Without this perk, ordinary magical casualties are excluded. Effects that explicitly destroy or invalidate remains, such as Disintegrate, remain excluded even with the perk.|
|Dark Conversion|Basic|When resolving Necromancy, every complete group of 3 Skeletons generated from eligible Core-tier casualties is automatically raised as 1 Zombie instead.|
|Black Harvest|Basic|After Necromancy raises at least 10 creatures, recover 1 Mana per 10 creatures raised, up to 10 Mana.|
|Soul Harvester|Advanced|When resolving Necromancy, every complete group of 6 Skeletons generated from eligible Elite-tier casualties is automatically raised as 1 Wight. Wights remain Core-tier; the conversion's input is Elite-tier casualties, not its output.|
|Death Lord|Advanced|Construct and Elemental casualties become eligible at 25% of the normal Necromancy conversion rate.|
|Grave Knowledge|Advanced|When defeating Undead enemies, 20% of their eligible casualties may be reclaimed as Skeletons even though they are not living.|
|Ossuary|Advanced|If the hero cannot accept the raised creatures because of unavailable army slots or Leadership limits, they are sent to the nearest owned Necropolis town instead of being lost.|
|Master of Bones|Expert|Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player.|
|Lord of the Dead|Expert|After defeating an army containing at least one Champion-tier living creature, if the base Necromancy result contains at least 12 Skeletons, 12 are automatically replaced with 1 Bone Dragon, once per combat. This conversion resolves before other Necromancy conversion perks. Consume the 12 Skeleton equivalents from Champion/unclassified contributions first, then Elite contributions, then Core contributions; only remaining contributions may fund the later conversions.|

When partial Resurrection restores a stack with mixed casualty causes, it restores
the most recent usable casualties first. Preserve that casualty order and damage
nature when accounting for the remaining corpses; destroyed remains are never
restored. Creatures that die again acquire the cause of their new death.


###### Stronghold — Bloodrage

###### **Faction-locked Skill.**

Stronghold gains momentum from destruction. Bloodrage is a battle-long damage state that rises whenever a non-summoned stack on either side is destroyed.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|Each non-summoned stack destroyed grants surviving<br>friendly stacks +5% damage, cumulative to +20%.|
|Advanced|Each destruction grants +8% damage, cumulative to<br>+40%.|
|Expert|Each destruction grants +12% damage, cumulative to<br>+60%.|



###### Stronghold — Bloodrage perk pool

|Perk|Requires|Effect|
|---|---|---|
|First Blood|Basic|The first qualifying stack destroyed in each combat grants two Bloodrage increments instead of one.|
|Blood Scent|Basic|When attacking an enemy below 50% maximum HP, treat Bloodrage as one increment higher for that attack.|
|War Drums|Basic|Begin each combat with one Bloodrage increment already active.|
|Rage Through Pain|Basic|The first time a friendly stack falls below 50% maximum HP, that stack gains one personal Bloodrage increment for the rest of combat.|
|Slayer|Advanced|Destroying an Elite or Champion stack grants two Bloodrage increments.|
|Unrelenting|Advanced|At half of the current Bloodrage cap or higher, friendly stacks gain +1 Speed.|
|Berserker|Advanced|At half of the current Bloodrage cap or higher, friendly stacks gain one additional retaliation.|
|Fury Unbound|Advanced|While at least one Bloodrage increment is active, friendly stacks treat negative Morale as 0.|
|Avatar of Rage|Expert|At maximum Bloodrage, friendly creature attacks ignore 25% Creature Defense.|
|Endless Bloodshed|Expert|Increase Bloodrage's maximum damage cap by 20 percentage points.|



###### Fortress — Bulwark of the Mire

###### **Faction-locked Skill.**

Fortress transforms the universal Defend action into a faction weapon. Bulwark projects the hero's Defense rating into static protection, pre-emptive retaliation, and damage reflection.

Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|While a friendly stack Defends, it gains additional physical<br>damage reduction equal to 5% + 0.10% × Hero Defense<br>until its next activation. The first melee attacker is struck<br>pre-emptively for 50% normal damage.|
|Advanced|Bulwark reduction becomes 7.5% + 0.15% × Hero<br>Defense; pre-emptive damage becomes 75%; the stack<br>reflects 25% of actual melee physical damage received<br>after reductions.|
|Expert|Bulwark reduction becomes 10% + 0.20% × Hero Defense;<br>pre-emptive damage becomes 100%; melee reflection<br>becomes 50%.|
###### Fortress — Bulwark of the Mire perk pool

|Perk|Requires|Effect|
|---|---|---|
|Mireborn|Basic|On swamp or rough terrain, Bulwark grants an additional 5 percentage points of physical damage reduction.|
|Thick Hide|Basic|Bulwark's damage reflection also applies to ranged physical creature damage at half its normal reflection percentage.|
|Bog Ambush|Basic|Bulwark's pre-emptive attack gains +25 percentage points of normal damage, capped at 100%.|
|Toxic Spines|Basic|Once per Defending Bulwark stack per round, the first melee attacker to suffer Bulwark reflection from that stack receives a physical Poison affliction. Base damage = max(1, floor(actual reflected HP damage / 4)). The affliction ticks on the target's next 3 real activations for base damage, floor(1.5 × base damage), and 2 × base damage. An equal- or stronger-damage reapplication replaces the affliction and restarts its duration; a weaker reapplication is ignored. Cure removes it; Dispel does not.|
|Deep Bulwark|Advanced|A stack currently benefiting from Bulwark cannot be forcibly displaced by non-magical effects.|
|Swamp Renewal|Advanced|At the beginning of the next activation after Defending, a surviving Bulwark stack restores 10% of physical creature damage suffered while Defending; casualties are not restored.|
|Mire Grip|Advanced|A melee attacker that damages a Bulwark stack loses 2 Speed until its next activation.|
|Shared Cover|Advanced|Adjacent friendly stacks that are also Defending receive half of this hero's Bulwark damage-reduction bonus.|
|Immovable|Expert|While Defending, the first physical creature attack against each Bulwark stack each round deals an additional 25% less final damage.|
|Vengeful Mire|Expert|Increase Bulwark's melee damage-reflection percentage by +25 percentage points, to a maximum of 75%.|



###### Conflux — Elemental Rebirth

###### **Faction-locked Skill.**

Conflux answers destruction with temporary elemental life. When a normal allied stack is destroyed, Elemental Rebirth summons an Elite Elemental using the lost stack's battle-start maximum aggregate HP as the scale. Capture that HP basis at the start of combat; intervening casualties or temporary HP changes do not change it.

###### Skill progression

|**Rank**|**Effect**|
|---|---|
|Basic|When a non-summoned allied stack is destroyed, summon<br>a random Elite Elemental stack at that position with<br>aggregate HP equal to 25% of the destroyed stack's<br>maximum aggregate HP.|
|Advanced|Reborn Elemental HP becomes 40% of the destroyed<br>stack's maximum aggregate HP.|
|Expert|Reborn Elemental HP becomes 50% of the destroyed<br>stack's maximum aggregate HP.|



Elementals created by Elemental Rebirth are temporary summons and cannot themselves trigger the base Elemental Rebirth effect.

###### Conflux — Elemental Rebirth perk pool

|Perk|Requires|Effect|
|---|---|---|
|Elemental Attunement|Basic|A reborn Elemental that matches the current battlefield terrain receives +20% aggregate HP.|
|Swift Rebirth|Basic|A newly reborn Elemental is inserted into the current Initiative cycle immediately after the current Creature Activation, but cannot receive more than one activation in the round.|
|Elemental Memory|Basic|A reborn Elemental inherits the destroyed stack's current positive Morale and Luck modifiers until the end of the round.|
|Primal Burst|Basic|When a reborn Elemental appears, adjacent enemies suffer elemental damage equal to 10% of the reborn stack's aggregate HP, divided evenly among them.|
|Greater Essence|Advanced|Elemental Rebirth creates an additional +15 percentage points of the destroyed stack's maximum HP.|
|Elemental Ward|Advanced|Reborn Elementals receive 20% magical damage reduction.|
|Adaptive Element|Advanced|Instead of a fully random Elemental, Rebirth selects from Elemental types appropriate to the current battlefield terrain.|
|Rebirth Chain|Advanced|Once per combat, when a reborn Elemental is destroyed, summon a second random Elite Elemental with 25% of that reborn stack's original HP. This second Elemental cannot trigger Rebirth.|
|Perfect Convergence|Expert|Every Rebirth uses the primary Elemental type associated with the current battlefield terrain by the Elemental Convergence terrain mapping instead of a random Elemental.|
|Phoenix Spark|Expert|Once per combat, the first eligible original friendly Champion stack destroyed triggers a temporary Phoenix instead of an Elite Elemental, with aggregate HP equal to 25% of its battle-start maximum aggregate HP. This replaces the ordinary Rebirth and Greater Essence percentage. Consume the use before placement, even if placement fails. Applicable Ward, Primal Burst, Elemental Memory and Swift Rebirth traits remain; Elite terrain matching and recursive Rebirth or Rebirth Chain do not apply. Native Phoenix creature rebirth remains separate.|



# Units

- Creature tiers use Core / Elite / Champion rather than the old seven-tier labels. The categories describe roster role and progression rather than simply renaming contiguous old levels; individual lines may move between bands when faction identity or balance benefits.

- Castle test mapping: Pikemen, Archers and Swordsmen are Core; Griffins, Monks and Cavaliers are Elite; Angels are Champion. The Griffin and Swordsman lines therefore swap bands relative to a simple 1-3 / 4-6 / 7 conversion. Conflux has two Core lines: Pixie upgraded to Sprite, and Wisp upgraded to its violet-blue form (upgrade name pending). Air, Water, Fire and Earth are the four Elite Elemental lines; Phoenix is Champion. Psychic/Magic Elementals leave the recruitable Conflux roster, and their dwelling line recruits Wisps instead. Existing Psychic/Magic Elemental identities remain valid for old maps and saves. Full faction mappings and experimental numbers are in Experimental Values.

###### Classical Conflux Elemental roles

Air / Storm, Water / Ice, Fire / Energy and Earth / Magma are four equal-economy
Elite lines. Air, Water, Ice, Fire, Earth and Magma are ground melee creatures.
Storm is a ground ranged creature with 16 shots and no obstacle/wall shooting
penalty; this does not grant No Melee Penalty or remove ordinary distance
penalties. Energy is a flying melee creature. Speed and Initiative are distinct
values, not interchangeable movement and activation ratings.

This rebalance grants no additional attacks or area attacks. Magma gains no
extra retaliation, Earth gains no Physical Damage Reduction and Water gains no
ranged-damage reduction. Preserve applicable Elemental classifications and
existing immunities unless separately revised. The legacy spell-specific
immunity/vulnerability interpretation remains pending review: do not invent a
mapping to the six New Horizons Magic Schools or silently treat inherited
legacy entries as a settled new-school immunity design. Pixie/Sprite, both Wisp
forms and Phoenix retain their unrelated rules.

###### Frozen — Ice Elemental physical affliction

Ice Elementals are melee-only, with zero shots. Each successful melee attack or
retaliation rolls a 20% Frozen chance once after damage. Frozen is a distinct
physical affliction, not Blind, Petrification or a spell effect. It prevents
retaliation and forfeits the recipient's next normal Creature Activation; the
recipient thaws immediately after that forfeiture. Cleansing before that normal
slot permits normal action, but cannot restore an activation already forfeited.

A subsequent ordinary physical creature attack against a Frozen recipient gains
25% final damage, then Shatters Frozen. The recipient cannot retaliate against
that attack, including after Shatter has removed Frozen. Direct magical damage
breaks Frozen without the Shatter damage bonus. Frozen cannot stack or refresh.
At most one successful application is allowed per recipient per round, and
cleansing, Shatter or thaw does not reset that round's application restriction.
Cure can clear Frozen; ordinary Dispel cannot. The default chance and Shatter
damage bonus are configurable values, initially 20% and 25% respectively.

Required interaction support includes save-state representation, AI use and
damage/action previews, a distinct Frozen status icon and tooltip, visible ice
encasement, Shatter feedback, an activation-forfeit message and an incapacitated
initiative marker. Status and encasement must clear on cleansing, Shatter or
thaw, while distinguishing an outstanding normal-activation forfeiture.

The treatment of extra activations, application stamps and multi-strike timing
is recorded separately as provisional interpretation in
[the provisional rule review ledger](../NH_PROVISIONAL_RULE_REVIEW.md#frozen-normal-activation-timing).
Those interpretations do not replace the required next-normal-activation rule
above. Specification and authored data do not themselves establish runtime
acceptance.

# Experimental Values

###### Purpose and status

This section is the numerical sandbox for New Horizons. Values here are the default test configuration for systems that were previously missing numbers, inherited obsolete Heroes III numbers, or are known to require rebalance. They are deliberately easier to change than the surrounding mechanical architecture. A value may move after testing without reopening the underlying system unless the test exposes a structural problem.

Values already specified inside the spell, Order, Skill, perk, Faction Skill, and Tower Elite tables remain experimental unless explicitly described as fixed. They are not duplicated here. This section instead closes the outstanding numerical gaps and defines conversion rules for legacy content.

###### Combat damage and defensive taxonomy

Ordinary creature attacks continue to use intrinsic Creature Attack and Creature Defense. Hero Primary Attributes are not substituted into this formula; hero Attack and Defense affect only mechanics that explicitly consume them.

Let Delta = attacker Creature Attack - defender Creature Defense. Before explicit damage bonuses and reductions:

If Delta >= 0: Attack/Defense Multiplier = 1 + min(3.00, 0.05 x Delta). If Delta < 0: Attack/Defense Multiplier = 1 - min(0.70, 0.025 x |Delta|). This preserves the familiar Heroes III relationship: offensive advantage adds 5% damage per point up to +300%, while defensive advantage removes 2.5% per point up to 70%.

Physical Damage Reduction is applied after the Creature Attack/Defense multiplier and offensive damage bonuses. Independent sources stack multiplicatively. For example, 20% and 30% Physical Damage Reduction produce Damage x 0.80 x 0.70 = 56% of pre-reduction damage. Explicit Physical Damage Reduction is capped at 80% after stacking; Creature Defense mitigation is a separate earlier layer.

Magical Damage Reduction uses the same multiplicative stacking rule but applies only to magical damage. It does not negate Slow, Teleport, Curse, control effects, or other nondamaging spell consequences. Natural Golem test values are Stone 50%, Iron 75%, Gold 85%, Diamond 95%; total Magical Damage Reduction is capped at 95%.

Magic Resistance is a separate chance roll. Dwarf 20% and Battle Dwarf 40% are retained as the baseline. If Magic Resistance succeeds, the hostile spell produces no effect on that stack. Each affected stack rolls separately against an area spell. Total Magic Resistance from all sources is capped at 75%.

Penetration wording is standardized. Effects that previously said they ignored physical Armor now ignore the stated fraction of Creature Defense for that attack. Effects that ignore Magical Damage Reduction multiply the target's current reduction by (1 - penetration). A 50% penetration effect against 60% Magical Damage Reduction therefore treats it as 30%.

###### Luck and Morale

|**Absolute value**|**Luck trigger chance**|**Morale trigger chance**|
|---|---|---|
|0|0%|0%|
|1|4%|3%|
|2|8%|6%|
|3|12%|9%|
|4|16%|12%|
|5|20%|15%|
|6|24%|18%|
|7|28%|21%|
|8|32%|24%|
|9|36%|27%|
|10|40%|30%|



Positive Luck: an eligible creature attack or retaliation deals 2.00x final direct physical damage. Negative Luck: it deals 0.50x final direct physical damage. Luck does not modify spell damage, war-machine damage, healing, or indirect damage unless an ability explicitly says otherwise. Sylvan Luck replaces the 2.00x positive multiplier with its listed 2.25x / 2.60x / 3.00x multiplier. Positive Morale: after the stack completes its normal Creature Activation, it immediately receives one additional Creature Activation at 75% direct damage. This bonus activation cannot trigger Morale again and cannot itself generate another additional activation. Negative Morale: the stack forfeits its normal Creature Activation when the trigger occurs. Creature Morale is checked only for normal Creature Activations, not extra activations granted by Second Wind!, Seize Initiative movement, or similar effects; Heroic Spirit's separate hero-Morale Order check follows its perk definition.

Hero Luck and Heroic Spirit test values reuse the curves above rather than introducing independent curves: current positive hero Luck gives 4% per point up to 40% at +10, and positive hero Morale gives 3% per point up to 30% at +10; both current dice denominators are 100. Nonpositive values do not trigger these positive effects. The saved hero perk chance scales are provisional tuning factors, not a second Luck or Morale attribute.

|**Captured field**|**Current test value**|
|---|---|
|heroLuck.luckyDayChanceScalePercent|100%|
|heroLuck.erraticManaChanceScalePercent|100%|
|heroLuck.druidicLuckChanceScalePercent|100%|
|heroLuck.orderMultiplierPercent|200% (2.00x)|
|heroLuck.damageMultiplierPercent|200% (2.00x)|
|heroLuck.manaRefundPercent|50%, rounded down from actual spent Mana|
|heroMorale.heroicSpiritChanceScalePercent|100%|

These optional captured blocks use the listed defaults when absent in an earlier snapshot. Each chance scale and multiplier is an integer from 0 to 1000 percent; Mana refund is an integer from 0 to 100 percent. Heroic Spirit's exactly one Order-only grant is a structural rule, not a configurable numerical grant count. Numerical tuning belongs here; eligibility, origin preservation and no-recursion rules belong to the perk definitions above.

###### Leadership progression and creature requirements

Let M = Attack growth + Defense growth for the hero class. Experimental class Leadership is: Starting Leadership = 500 + 75 x M. Leadership gained per hero level = 20 + 5 x M. Hero Leadership(L) = Starting Leadership + Growth x (L - 1).

|**Might:Magic polarity**|**Classes**|**Start**|**Per level**|**Level 30**|
|---|---|---|---|---|
|8:2|Barbarian, Beastmaster|1100|60|2840|
|7:3|Knight, Tyrant, Overlord|1025|55|2620|
|6:4|Ranger, Death Knight|950|50|2400|
|5:5|Battle Mage, Shaman,<br>Planeswalker|875|45|2180|
|3:7|Cleric, Cultist,<br>Necromancer, Warlock,<br>Witch|725|35|1740|
|2:8|Druid, Wizard,<br>Elementalist|650|30|1520|



Creature Leadership Requirements below are for the base creature. An upgraded form normally uses 120% of the base requirement, rounded to the nearest 10. Stack capacity remains floor(Hero Leadership / Creature Leadership Requirement). No Skill or Recruitment effect applies a percentage discount to these requirements.

Academy's Gremlin and Master Gremlin presentation is replaced by original Cabir
and Cabir Master. Both have empty hands, with no golden pot or fire vessel.
The approved Cabir home-agent handoff v1 supersedes the earlier hand-fired visual:
both forms spit fire from the mouth. Base Cabir has bare scales; Cabir Master
retains the approved light brass collar and cuffs. Both are ranged attackers with ordinary
ammunition and ranged targeting. The base form is not melee-only. Both retain
ordinary shooter melee penalties; this does not grant No Melee Penalty.
Both take 50% less Fire damage and 25% more Water/Frost damage (provisional values).
These are damage modifiers, not chances to resist an entire spell. Cabir Master
additionally repairs surviving allied Golem and Gargoyle stacks, including both
base and upgraded forms: repair heals wounds and restores fallen creatures within
that stack. It does not target an entirely destroyed stack. These
user-requested abilities supersede the initial unchanged-gameplay restriction;
retain unrelated existing stats, upgrade relationship, growth and Leadership
requirements. The initial tunable repair prototype is one use per combat,
restoring up to 10 HP per Cabir Master in the acting stack; it consumes that
stack's normal creature activation. These values are not inferred from Heroes VII.
Elemental modifiers use explicit damage-element tags, not the six Magic Schools;
untagged damage is unchanged. Supply complete
original base/upgraded creature art and battle
animation sets, with consistent army and recruitment portraits. Internal
Gremlin identities may remain for map/save compatibility while displayed names
and assets use Cabir. Provisional artwork follows the Heroes III Art workflow.

|Faction|Creature line|Category|Weekly growth|Leadership|Notes|
|---|---|---|---|---|---|
|Castle|Pikeman|Core|14|60||
|Castle|Archer|Core|10|90||
|Castle|Swordsman|Core|7|150|Moved down to Core|
|Castle|Griffin|Elite|5|220|Moved up to Elite|
|Castle|Monk|Elite|4|320||
|Castle|Cavalier|Elite|3|450||
|Castle|Angel|Champion|1|650||
|Rampart|Centaur|Core|14|60||
|Rampart|Dwarf|Core|8|90||
|Rampart|Wood Elf|Core|7|140||
|Rampart|Pegasus|Elite|5|210||
|Rampart|Dendroid|Elite|3|280||
|Rampart|Unicorn|Elite|3|420||
|Rampart|Green Dragon|Champion|1|650||
|Tower|Cabir|Core|16|50|Cabir Master upgrade; both forms shoot fire with bare hands and have elemental defenses; repair on upgraded form; unrelated Gremlin-line gameplay retained|
|Tower|Stone Gargoyle|Core|9|80||
|Tower|Stone Golem|Core|6|140||
|Tower|Mage|Elite|3|300|Premium shooter|
|Tower|Genie|Elite|4|240|Support-oriented|
|Tower|Naga|Elite|3|450||
|Tower|Giant|Champion|1|650||
|Inferno|Imp|Core|15|50||
|Inferno|Gog|Core|8|90||
|Inferno|Hell Hound|Core|5|150||
|Inferno|Demon|Elite|4|220||
|Inferno|Pit Fiend|Elite|3|320||
|Inferno|Efreet|Elite|2|430||
|Inferno|Devil|Champion|1|650||
|Necropolis|Skeleton|Core|18|45||
|Necropolis|Walking Dead|Core|8|80||
|Necropolis|Wight|Core|7|130||
|Necropolis|Vampire|Elite|4|230||
|Necropolis|Lich|Elite|3|330||
|Necropolis|Black Knight|Elite|2|470||



|Necropolis|Bone Dragon|Champion|1|650||
|Dungeon|Troglodyte|Core|14|55||
|Dungeon|Harpy|Core|8|90||
|Dungeon|Beholder|Core|7|140||
|Dungeon|Medusa|Elite|4|230||
|Dungeon|Minotaur|Elite|3|330||
|Dungeon|Manticore|Elite|2|430||
|Dungeon|Red Dragon|Champion|1|650||
|Stronghold|Goblin|Core|15|50||
|Stronghold|Wolf Rider|Core|8|90||
|Stronghold|Orc|Core|7|140||
|Stronghold|Ogre|Elite|4|250||
|Stronghold|Roc|Elite|3|300||
|Stronghold|Cyclops|Elite|2|450||
|Stronghold|Behemoth|Champion|1|650||
|Fortress|Gnoll|Core|14|55||
|Fortress|Lizardman|Core|9|90||
|Fortress|Serpent Fly|Core|8|140||
|Fortress|Basilisk|Elite|4|230||
|Fortress|Gorgon|Elite|3|340||
|Fortress|Wyvern|Elite|2|430||
|Fortress|Hydra|Champion|1|650||
|Conflux|Pixie|Core|14|45|Upgrades to Sprite; restored single Core line|
|Conflux|Wisp|Core|8|140|Second Core line; upgraded form Leadership 170; provisional supplied values|
|Conflux|Air Elemental|Elite|4|250|Storm upgrade Leadership 300|
|Conflux|Water Elemental|Elite|4|250|Ice upgrade Leadership 300|
|Conflux|Fire Elemental|Elite|4|250|Energy upgrade Leadership 300|
|Conflux|Earth Elemental|Elite|4|250|Magma upgrade Leadership 300|
|Conflux|Phoenix|Champion|1|650||

###### Classical Conflux Elemental rebalance values

These values supersede the previous experimental values for these eight
creatures only. All eight are Elite, with weekly base growth 4. Base creatures
cost 550 Gold and require 250 Leadership each; upgraded creatures cost 750 Gold
and require 300 Leadership each. Growth modifiers from separate buildings or
mechanics are additional, not included in these base growth figures.

|Creature|Attack|Defense|Damage|HP|Speed|Initiative|Gold|Leadership|Growth|
|---|---|---|---|---|---|---|---|---|---|
|Air Elemental|12|10|7–10|45|10|14|550|250|4|
|Storm Elemental|13|11|8–11|50|10|15|750|300|4|
|Water Elemental|11|12|7–10|50|6|9|550|250|4|
|Ice Elemental|12|14|9–12|60|6|10|750|300|4|
|Fire Elemental|14|9|9–12|40|8|12|550|250|4|
|Energy Elemental|15|10|11–14|45|10|13|750|300|4|
|Earth Elemental|10|14|8–11|55|4|6|550|250|4|
|Magma Elemental|11|16|10–13|65|5|7|750|300|4|

One unmodified week's growth across the four lines supplies 16 creatures:
8,800 Gold and 4,000 Leadership for all base forms, or 12,000 Gold and 4,800
Leadership for all upgraded forms. Pixie/Sprite, Wisp and Phoenix numerical
values are unchanged by this classical Elemental rebalance.

###### Wisp roster and experimental values

The Wisp handoff roster decision supersedes the independent Sprite Grove and
eighth recruitment slot: restore the Pixie dwelling's Sprite upgrade. The former
Psychic/Magic Elemental dwelling line becomes the Wisp base/upgrade dwelling,
using the intended Altar of Magic artwork. Preserve the original Psychic and
Magic Elemental identities for existing maps and saves, but exclude them from
this town's recruitment roster. Wisps use separate stable creature identities.

Both Wisp forms are non-shooters. Pass-through permits traversal through occupied
or blocked battlefield positions, but movement must end on an ordinarily legal,
unoccupied hex and uses the normal movement budget. It is not Flying, does not
grant adventure-map bypass, and grants neither immobilization nor hazard immunity.
Long Reach 5 permits physical melee attacks across five intervening hexes
(maximum hex distance 6). Their innate 95% Magical Damage Reduction uses normal
magical penetration rules and does not block status spells or reduce physical
damage. Shooting artwork does not grant ammunition or Shooter capabilities.

Use the supplied prototype values initially: Wisp Attack/Defense 7/4, damage
2–3, Health 12, Speed/Initiative 5/6, Gold 160; upgraded Wisp 9/6, damage 3–4,
Health 16, Speed/Initiative 6/7, Gold 240. Both share weekly growth 8. The upgrade's
provisional display name is Greater Wisp. Its outgoing attacks prevent retaliation,
including adjacent attacks; it retains normal retaliation when attacked adjacently.
Long Reach requires a direct clear hex corridor: intermediate stacks, intact walls
and solid obstacles block attacks. When hex geometry admits multiple equally direct
corridors, any clear one suffices. Pass-through movement does not bypass this attack
restriction. These supplied rules and values are approved provisionally for playtesting.
Attack uses MAGICBLT.wav once per physical attack; movement uses AELMMOVE.wav,
and hit reactions use AELMWNCE.wav. These sounds do not turn attacks into spells.



###### Castle Griffin / Swordsman swap

The category swap is accompanied by a modest stat/economy adjustment rather than being a label-only change. Other Castle lines initially retain their Heroes III Complete combat statistics while the full creature pass is tested.

|**Creature**|**Cat.**|**A / D**|**Damage**|**HP**|**Speed /**<br>**Init.**|**Growth**|**Gold**|**Leadership**|**Notes**|
|---|---|---|---|---|---|---|---|---|---|
|Swordsman|Core|8 / 10|5-7|30|5 / 5|7|250|150|Durable<br>Core<br>infantry|
|Crusader|Core|10 / 11|6-8|35|6 / 6|7|350|180|Double<br>attack<br>retained|
|Griffin|Elite|9 / 9|4-7|30|7 / 7|5|300|220|Two<br>retaliations|
|Royal Griffin|Elite|11 / 11|6-9|40|9 / 9|5|450|260|Unlimited<br>retaliations|



###### Creature conversion defaults

Until a faction receives a dedicated creature-stat pass, its old Heroes III Complete Attack, Defense, damage, HP, shots, special abilities, upgrade relationships, and Gold/resource costs are the starting experimental values. Old Speed becomes both battlefield Speed and Initiative as the initial conversion; later role tuning may separate the two. Weekly growth and Leadership are replaced by the table above. This keeps the new systems testable without pretending every legacy creature has already been individually re-authored.

###### Conflux Elementals and terrain mapping

The four Elite Elemental lines are Air, Water, Fire and Earth. Pixie upgrades to
Sprite as one Core line; Wisp and its upgrade form the second Core line. Phoenix
remains the Champion. The earlier five-Elite/independent-Sprite roster is superseded.

|**Battlefield terrain**|**Elemental Convergence result**|
|---|---|
|Lava / volcanic / burning terrain|Fire Elemental|
|Snow / ice / shallow water / water-rich terrain|Water Elemental|
|Rough / subterranean / stone-dominant terrain|Earth Elemental|
|Grass / highlands / open-sky terrain|Air Elemental|
|Magic plains / Conflux / explicitly arcane terrain|Magic Elemental|
|Dirt / inland Sand / Wasteland|Earth Elemental|
|Swamp / coastal battlefield, including coastal Sand|Water Elemental|

Coastal battlefield context takes precedence over its forced Sand terrain.
Use the captured battlefield terrain and coastal context, not the hero's
adventure-map tile, for Elemental Convergence and its terrain-dependent perks.



###### Siege, war machines, and fortifications

|**Machine / structure**|**Experimental output**|**Other value**|
|---|---|---|
|Ballista|Damage = 50 + 2.0 x Siege|300 HP; one normal activation per round|
|Catapult|Structural Damage = 100 + 3.0 x Siege|500 HP; damage applies to selected<br>fortification section when permitted|
|First Aid Tent|Healing = 75 + 3.0 x Siege|250 HP; cannot restore casualties without<br>Battlefield Medic|
|Ammo Cart|No Siege-scaled output|250 HP|
|Defensive Tower|Damage = 60 + 1.5 x defending hero Siege|350 structural HP|



|**Machine / structure**|**Experimental output**|**Other value**|
|---|---|---|
|Wall section|300 structural HP|No hero Siege contribution to durability|
|Gate|450 structural HP|No hero Siege contribution to durability|



Counter-Battery currently grants +50% final damage against enemy war machines. Battlefield Medic may convert at most 50% of the Tent's calculated healing into casualty restoration. Field Workshop uses the full Tent healing value for repairs. These numbers are experimental.

###### Adventure Magic and Mage Guild economy

|**Guild**|**Adventure Spell**|**Mana**|**Per-town unlock cost**|**Key restriction**|
|---|---|---|---|---|
|I|Summon Boat|20|2,500 Gold + 1 of each<br>precious resource|One legal adjacent water<br>destination; one Adventure<br>Spell/day|
|II|Water Walk|30|5,000 Gold + 2 of each<br>precious resource|Water steps cost 1.50x;<br>must end day on land|
|III|Town Portal|50|10,000 Gold + 4 of each<br>precious resource|Nearest controlled town<br>only; ends Movement|
|IV|Fly|60|15,000 Gold + 6 of each<br>precious resource|Flying steps cost 1.50x;<br>protected barriers remain<br>impassable|
|V|Dimension Door|80|25,000 Gold + 10 of each<br>precious resource|Range 8 visible legal tiles;<br>ends Movement|



New Mage Guild construction levels use common experimental costs where a faction previously lacked the level: Mage Guild IV = 5,000 Gold + 5 Mercury + 5 Sulfur + 5 Crystal + 5 Gems; Mage Guild V = 10,000 Gold + 10 of each precious resource. Existing factions that already had those levels keep their current build-tree prerequisites but use the same cost during the first balance pass.

House of Wisdom scrolls cost 1,000 Gold per spell level. Each House has six distinct eligible combat-spell offers, generated once and preserved in saves; purchasing one removes it from that House's stock.

###### Ordinary Mage Guild spell generation

|**Guild level**|**Ordinary spell slots**|**Experimental composition**|
|---|---|---|
|I|5|1 Preferred A, 1 Preferred B,<br>3 different non-preferred|
|II|4|1 Preferred A, 1 Preferred B,<br>2 different non-preferred|
|III|2|1 Preferred A, 1 Preferred B|
|IV|2|1 Preferred A, 1 Preferred B|
|V|2|1 Preferred A, 1 Preferred B|



The two Preferred Schools are equal; neither is Major or Minor. Every Guild level reserves exactly one slot for each Preferred School. Level I also selects three different non-preferred schools uniformly without replacement from the other four schools; Level II selects two different non-preferred schools the same way; Levels III-V contain no non-preferred slots. Within each required school-and-level pool, every eligible ordinary combat spell has the same chance.

Faction spell weights are not used. Once a spell fills a slot, it is removed from all remaining pools for that Guild level, so duplicate spells cannot occur even when a spell belongs to multiple schools. If a required school has no eligible spell at that level because of the saved roster, map bans, or other ordinary eligibility rules, that slot remains empty and is not replaced by another school. A complete Guild with all required pools available contains 15 ordinary combat spells. The Tower Library does not alter spell generation. This is ordinary spell generation, not Spell Research.

Tower Library experimental values: +1 Mage / Arch Mage growth per week; construction cost 15,000 Gold, 10 Wood, 10 Ore, 5 Mercury, 5 Sulfur, 5 Crystal, and 5 Gems. Prerequisites: Mage Tower dwelling and Mage Guild IV.

###### Creature-growth buildings

|**Faction**|**Building**|**Experimental growth effect**|
|---|---|---|
|Castle|Griffin Bastion|+3 Griffin growth/week|
|Rampart|Miners' Guild|+4 Dwarf growth/week|
|Rampart|Dendroid Saplings|+2 Dendroid growth/week|
|Tower|Library|+1 Mage / Arch Mage<br>growth/week|
|Inferno|Birthing Pools|+8 Imp growth/week|
|Inferno|Cages|+3 Hell Hound growth/week|
|Necropolis|Unearthed Graves|+6 Skeleton growth/week|
|Dungeon|Mushroom Rings|+7 Troglodyte growth/week|
|Stronghold|Mess Hall|+8 Goblin growth/week|
|Fortress|Captain's Quarters|+6 Gnoll growth/week|
|Conflux|Garden of Life|+4 Pixie and +3 Sprite growth/week|
|Conflux|Vault of Ashes|+2 Fire Elemental growth/week|



###### Artifact rebalance framework

|**Artifact family**|**Experimental conversion**|
|---|---|
|Primary Attribute bonus/penalty|Multiply the old H3 numerical bonus or penalty by 5. Example: +3<br>Knowledge -> +15 Knowledge.|
|Luck / Morale|Keep the printed numerical modifier unchanged because New Horizons<br>already uses a -10 to +10 scale.|
|Land Movement flat bonus|Divide the old H3 movement-point bonus by 10. Equestrian's Gloves -><br>+30 MP; Boots of Speed -> +60 MP.|
|Sea Movement flat bonus|Divide the old H3 value by 10. Necklace of Ocean Guidance -> +100<br>sea MP.|
|Speed artifact|Each old +1 Speed becomes +1 battlefield Speed and +1 Initiative.|
|Mana regeneration artifact|Old +1 / +2 / +3 daily Mana tiers become max(5, 5% max Mana) /<br>max(10, 10%) / max(15, 15%) daily recovery.|
|Magic Resistance artifact|Keep the original percentage chance. Artifact bonuses add to innate<br>Magic Resistance; total Magic Resistance caps at 75%.|
|Elemental damage Orb|Old +50% elemental spell damage becomes +25% final magical<br>damage for spells tagged with that element.|
|Resource / Gold artifact|Keep legacy income for the first economy pass unless a separate town/economy test shows inflation.|
|Spell-duration / immunity artifact|Keep the original qualitative effect unless it refers to a removed school<br>or Adventure Spell.|

Elemental Orb tags describe a spell's actual element/theme, independently of its
Magic School memberships. Fireball is Fire, Ice Bolt is Water/Frost, and Lightning
Bolt is Air/Lightning. Neutral and necrotic spells are untagged and receive no
elemental Orb bonus. Do not preserve legacy Heroes III School affinities as
element tags or infer elements from the six New Horizons Schools.



Adventure-movement artifact exceptions: Boots of Levitation allow Water Walk for 20 Mana without requiring the spell to be learned, but the cast still consumes the hero's one Adventure Spell for the day. Angel Wings allow Fly for 40 Mana under the same rule. Neither item permits free chaining with Town Portal or Dimension Door.

A combat spell legitimately inscribed in a hero's spellbook may be cast regardless of School proficiency; School proficiency governs acquisition, not permission to use an already-inscribed spell. This grants no unknown or removed spell and bypasses no Spell Point cost, typed action, target, immunity, Spell Lock, or Adventure Spell rule. Spellbinder's Hat is redesigned around temporary availability: while equipped, all eligible Level 5 combat spells are inscribed in the hero's spellbook. Removing it ends only access supplied by the Hat and never erases independently learned spells. The four legacy elemental Tomes are removed from the experimental random-artifact pool until a six-school replacement set is authored; they should not silently map four old schools onto six new ones.

Hero starting profiles audit starting Skills, spells, specialties, and armies together against the current registries and mechanics. Removed or replaced vanilla mechanics must not leave stale spells, empty starting choices, obsolete specialties, or armies that violate Leadership. Author a replacement where the design leaves a gap rather than inferring one; Halon's former Mysticism package and the remaining three-starting-development profiles therefore require explicit authored replacements before activation.

Thant's authored provisional replacement uses the existing Re-animate spell for
his fresh default starting inscription and specialty instead of removed Animate
Dead. Apply the non-damage specialty conversion only to Re-animate's Spell
Power-derived restoration component (+20%); its fixed220 HP component and
ordinary temporary casualty eligibility, targeting, costs and actions remain
unchanged. Do not add a separate Masterful spell or hero-level scaling rider.
Preserve map-prescribed spellbooks, captured legacy rules, class, biography and
other starting choices. Revisit restoration-cap/casualty composition during
integration; this replacement does not authorize other missing hero profiles.

Cuthbert, Olema, Mirlanda and Xsi use the existing Shadow Frailty spell for
their fresh default starting inscription and specialty instead of removed
Weakness/Stone Skin. Apply +20% only to Frailty's Spell Power-derived Defense-loss
component; preserve its fixed10%, per-cast20% and cumulative60% caps. Preserve
class, biography, other starting choices, map-prescribed spellbooks and captured
legacy contexts. These authored provisional replacements add no separate
Masterful spell or new hero-level rider; revisit specialist differentiation
during integration and balance.

Aenain uses existing Shadow Frailty instead of inaccessible Disrupting Ray for
his fresh default inscription and specialty. Apply +20% only to its Spell
Power-derived Defense-loss component, preserving the fixed10%, per-cast20%
and cumulative60% caps. Preserve other starting choices, class, biography,
map-prescribed spellbooks and captured legacy contexts. This provisional
replacement follows the targeted Defense-debuff role; it is not a new Masterful
spell or approval of an undefined Earthquake specialty.

###### Hero specialty conversion

|**Specialty family**|**Experimental conversion**|
|---|---|
|Primary Attribute specialty|Use the new attribute scale directly; a legacy flat +1-equivalent<br>specialty becomes +5 to that Primary Attribute.|
|Creature-line specialty|Affected creature line gains +1 Speed and +1 Initiative; additionally<br>+1 Creature Attack and +1 Creature Defense per 5 hero levels,<br>maximum +6/+6 at level 30.|
|Damage-spell specialty|+15% to that spell's Spell Power-derived numerical damage<br>component.|
|Non-damage spell specialty|+20% to the spell's Spell Power-derived numerical component; if<br>the spell has no numerical SP component, +1 round duration when<br>duration exists.|
|Skill specialty|+20% to the Skill's core numerical effect only; does not strengthen<br>perks.|
|Resource specialty|Retain the legacy daily resource quantity for the first economy<br>pass.|



###### Economy defaults and scope

Unless this document explicitly changes a creature, its Gold/resource recruitment cost and dwelling/upgrade construction cost begin from Heroes III Complete values. Renamed unique buildings retain the construction cost of the graphic/building they replace for the first test pass. This isolates the effect rebalance from the economy rebalance. The explicit exceptions are the

Tower Mage/Genie changes, the Castle Griffin/Swordsman swap, new Mage Guild IV/V costs, Adventure Spell unlocks, and House of Wisdom scroll prices listed above. Grail buildings retain their legacy universal +5,000 Gold/day and +50% creature-growth baseline during the experimental build. Faction-specific Grail effects require a later identity pass if they refer to mechanics that no longer exist; until then they are enabled only where the original effect remains mechanically valid.

The experimental section is intended to be updated aggressively during playtesting. Whenever a previously unspecified number is introduced elsewhere in the design, it should either be added here or explicitly declared fixed in its owning section so numerical uncertainty stays visible rather than becoming accidental canon.

# Future Design — Governors and Regional Administration

#### <mark>Introduction</mark>

The proposed governor system keeps the kingdom as the owner of its possessions while introducing regions as the places through which those possessions are administered. It does not divide the map into packages that automatically change hands when a town falls. Ownership, geography, and administration remain distinct.

The kingdom determines whose possessions they are. The region determines which town can organize them. The governor determines what that regional administration can accomplish.

The purpose is to make towns meaningful centers of their surrounding countryside and to give secondary heroes useful responsibilities beyond ferrying troops. Governance should provide decisions about recruitment, infrastructure, defense, and regional development, rather than simply attaching another collection of passive bonuses to a hero. This section establishes the conceptual proposal; formulas, numerical balancing, and implementation details remain undecided.

#### The kingdom remains the whole

The kingdom remains the common owner and financier. It retains one treasury and one kingdom-wide stockpile of resources. Towns, flagged mines, dwellings, heroes, and armies belong to the player, not to independent provincial governments. The one-Capitol-perkingdom restriction remains.

A sawmill still produces wood for the kingdom, and that wood can be spent on a building in any owned town. Regional administration does not introduce separate provincial treasuries or require caravans to transport every unit of ore. Money and resources remain abstract and kingdom-wide; armies and reinforcements remain physical and local.

Regions add a way to organize the kingdom's holdings. They do not turn each town into a self-contained miniature kingdom or require a new economy of population, happiness, loyalty, or bureaucracy.

#### Regions and administrative seats

For the initial proposal, each town serves as the administrative seat of a surrounding region. The region is a recognizable geographic district: a valley, a forest basin, a stretch of coast, or the countryside around a fortress. Mines, external dwellings, roads, and other locations have a regional affiliation.

A region's geography should remain stable during a scenario. Capturing its town changes who can administer it, not where its boundaries lie. The player should be able to learn the

geography and make plans around it. Whether mapmakers define the districts or the game establishes them automatically is a later implementation question.

Not every part of the map must belong to a town. Wilderness, remote islands, and isolated underground areas may remain outside regional administration. Players can still explore those places and own holdings there.

Regional boundaries are administrative, not barriers to movement. Heroes continue to cross the adventure map normally. Crossing a boundary changes which administration is relevant; it does not create an artificial toll or prevent an army from marching onward.

#### Ownership and administration

Every holding has two independent relationships: its geographic region and its current owner. An ore pit can belong to the Ironpass region while being owned by the Blue player, regardless of who currently owns Ironpass itself.

The proposed rule is that a player must control both a holding and its region's administrative seat to administer that holding through the region. Actions involving physical transportation additionally require a usable route. Regional affiliation alone cannot move creatures through a mountain or an enemy blockade.

Owning a town therefore grants a base for administration, not automatic possession of everything nearby. A governor cannot collect an opponent's mine income, recruit from a neutral dwelling, or claim nearby creatures merely because those locations lie within the region. Heroes must still conquer them.

A holding that the player owns without controlling its regional town remains an outlying possession. It continues its ordinary function, such as a mine contributing resources to the kingdom, but does not receive services dependent on that player's regional administration. Conversely, controlling a regional town does not make the surrounding hostile countryside safe.

#### Example: Oakvale

Oakvale contains a town, a sawmill, a gold mine, and an external creature dwelling. The player owns the town and sawmill, an opponent owns the gold mine, and the dwelling remains neutral.

The player may appoint Oakvale's governor and administer the sawmill through that town. The gold mine continues to belong to the opponent. The neutral dwelling remains unavailable for organized recruitment until captured. Securing the administrative seat and securing the countryside are related but separate objectives.

If the opponent captures Oakvale's town, the player's authority to administer the region through Oakvale ends. Governor services dependent on the town stop, and reinforcement arrangements using it as a destination or transfer point must be interrupted or redirected.

The sawmill does not automatically change sides. It remains the player's possession until separately captured and continues its ordinary production without the lost administration's services. Friendly armies elsewhere in the region also remain friendly; they are neither expelled nor transferred when the town falls.

This makes the town strategically important without rendering every other position irrelevant. The precise fate of the displaced governor and of caravans already travelling is still an open rules question.

#### The governor's role

Regions exist and ordinary production functions without an appointed governor. Towns still build, mines still produce, and creatures still grow. A governor is not a compulsory fee that must be paid before a town can function.

Appointing a hero adds directed administration: the ability to undertake regional policies and projects. A field hero is commanded through movement, exploration, and battle. A governor is commanded through decisions about what to organize, develop, and prepare. Neither role should amount to merely waiting for a passive bonus.

The current recommendation is that governing should be a genuine service assignment. A governor would be based at the regional seat rather than administering it at full effectiveness while simultaneously campaigning anywhere on the map. The hero could be relieved and returned to field service. The precise residence requirement, appointment process, and transition rules remain to be decided.

Governorship should provide an alternative role for existing heroes, not require a rigid new Governor class. A hero may move from military command into administration or return to field command in an emergency. Administrative usefulness should come from relevant specialization rather than automatically making the highest-level conquering hero the best choice for every town. Specific skill interactions will be defined after the ownership and administration model is settled.

#### Actions and standing orders

Governor actions should operate on actual holdings and connections. The initial system should concentrate on a small set of concrete activities rather than covering the region with invisible combat bonuses or introducing several additional currencies.

###### **Organize recruitment**

The governor establishes collection from owned external dwellings to the regional town. The player authorizes recruitment spending from the kingdom treasury, and the recruited creatures physically travel to that town. Dwellings remain locations that must be captured and protected, but a hero no longer needs to repeat the same collection circuit simply to perform routine logistics.

###### **Develop local infrastructure**

The governor undertakes a project involving a particular road or productive holding. The player chooses which connection or mine deserves investment. Development is a commitment to an identifiable place, not a blanket modifier applied to everything within the regional boundary.

###### **Prepare regional defenses**

The governor organizes a defensive measure at the town or a designated position. Any actual defenders must come from an explicit recruitment or reserve mechanism. Fortifying a region should not mean that every friendly creature mysteriously becomes tougher whenever it crosses the boundary.

Standing orders and major projects should be distinct. An instruction to collect recruits from a dwelling remains in force until changed or interrupted. The meaningful decisions are whether to fund it, where to send its output, and how to protect the route, not whether the player remembered to repeat the same click every week.

The action economy, project duration, costs, and any limits on simultaneous activity are not yet defined. These examples establish the purpose of regional administration without treating a preliminary action list as a finalized ruleset.

#### Logistics without hero chaining

The design principle is that heroes move armies while infrastructure moves reinforcements. Secondary heroes should be independent strategic actors, not a sequence of interchangeable movement-point extensions for the main army.

Regional towns become places where recruitment is gathered and onward transport is organized. Reinforcements can travel between friendly towns and toward a forward base, making distance, geography, route security, and intermediate positions relevant. This is not an instantaneous transfer into a campaigning hero's army.

Ordinary troop transportation must not consume a scarce governor action or require a particular hero build. Otherwise the system would replace hero chaining with another compulsory administrative chore. Basic infrastructure should provide the alternative to chaining; governors may improve that infrastructure or undertake special logistical preparations.

No particular anti-chaining restriction is settled in this proposal. The precise rules for heroto-hero transfers, caravan interception, transport routes, and delivery to field armies must be designed together later. The replacement system should preserve logistical decisions while removing the incentive to move one army through a same-turn relay of heroes.

#### The Capitol

The kingdom's Capitol and a region's administrative seat are different concepts. A town does not need a Capitol to administer its surroundings, and regional administration should not wait until the kingdom has built one.

The initial proposal preserves the Capitol's existing kingdom-level role rather than immediately attaching a new empire-wide government system to it. Kingdom-level administrative decisions could be considered separately in the future.

Regional connections do not all have to pass through the Capitol. Two frontier towns should be able to support one another directly, and losing the Capitol should not automatically stop administration in every other town. The kingdom remains the common owner and financier while administration is distributed among its regional seats.

#### Regional identity and the Kingdom Overview

Regional identities should emerge from geography, holdings, and player decisions rather than from selecting a fixed province class. A town surrounded by mines may become an economic district. A town near several external dwellings may become a recruitment center. A town controlling the approach to enemy territory may become a defensive and reinforcement base.

The Kingdom Overview can organize possessions by region, showing the administrative seat, governor, ongoing project, recruitment arrangements, and interrupted connections. Holdings whose regional town is not controlled remain visible as outlying possessions rather than disappearing from the player's economic picture.

The adventure map remains the place for exploration, conquest, raids, and battle. Regional administration is how secured holdings become an organized support system. Its governing principle is not "own the town, therefore own the region," but "own the town, therefore have a base from which to organize your possessions in the region."

#### Open design questions

The next design decisions concern how regions are established, the governor's physical commitment and return to field service, the distinction between automatic services and

deliberate actions, and what happens when routes or administrative seats are lost. Transportation and anti-chaining rules must be developed as one coherent system.

Costs, cooldowns, numerical bonuses, hero-skill mappings, progression, and AI implementation remain outside this conceptual draft. The aim at this stage is to establish ownership, jurisdiction, responsibility, and the decisions the player should make before choosing formulas.

# UI Work and Implementation Requirements

The UI plan distinguishes systems that already have working presentation from mechanics that still require interaction, targeting, state feedback, or dedicated panels. The existing Hero screen redesign and the spellbook school bookmarks / school artwork are retained; they need integration with the rules below, not another visual redesign.

Battle logs record meaningful combat interactions with actor, cause, target, mechanical effect, and authoritative result. Metamagic follow-ups name the hero, follow-up ordinal, triggering Metamagic effect, spell, and resulting damage or other outcome. Orders and perks expose applicable numerical bonuses, prevention, formulas, and conditions, then record realized outcomes when they resolve. Use direct Heroes III-style wording, omit repetitive New Horizons prefixes and obsolete legacy labels, and attribute mitigation to the actual mechanic. All new purpose-made art, including provisional art, follows the Heroes III art workflow and retains source prompt/reference provenance. The UI/asset register covers every live Skill, perk, Order, spell, and derived attribute, classifying each asset as Not done, Provisional, or Final; borrowed unrelated icons are Not done, while Final requires explicit approval evidence. Historical school-bookmark and Metamagic classifications remain approval records rather than automatic claims about current runtime bindings.

### Existing UI to retain and extend

|**Area**|**Status**|**Required integration**|
|---|---|---|
|Hero screen|Working|Keep the current redesigned layout.<br>Populate the complete Skill roster,<br>Basic / Advanced / Expert rank,<br>exactly one perk slot per tier,<br>class/faction restrictions, and<br>Faction Skill. The Skill-probability<br>pane uses Skill icons, excludes<br>other factions' unique Skills, and<br>opens from a small gold circled serif<br>information control beside the<br>Skills / learned perks heading with<br>leather showing through and no<br>rectangular button background.<br>Stormcaller, Movement, hero<br>Leadership, and creature<br>Leadership use purpose-appropriate<br>visible icons. No new top-level<br>Hero-screen redesign is required.|



|**Area**|**Status**|**Required integration**|
|---|---|---|
|Spellbook school navigation and<br>school art|Working|Keep the existing six-school<br>bookmarks and new school artwork.<br>Add a separate neutral Adventure<br>Spell section rather than treating<br>Adventure Magic as a seventh<br>school. Extend spell acquisition<br>offers with School-rank<br>requirements, and extend inscribed<br>spell entries with Wisdom-adjusted<br>ordinary spell costs, Mass-variant<br>availability, special casting controls,<br>and richer targeting feedback.<br>Never present insufficient School<br>proficiency as a casting lock on a<br>legitimately inscribed combat spell.<br>The player may always open and<br>inspect the spellbook after casting<br>opportunities are exhausted;<br>inspection never authorizes an<br>illegal cast, automatically reopens<br>the book, or repurposes Wait as a<br>decline control.|
|Creature / stack information|Needs extension|Show Speed and Initiative as<br>separate full rows. Show Leadership<br>Cost as a full row with its simple<br>yellow monochrome crown icon,<br>and show the inspected stack's<br>current size / maximum<br>commandable count such as 5 / 11<br>while keeping per-creature and<br>total-stack Leadership costs distinct.<br>Core / Elite / Champion is a<br>horizontal stat row with a simple<br>yellow monochrome ascending<br>stair-step-line icon, not a ladder with<br>rails and rungs. Preserve existing<br>useful statistics, readable<br>icon/label/value spacing, and panel<br>styling rather than shrinking them to<br>make room.|
|Town screens and building art|Needs extension|All towns now reach Mage Guild V.<br>Create five new Mage Guild<br>upgrade visuals: Castle V;<br>Stronghold IV and V; Fortress IV<br>and V. Each needs town-screen<br>building art/state, construction<br>icon/button art, selection<br>mask/hitbox, correct<br>z-order/layering, and any animation<br>frames used by that town. Reuse<br>existing unique-building locations<br>where mechanics are renamed:<br>Wall of Knowledge art -> Arcane<br>Reservoir; Lookout Tower art -><br>Astronomy Tower; Mana Vortex art<br>->Astral Nexus.|



### Combat hero action and Orders

|Component|Required UI behavior|
|---|---|
|Hero Action state|Show whether the hero's normal Hero Action for the round is available or already spent. Do not introduce a general action-token currency or a panel presenting Hero, Spell, and Order as three independent counters. Automatic extra Spell or Order opportunities granted by specific rules are contextual extensions of the ordinary Spell or Order controls; show their source and expiry there. Generic Skill resources or states use the provider-driven Faction Skill status presentation. The panel uses surrounding leather, red outlines, gold detailing, and readable spacing rather than a bare box.|
|---|---|
|Metamagic / Divine Mandate / Double Command|Metamagic and Double Command trigger<br>automatically when their conditions are met.<br>Expose the resulting additional Spell or Order<br>opportunity through the ordinary casting or<br>Orders interface, show its source and expiry,<br>and do not show an Activate / Decline perk<br>prompt. Metamagic's remaining used-<br>sequence allowance is a Skill-provided<br>combat status, not a count of currently<br>available Spell Actions. Divine Mandate<br>retains its Skill-defined contextual follow-up<br>behavior and must be labeled as a Faction<br>Skill effect rather than a perk activation.|
|Orders panel|Provide finished icons for all eight Orders, current calculated<br>numerical effect using the hero's ratings and Command, duration,<br>target type, and disabled-state explanation. The combat Orders<br>control uses a readable monochrome yellow gauntlet; the chooser<br>uses Heroes III textured leather rather than a flat grey panel.<br>Disabled Orders remain right-click inspectable. Targetless army-<br>wide Orders resolve after confirmation; targeted Orders enter<br>battlefield targeting mode rather than opening a second target<br>menu.|
|Order targeting|Focus Fire! and Flank! highlight legal enemy stacks. Protect! uses<br>a two-step Protector -> Ward selector and previews the adjacency<br>link. Second Wind! highlights only friendly stacks that have already<br>completed their normal Creature Activation this round. Invalid<br>targets explain why they are invalid.|
|Order state on creatures|Show compact status badges with source and duration. Charge!<br>should indicate whether a stack's qualifying first attack is still<br>available; Hold the Line! should mark the anchored position; Brace!<br>should show readiness; Protect! should visually link Protector and<br>Ward; Focus Fire! / Flank! should mark the designated enemy.<br>Orders are not dispellable spells and do not inherit spell-duration<br>modifiers. Heroes likewise show active-effect states such as<br>Warcasting. Reuse suitable existing animations but distinguish<br>different Orders; exact per-Order assignments remain<br>implementation judgment.|



|**Component**|**Required UI behavior**|
|---|---|
|Flank side tracking|While Flank! is active, show which attack sides have already<br>contributed to the distinct-side count and preview the bonus for a<br>proposed attack position.|
|Initiative integration|Second Wind!, Seize Initiative, Wait-based Battlecraft perks, Rapid<br>Response, and similar effects must update the initiative bar<br>immediately and distinguish a moved normal activation from a<br>genuinely additional activation.|
|Pre-combat command window|Battle Plan automatically presents its free<br>pre-combat Order before the first Creature<br>Activation. Redeployment and Grand Tactics<br>modify the ordinary deployment phase<br>automatically; use the standard deployment<br>interface and its normal confirmation flow<br>rather than separate perk buttons or prompts.|



### Spellbook, casting variants, and targeting

|**Component**|**Required UI behavior**|
|---|---|
|School proficiency locks|Spell acquisition offers show the School Skill rank<br>required for Levels 3 / 4 / 5. If an unknown spell is<br>visible from a Guild, teacher, scroll source, or other<br>acquisition interface but the hero lacks the required<br>rank, show why it cannot be acquired rather than<br>silently hiding it. A legitimately inscribed combat spell is<br>never casting-locked by insufficient School proficiency.|
|Wisdom cost display|Show listed Mana cost and current final casting cost<br>when Wisdom or other cost modifiers apply. Tooltips<br>should break down the calculation. Wisdom reduces<br>spell cost; School Skills unlock spell levels for<br>acquisition but never revoke casting access to<br>legitimately inscribed combat spells.|
|Mass spell variants|Perk-granted Mass variants appear as<br>distinct Spell entries once unlocked. Show<br>their own Mana cost, target scope, projected<br>effect, and any variant-specific magnitude<br>such as Mass Slow. The base Spell remains<br>a separate normal choice. Do not add a Mass<br>toggle to the base Spell and do not<br>automatically transform one Spell into<br>another because of cast count, Mana, target<br>state, or other runtime conditions.|



|**Component**|**Required UI behavior**|
|---|---|
|Magic Arrow Overcharge|After selecting a target, open a compact centered<br>segmented Overcharge modal in the Heroes III leather,<br>red, and gold visual style: - and + controls or a slider,<br>current Overcharge, maximum allowed by Spell Power,<br>base Wisdom-adjusted Mana cost, additional<br>Overcharge Mana, total Mana, and live projected<br>damage and estimated casualties both with and without<br>the selected Overcharge. Recalculate from the shared<br>battle forecast whenever Overcharge changes, using<br>current target health, partial casualties, temporary HP,<br>resistance, and mitigation. Disable unaffordable values<br>before confirmation and label uncertain outcomes as<br>estimates rather than guaranteed kills.|
|Perk casting modifiers|Perks never add optional Mana surcharges,<br>activation toggles, or cast-confirmation<br>modes. Automatic perk modifiers must be<br>reflected directly in the displayed Mana cost,<br>projected result, target preview, or duration.<br>Spell-intrinsic controls such as Magic Arrow<br>Overcharge remain part of the Spell itself.|
|Multi-target selector|Storm of Daggers needs selection of 1-5 enemy stacks<br>with numbered markers, running target count, total<br>damage pool, and projected damage on each selected<br>stack.|
|Area / orientation templates|Vengeful Vines selects exactly three connected hexes<br>with a live overlay and casts on the third legal click;<br>Backspace undoes the last selection and Escape cancels.<br>Fire Wall, Frost Ring, Inferno, Meteor Shower,<br>Earthquake, Time Stop, and similar geometry spells<br>preview every affected hex. Orientation-based spells<br>need rotation controls where applicable. Casting-choice<br>and confirmation panels are centered in the viewport.|
|Repeated placement spells|Land Mine and Quicksand require sequential placement<br>of the exact number of patches/mines, a remaining-<br>placement counter, undo-last-placement, confirm, and<br>caster-only visualization for concealed objects.|
|Relocation and transformation|Teleport highlights every legal destination. Blink<br>previews its possible radius rather than pretending the<br>destination is deterministic. Transfigure Matter<br>highlights valid obstacles and previews the resulting<br>summon HP / count before confirmation. Until purpose-<br>made final art is approved, Transfigure Matter<br>deliberately uses Remove Obstacle's spell icon rather<br>than an unrelated placeholder.|
|Summons and prisons|Summon Trolls, Verdant Prison, Elemental<br>Convergence, Phantom Army, and similar spells<br>preview legal placement, resulting aggregate HP /<br>count, footprint, and any choice of creature type before<br>the spell is committed.|
|Propagation preview|Chain Lightning and Nature's Wrath should preview<br>their deterministic or currently determined propagation<br>path and projected result per stack whenever the rules<br>make that information knowable before casting.|



|**Component**|**Required UI behavior**|
|---|---|
|State-exchange / state-count spells|Reality Warp should preview which buffs/debuffs will<br>move and flag non-transferable effects. Pandemonium<br>should show counted debuffs and projected damage<br>over affected stacks before confirmation.|
|Focus Magic / Arcane Breach state|A friendly stack enchanted by Focus Magic shows the spell icon and remaining duration. Enemy stacks carrying Arcane Breach show the current number of marks, their remaining duration, and the resulting total Creature Defense penetration for friendly ranged creature attacks. When previewing a ranged attack against a marked stack, show the Creature Defense penetration currently applying to that attack and indicate that a successful hit will add another Arcane Breach mark if the target is below the three-mark cap. If Arcane Ballistics is active and the target has 3 Arcane Breach marks, the preview also shows the applicable penetration of Physical Damage Reduction.|
|Spell Lock / Time Stop|Affected stacks need unmistakable status icons and<br>remaining duration. Spell Lock should identify whether<br>beneficial or hostile magical states are being preserved.<br>Time Stop should visually distinguish stasis from<br>ordinary disable effects and show frozen-duration<br>behavior in the tooltip.|
|Friendly-fire confirmation|Armageddon and other explicitly indiscriminate spells<br>require a confirmation that clearly previews affected<br>friendly stacks. Do not add the same warning to spells<br>whose perk or targeting rules already exclude friendlies.|
|Adventure Spell section|Display the five neutral Adventure Spells in a separate<br>spellbook section. Show their fixed Guild tier,<br>deliberately high Mana cost, and a clear daily status<br>indicating whether the hero has already used an<br>Adventure Spell today. Wisdom discounts do not apply.|
|Mage Guild Adventure Spell unlocks|The Mage Guild interface shows the fixed Adventure<br>Spell associated with each built Guild level,<br>unlocked/locked state, exact Gold/resource purchase<br>cost, and an unlock button. This is a fixed Adventure<br>Spell purchase; ordinary combat Spells are never<br>rerolled or researched. State is stored per town.|
|Adventure-map targeting|Summon Boat highlights legal adjacent water<br>destinations. Town Portal displays the determined<br>nearest controlled town rather than opening a free<br>destination picker. Water Walk and Fly pathing must<br>show their special movement cost and blocked<br>protected barriers. Dimension Door highlights only<br>visible legal tiles within its current range and warns that<br>casting ends movement.|



### Hero development, army management, and Skillspecific UI

|**Area**|**Required UI behavior**|
|---|---|
|Level-up choices|Present up to four choices: up to two Skill-rank choices<br>and up to two perk choices. Each card shows current -><br>new rank or perk, prerequisite Skill rank, associated<br>Skill, and full mechanical effect. Do not use a wheel.<br>Only legal choices are selectable; unavailable states<br>must explain why.|
|Perk detail on Hero screen|Use the existing Hero screen to show acquired,<br>available, and locked perks, with one empty visual slot<br>for each unfilled Basic, Advanced, or Expert tier and an<br>explanation of every lock. Left-clicking a Skill opens a<br>read-only browser of all ten perks grouped by tier with<br>names and icons, visibly distinguishing learned and<br>unlearned entries; right-clicking any entry, including an<br>unlearned one, shows the ordinary perk explanation<br>with the owning Skill's name and icon. Browsing never<br>acquires a perk. School Skill entries explicitly state the<br>maximum spell level currently unlocked for acquisition.|
|Leadership capacity|Beside the hero's current Leadership total, show +x for<br>the class's per-level Leadership growth; artifacts and<br>temporary bonuses affect the total but not this growth<br>annotation. Creature info shows Leadership per<br>creature, total stack Leadership, and current stack size /<br>maximum commandable count. Recruitment and army<br>exchange screens show the receiving hero's per-slot<br>capacity, additional Leadership required by the<br>proposed stack, resulting legal maximum stack size,<br>and a precise explanation when a transfer or<br>recruitment would exceed capacity.|
|Core / Elite / Champion|Town recruitment presents the complete authored<br>roster simultaneously in horizontal Core, Elite, and<br>Champion bands and adapts each row without<br>dropping, merging, or duplicating creatures to force a<br>universal 3 / 3 / 1 layout. Each card shows name,<br>portrait, available count, weekly growth, Attack,<br>Defense, Damage, Health, Speed, Initiative, Leadership<br>Cost, and Growth with the creature UI's statistic icons;<br>dwelling previews and dwelling names do not appear.<br>Increase the window and cards as needed for readable<br>rows, while retaining the town's Heroes III identity,<br>resource bar, date, and confirmation control. Core,<br>Elite, and Champion headings use the same yellow font<br>in every town and fallback Fort screen. Correct Conflux-<br>specific crowding and clipped previews without<br>redesigning working town layouts. External dwellings<br>also present the category and any tier-sensitive Skill<br>effects such as Recruitment Muster or faction<br>mechanics.|



|**Area**|**Required UI behavior**|
|---|---|
|Recruitment Skill - Muster|When a hero with Recruitment is in an eligible<br>town/dwelling, expose a Muster action with weekly-use<br>status, exact number and tier of recruits to be added,<br>legal destination dwelling(s), and settlement-level once-<br>per-week protection. Perks that split Muster or allow a<br>second use need explicit destination selection.|
|Field Instructor / training|Field Instructor is automatic. Qualifying Core<br>and Elite creatures should show whether they<br>have completed their first combat under this<br>hero and whether the resulting +1 Creature<br>Attack training bonus is active. No training<br>button or upgrade dialog is required.|
|Diplomacy|Neutral-stack interaction should be<br>deterministic: display relative Army Value<br>threshold, whether negotiation is available,<br>exact Gold required to join, and any<br>automatic Peacemaker or Tribute resolution<br>before the encounter is committed. Avoid<br>perk-specific action buttons and random-<br>looking presentation that invites save-<br>scumming expectations.|
|Estates perk feedback|Estates perks are passive or automatic. Show<br>their current calculated contribution in<br>treasury, Marketplace, mine, and town<br>tooltips where relevant: Investor's weekly<br>income scaling, Resource Broker's<br>exchange-rate modifier, Prospector's next<br>automatic mine trigger, and Magnate's<br>current beneficiary. Do not add perk-specific<br>economic action buttons.|
|War Machines / Siege|Direct-control UI must support Ballista target<br>selection, Precision Bombardment selection<br>of wall/gate/tower sections, First Aid Tent<br>targeting of troops or repairable<br>machines/fortifications when allowed, and<br>manual defensive-tower targets under<br>Fortification Engineer. Quartermaster's extra<br>activation, Surgeon's affliction priority,<br>Breachmaker overflow, and Battlefield Medic<br>casualty restoration resolve automatically and<br>need status/preview feedback rather than<br>activation controls.|
|Battlecraft states|Wait and Defend should display Battlecraft-derived<br>temporary bonuses. Overwatch needs a ready marker<br>and reaction range; pre-emptive effects need readable<br>status so the player understands why an attack was<br>interrupted.|



|**Area**|**Required UI behavior**|
|---|---|
|Warcasting|Show a small hero status identifying whether the next<br>eligible alternating action is carrying Spell->Order or<br>Order->Spell Warcasting empowerment, its percentage,<br>and expiry. This is a status indicator, not an action<br>currency.|
|Faction Skill states|Faction-Skill combat statuses use a generic<br>provider-driven presentation: icon, localized<br>label, current and maximum value where<br>applicable, and explanatory tooltip. Hide<br>absent entries and do not reserve a<br>hardcoded Metamagic field. Metamagic<br>provides its remaining used-sequence<br>allowance through this system; another<br>Faction Skill may provide a different resource<br>or state without inheriting Metamagic rules.<br>Divine Mandate needs contextual follow-up<br>status; Demonic Gating needs a reserve<br>panel and legal tier filters; Bloodrage needs<br>current bonus/cap display; Bulwark of the<br>Mire needs a clear Defend-enhancement<br>state; Necromancy needs a deterministic<br>post-battle raising/conversion summary;<br>Elemental Rebirth needs summon/result<br>feedback; Shroud of Malassa and Sylvan<br>Luck need their special movement/flanking or<br>Luck effects surfaced in tooltips/statuses.|
|Morale and Luck|All displays must support the -10 to +10 range, show<br>current value and sources, and later show exact trigger<br>chances once the probability curve is finalized. Effects<br>that temporarily treat negative Morale as 0 should show<br>both the real value and the effective value.|
|Help and tooltips|Every redesigned term - Primary Attribute, Secondary<br>Attribute, Skill, perk, Leadership, Siege, Hero Action,<br>Creature Activation, Order, School proficiency, Mass<br>variant, Adventure Spell, Movement Points, native<br>terrain, and Astrology Week - should have consistent<br>glossary/help text where first encountered.|
|Adventure-map Movement|Hero/adventure UI shows current and maximum<br>Movement Points, with a tooltip breakdown of the 200-<br>point base, Logistics and other bonuses, road<br>reduction, terrain multiplier, native-terrain qualification,<br>and special Water Walk/Fly costs. Army creature Speed<br>must never appear as a Movement modifier. Path<br>previews use the final integer tile costs.|
|Unique-building state|<br>Town UI and tooltips show per-building hero-training<br>state where relevant (for example Brotherhood of the<br>Sword, Hall of Valhalla, Order of Fire, Astral Nexus,<br>Battle Scholar Academy), weekly availability for Arcane<br>Reservoir and similar buildings, Astronomy Tower's<br>next-Week preview, and area effects such as Glyphs of<br>Fear.|



### UI priority for Version 1.0

|**Priority**|**Work**|
|---|---|
|P0 - combat interaction|Hero Action state; complete Orders<br>panel/targeting/state feedback; initiative integration;<br>School-rank acquisition feedback without known-spell<br>casting locks; Wisdom cost display; Mass variants;<br>Magic Arrow Overcharge; special spell targeting and<br>previews.|
|P1 - hero development|Level-up four-choice presentation and integration of all<br>Skill/perk/faction-skill states into the existing Hero<br>screen.|
|P1 - army and recruitment|Leadership-capacity feedback, Core/Elite/Champion<br>recruitment presentation, Recruitment Muster/training,<br>Diplomacy negotiation, War Machines controls.|
|P1 - adventure magic|Fixed Mage Guild Adventure Spell unlock purchases;<br>neutral Adventure Spellbook section; one-cast-per-day<br>state; Town Portal nearest-town feedback; Water Walk /<br>Fly pathing; Summon Boat and Dimension Door legal-<br>target previews.|
|P2 - strategic perk feedback|Estates, Diplomacy, Learning, and other<br>lower-frequency strategic perks need<br>passive-state, automatic-trigger, and outcome<br>feedback after the core combat/hero loop is<br>stable. No perk-specific activation dialogs<br>should be introduced.|
|P1 - town screens and movement|Five new Mage Guild town-screen upgrade visuals<br>(Castle V; Stronghold IV/V; Fortress IV/V), construction<br>UI assets and hitboxes; Astronomy Tower / Arcane<br>Reservoir / Astral Nexus state feedback; Olden Era-<br>style Movement display and path-cost breakdown.|



### Future UI - not a Version 1.0 dependency

Governors, regional administration, caravan routes, interception, and anti-chaining logistics remain future systems. When their mechanics are settled, the preferred starting point remains a compact Governor panel in the town screen plus region/route overlays on the adventure map. Do not build controls that imply unsettled Governor or Caravan rules merely to fill UI space.
