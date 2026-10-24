# Skills and perks

These are the **current development rules**, including changes after the published Early Access releases. Older downloads and saved games can differ. Numbers remain provisional while integration and playtesting continue.

For the wider comparison with Heroes III Complete, see the [gameplay changelog](NEW_HORIZONS_GAMEPLAY_CHANGELOG.md). Spell descriptions and formulas belong in the [spell guide](NEW_HORIZONS_SPELL_GUIDE.md).

Jump to [Martial Skills](#martial-skills) · [Magic Skills](#magic-skills) · [Adventure and fortune Skills](#adventure-and-fortune-skills) · [Faction Skills](#faction-skills).

## Building a hero

A hero normally has room for **eight Skills**. Each Skill has Basic, Advanced and Expert ranks and **ten alternative perks: four Basic, four Advanced and two Expert**. You choose at most **one perk from each tier**, for three perks in a fully developed Skill. Choosing a perk permanently closes the other choices from that tier for that hero.

Ordinary progression alternates:

**Basic Skill → Basic perk → Advanced Skill → Advanced perk → Expert Skill → Expert perk.**

An external teacher or reward may advance a Skill rank when its rules permit it, but does not grant perks or skip their order. For example, a hero exceptionally taught Expert Offense without any Offense perks must still choose a Basic perk first, then Advanced, then Expert. Class, faction and other learning restrictions still apply.

A level-up offers **up to two Skill choices and two perk choices**. Choose **one** option overall. A Skill with an occupied perk tier cannot offer another perk until it reaches the next needed rank. An exhausted or otherwise illegal Skill does not occupy a perk slot merely to fill the screen.

### How perk offers are weighted

Ordinary Skill offers keep the full class preference weights. Perk offers soften those preferences: the most favored eligible parent Skill has at most **twice** the weight of the least favored one. The reference range comes from the class's complete positive Skill-weight table, not just the Skills currently eligible for perks. Learning another Skill or finishing one does not rescale the remaining Skills against each other.

The game first picks an eligible **parent Skill**, then picks one of that Skill's legal unlearned perks with equal chances. Having more available perks does not increase a Skill's chance to be picked. The second perk offer uses a different parent. With exactly two eligible parents, both appear when there are two offer slots; with only one eligible parent, only **one** perk is offered. No eligible parent means no perk offer.

A legitimately learned zero-weight Skill receives the minimum perk weight once it is eligible; this does not make a normally forbidden Skill available for ordinary acquisition. For a class whose positive weights range from 1 to 10, illustrative perk weights are:

| Ordinary Skill weight | Perk-parent weight |
| --- | --- |
| 1 | 1.000 |
| 2 | 1.232 |
| 5 | 1.623 |
| 10 | 2.000 |

These are relative weights, not fixed percentages of level-ups or guaranteed appearances.

### Skill-based attribute growth

Alongside normal class growth, these learned Skills each independently have a **10% / 20% / 30%** chance at Basic/Advanced/Expert to add one associated primary-attribute point at level-up:

| Skill | Additional attribute |
| --- | --- |
| Offense | +1 Attack |
| Archery | +1 Attack |
| Armorer | +1 Defense |
| Spellcraft | +1 Spell Power |
| Wisdom | +1 Knowledge |

Offense and Archery roll independently. Deep Knowledge adds 10 percentage points to Wisdom's chance. Hero Attack and Defense still do not become passive additions to every creature's Attack and Defense.

### Reading the tables

Rank values below are shown in **Basic / Advanced / Expert** order and are totals unless stated otherwise. A **percentage point** is added directly to a percentage: +10 points changes 20% to 30%, not 22%. A **Spell Power-based effect** is the part of a spell's damage, healing, protection or other numerical effect that scales with Spell Power; its fixed base is unchanged unless the perk says otherwise. Hero specialties and other named effects can modify these baselines.

**Army Value** is the game's relative army-strength rating, not creature headcount. **Total HP** means the entire stack's HP. Healing surviving creatures does not restore casualties unless the text says so. An extra creature activation is not an extra hero Spell or Order. An Order-only opportunity cannot be spent on a Spell.

## Martial Skills

### Offense

**Availability:** All heroes.

**Ranks:** Melee attacks and retaliations deal **+10% / +20% / +30% physical damage**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Shock Assault** | Attacks benefiting from Charge! ignore 25% of the target's Creature Defense. |
| Basic | **Encirclement** | Flank! gains +7% damage per additional distinct attack side instead of +4%. |
| Basic | **Pursuit** | After destroying an enemy stack in melee, the attacker's creature activation remains open for movement using its unused movement. It gains no additional attack. |
| Basic | **Executioner** | Melee attacks deal +20% damage against stacks below 40% of maximum HP. |
| Advanced | **Armor Piercer** | All melee attacks ignore 20% of the target's Creature Defense. |
| Advanced | **Breakthrough** | Melee attacks ignore 50% of the damage reduction from Defend and other non-magical defensive states. |
| Advanced | **Cleave** | After a melee kill, automatically hit the adjacent enemy stack with the most total current HP for 50% normal damage. Once per activation; cannot trigger another Cleave. Ties use fixed battlefield hex order. |
| Advanced | **Vengeance** | While Riposte! is active, each affected stack gains one additional retaliation for the round. |
| Expert | **Relentless Assault** | Consecutive activations attacking the same enemy stack gain +10% damage, stacking to +30%. Attacking another target resets the bonus. |
| Expert | **No Quarter** | If a melee attack leaves an enemy below 25% maximum HP, it loses all remaining retaliations for the round and suffers -2 Morale until the end of its next activation. |

### Armorer

**Availability:** All heroes.

**Ranks:** Physical creature attacks and retaliations against your army deal **5% / 10% / 15% less damage**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Shield Master** | Protect! may intercept the first two qualifying melee attacks against the Ward each round instead of only the first. |
| Basic | **Iron Discipline** | Hold the Line! also reduces magical damage by half of its current physical damage-reduction value. |
| Basic | **Countercharge** | The pre-emptive attack granted by Brace! deals an additional +25 percentage points of normal damage, up to 100%. |
| Basic | **Pavise** | When a friendly stack Defends, ranged physical creature damage against it is reduced by an additional 25%. |
| Advanced | **Formation Fighting** | A stack adjacent to at least one friendly stack cannot be flanked and receives an additional 10% physical damage reduction. |
| Advanced | **Unyielding** | Friendly stacks that are Defending or affected by Hold the Line! cannot be forcibly displaced by non-magical effects. |
| Advanced | **Veteran** | At the beginning of its activation, a stack restores 15% of physical creature damage suffered since its previous activation, limited to surviving creatures. |
| Advanced | **Defiant** | The first non-magical enemy effect each round that would prevent a friendly stack from retaliating is ignored. |
| Expert | **Last Stand** | Once per combat, the first friendly stack that would die to a physical creature attack instead survives with one creature at 1 HP and immediately Defends. Does not protect clones or Phantom Integrity. If the attacker survives a lethal retaliation this way, its activation ends. |
| Expert | **Bastion** | While Defending or under Hold the Line!, the first physical creature attack received each round deals 30% less final damage. |

### Archery

**Availability:** All heroes.

**Ranks:** Ranged creature attacks deal **+10% / +20% / +30% physical damage**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Target Caller** | While Focus Fire! is active, friendly shooters gain an additional +5 percentage points of damage against the designated target and ignore all obstacle penalties. |
| Basic | **Skirmisher** | A shooter may move up to half its Speed and then make a ranged attack at 75% normal damage during the same activation. |
| Basic | **Point-Blank Shot** | Shooters may use their ranged attack against adjacent enemies without the ordinary adjacent-target ranged penalty. |
| Basic | **Counterfire** | Once per round, after a shooter suffers ranged creature damage, it automatically makes a ranged attack against the attacker at 50% normal damage if the attacker is a legal target. |
| Advanced | **Armor-Piercing Shot** | Ranged attacks ignore 20% of the target's Creature Defense. |
| Advanced | **Suppression** | The first ranged hit made by a stack during its activation reduces the target's Speed by 1 until the target's next activation. |
| Advanced | **High Arc** | Ranged attacks ignore obstacle penalties and halve ordinary range penalties. |
| Advanced | **Crossfire** | A ranged attack deals +15% damage if another friendly shooter has already damaged the same target this round. |
| Expert | **Deadeye** | The first ranged attack made by each friendly shooter each round rolls maximum creature damage and ignores 25% Creature Defense. |
| Expert | **Rain of Arrows** | After a ranged attack, the enemy adjacent to the primary target with the most total current HP takes 35% of the damage actually dealt to the primary target. Once per activation; no adjacent enemy means no extra hit. Ties use fixed battlefield hex order. |

### Battlecraft

**Availability:** All heroes.

**Ranks:** A stack that Waits gains **+5% / +10% / +15% physical damage** on its next attack or retaliation before round end. A stack that Defends gains an extra **5% / 10% / 15% physical creature-damage reduction** until its next activation.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Tactics** | The army's deployment area extends two additional battlefield rows forward, subject to scenario and siege restrictions. |
| Basic | **Reserve** | When a stack takes its delayed activation after Waiting, it gains +2 Speed for that activation. |
| Basic | **Entrench** | The additional physical damage reduction granted by Battlecraft when Defending increases by +5 percentage points. |
| Basic | **Overwatch** | A shooter that Waits automatically makes one 50%-damage ranged reaction against the first enemy that voluntarily moves into its range before the delayed activation, if that enemy is a legal target. Once per round. |
| Advanced | **Pre-emptive Strike** | The first melee attack each round against a Defending friendly stack triggers a 50%-damage pre-emptive attack. This does not consume normal retaliation. |
| Advanced | **Redeployment** | After both armies deploy, make one final relocation of a friendly stack within your legal deployment area, before the first creature acts. Uses the normal deployment controls. |
| Advanced | **Passing Lines** | Friendly stacks may move through hexes occupied by friendly stacks, provided they end movement in a legal empty position. |
| Advanced | **Rapid Response** | Once per round after an enemy creature activation ends, if friendly stacks are still Waiting, the waiting friendly stack scheduled latest in the current initiative order automatically takes its delayed activation next. |
| Expert | **Grand Tactics** | Complete initial deployment after the opponent. If both heroes have Grand Tactics, keep the usual attacker-then-defender order. Does not change Redeployment or bypass scenario restrictions. Without Tactics, the ordinary one-row deployment area remains. |
| Expert | **Battlefield Mastery** | Each round, the first eligible ordinary friendly stack to Wait or Defend receives double the Skill-rank bonus for that action. Other perk bonuses are not doubled; an ineligible war machine does not use this opportunity. |

### War Machines

**Availability:** All heroes.

**Ranks:** Gain **+20 / +40 / +60 Siege** in total. Basic rank also grants direct control of your war machines.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Master Gunner** | The Ballista fires twice during each activation. The second shot deals 60% normal damage and may target a different enemy. |
| Basic | **Precision Bombardment** | The Catapult may target a specific wall section, gate, or defensive tower. An accuracy miss deals no damage instead of redirecting to another part; remaining shots stop once the selected part is destroyed. Accuracy and damage-quality rolls are unchanged. |
| Basic | **Surgeon** | First Aid Tent healing also removes one physical affliction. Removal priority is Poison, then Disease, then Bleeding, then other eligible physical afflictions in order of application. |
| Basic | **Quartermaster** | Once per combat, while the Ammo Cart survives, the first allied war machine other than the Ammo Cart to complete an activation immediately receives one additional activation at 50% effectiveness. |
| Advanced | **Piercing Bolts** | Ballista attacks ignore 50% of the target's Creature Defense. |
| Advanced | **Breachmaker** | When your controlled Catapult destroys a wall or gate section, half the excess final damage, rounded down, hits the surviving adjacent outer-wall section with the least HP. Ties use fixed fortification order. This overflow happens once and is not multiplied by Siege again. |
| Advanced | **Battlefield Medic** | After the First Aid Tent heals surviving creatures in its target stack, up to 50% of the Tent's calculated healing is automatically applied to restore casualties. The stack may not exceed its battle-start count. |
| Advanced | **Field Workshop** | The First Aid Tent may repair surviving damaged allied war machines or friendly defended-town fortifications using its full normal Siege-scaled healing value, capped by missing HP. It does not restore fallen machines or rebuild destroyed structures. Normal troop healing remains unchanged. |
| Expert | **Fortification Engineer** | When defending a fortified town, defensive towers use 125% of the hero's Siege rating and may be manually targeted. |
| Expert | **Counter-Battery** | Ballistae and defensive towers may target enemy war machines and deal +50% final damage against them. |

### Discipline

**Availability:** All heroes.

**Ranks:** Your army gains **+1 / +2 / +3 Morale** in total.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Inspirational Leader** | When positive Morale triggers for a friendly stack, that stack deals +10% damage until the end of its activation. |
| Basic | **Steadfast** | Morale penalties applied by enemy effects are reduced by 1, to a minimum penalty of 0. |
| Basic | **Rally** | Once per combat, the first negative Morale trigger affecting a friendly stack is automatically canceled. |
| Basic | **Esprit de Corps** | Army-composition Morale penalties are reduced by 1. |
| Advanced | **Standard Bearer** | A friendly stack adjacent to at least one other friendly stack receives +1 Morale. This bonus does not stack with itself. |
| Advanced | **Fearless** | Friendly stacks are immune to non-magical fear effects. |
| Advanced | **Veteran Cohesion** | The first time a friendly stack falls below 50% of its maximum HP during combat, it gains +2 Morale for the rest of that combat. |
| Advanced | **Hold Fast** | Friendly stacks that Defend or are affected by Hold the Line! treat negative Morale as 0 until their next activation. |
| Expert | **Unbreakable** | The first negative Morale trigger against the army each round is ignored. |
| Expert | **Heroic Spirit** | After an ordinary Order, positive hero Morale may grant one additional Order-only Hero Action. Bonus Orders cannot trigger further Morale. Current chance: 3% per positive Morale point, up to 30%. |

### Recruitment

**Availability:** All heroes.

**Ranks:** Muster once per week in a friendly town. **Basic:** add 2 Core recruits to one Core dwelling. **Advanced:** 4 Core or 1 Elite. **Expert:** 6 Core, 2 Elite or 1 Champion. Choose one matching dwelling pool; this adds availability, not free troops.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Volunteer Network** | When Muster targets a Core dwelling, add 2 additional Core recruits. |
| Basic | **External Recruiter** | The hero may spend the weekly Muster use at an owned external Core dwelling instead of a town, adding 2 Core recruits there. |
| Basic | **Drill Sergeant** | Core and Elite creatures recruited directly by this hero gain +1 Morale in their first actual combat within 7 days, counting recruitment day. That combat uses the benefit even if immunity or a cap prevents the bonus. |
| Basic | **Broad Muster** | When Muster targets Core creatures, its generated recruits may be split between two Core dwellings in the same town. |
| Advanced | **Elite Draft** | When Muster targets an Elite dwelling, add 1 additional Elite recruit. |
| Advanced | **Field Instructor** | Original Core and Elite recruits that survive their first combat under their recruiter gain +1 Creature Attack while continuously remaining in that hero's army. Surviving retreat and surrender armies qualify. |
| Advanced | **Recruiter's Contacts** | Once per week, visit an external dwelling you already owned before arriving: replenish its first empty recruitment pool that has positive weekly growth, using normal growth modifiers. Base and upgraded forms share one pool. Capturing the dwelling on that visit, zero growth, or a failed replenishment does not spend the use. |
| Advanced | **Reinforcement Drill** | Once per hero per week, the newly recruited eligible stack in the lowest original army slot gains +2 Initiative in round 1 of combat. Champions qualify. Entering combat spends the use. |
| Expert | **Champion's Call** | When Expert Muster targets a Champion dwelling, add 2 Champions instead of 1. |
| Expert | **Master Recruiter** | The hero may use Muster twice per week, but never twice in the same town or external dwelling. A town or dwelling may still benefit from Muster only once per week. |

### Command

**Availability:** Might-exclusive.

**Ranks:** The attribute-based portions of your Orders operate at **110% / 120% / 130% efficiency**. Their fixed base portions are unchanged.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Aggressive Commander** | Attack-derived components of Orders receive an additional +20 percentage points of Command Efficiency. |
| Basic | **Defensive Commander** | Defense-derived components of Orders receive an additional +20 percentage points of Command Efficiency. |
| Basic | **Iron Will** | Friendly stacks affected by an Order that normally expires at the end of the round retain that Order through their next creature activation. |
| Basic | **Battle Plan** | Once per combat, immediately before the first creature activation of round 1, issue one Order without consuming the hero's Hero Action for that round. |
| Advanced | **Veteran Commander** | Leadership-derived components of Orders receive an additional +25 percentage points of Command Efficiency. |
| Advanced | **Combined Arms** | Focus Fire! grants friendly melee attacks 50% of its damage bonus against the designated target. Flank! grants friendly ranged attacks 50% of its Attack-derived damage bonus; shooters do not add Flank sides. |
| Advanced | **Commanding Presence** | Stacks currently benefiting from your Order treat negative Morale as 0. The floor ends as soon as that stack's benefit is consumed or broken—for example, after Charge is used, Protect breaks or runs out, or Second Wind's extra activation ends. |
| Advanced | **Crisis Command** | Once per combat, when a friendly stack is completely destroyed, immediately issue one Order without consuming the Hero Action. |
| Expert | **Double Command** | The first time each combat the hero uses the Hero Action to issue an Order, immediately issue one additional different Order. The same Order cannot be issued twice in that sequence. |
| Expert | **Seize Initiative** | The first time each combat the hero uses the Hero Action to issue an Order, the friendly stack scheduled latest in the current initiative order among those that have not completed their normal creature activation becomes the next friendly stack to activate after the current activation ends. |

## Magic Skills

For all six magic-school Skills, **Basic permits ordinary learning of Level 3 spells, Advanced Level 4, and Expert Level 5**. Their school-scaled Spell Power portions use **115% / 130% / 145%** of the normal coefficient where that spell supports school-rank scaling. This is not a blanket increase to every part of every spell. Already inscribed eligible combat spells and combat scrolls can be cast without the corresponding School Skill; normal learning requirements remain.

### Light Magic

**Availability:** All heroes.

**Ranks:** follow the shared magic-school progression above.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Healer** | Cure and Guardian Spirit receive +20% to their Spell Power-based healing or temporary HP. |
| Basic | **Benediction** | Bless lasts 1 additional round. |
| Basic | **Sanctuary Keeper** | A friendly stack protected by Sanctuary also gains +2 Morale while Sanctuary remains active. |
| Basic | **Guardian** | Guardian Spirit creates 25% more temporary HP. |
| Advanced | **Aegis** | Holy Armor and Heavenly Gale receive +20% to their Spell Power-based protection. |
| Advanced | **Litany** | Grants Mass Bless. |
| Advanced | **Purifier** | Purify also removes one physical affliction from each affected friendly stack. |
| Advanced | **Retributionist** | Divine Retribution deals +20% reactive damage. |
| Expert | **Miracle Worker** | Resurrection restores 25% more casualties than its normal calculated amount. |
| Expert | **Crusader** | Crusade! lasts 1 additional round and affected creatures cannot suffer negative Morale while it remains active. |

### Shadow Magic

**Availability:** All heroes.

**Ranks:** follow the shared magic-school progression above.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Malediction** | Curse and Sorrow last 1 additional round. |
| Basic | **Blood Drinker** | Life Drain heals the friendly target for 75% of actual damage dealt instead of 60%. |
| Basic | **Painweaver** | Hex of Pain receives +20% to its Spell Power-based damage. |
| Basic | **Withering Touch** | Frailty strips an additional 5 percentage points of base Creature Defense per cast; its cumulative cap is unchanged. |
| Advanced | **Plaguebearer** | Plague may propagate one additional time beyond its normal limit. |
| Advanced | **Soul Binder** | Soul Chain echoes an additional 15 percentage points of secondary-victim damage into the primary target. |
| Advanced | **Dark Gift** | Shadow Gift requires 25% less sacrificed HP for the same offensive effect. |
| Advanced | **Night Feeder** | Vampirism returns an additional 15 percentage points of damage as healing. |
| Expert | **Reanimator** | Re-animate restores 25% more temporary casualties. |
| Expert | **Grand Malediction** | Grants Mass Curse and Mass Sorrow. |

### Nature Magic

**Availability:** All heroes.

**Ranks:** follow the shared magic-school progression above.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Herbalist** | Regeneration marks an additional 10 percentage points of eligible damage as Regenerable, subject to its normal cap. |
| Basic | **Rootcaller** | Entangle lasts 1 additional round, to a maximum of 3 rounds. |
| Basic | **Venomancer** | Poison's Base Poison value is increased by 20%. |
| Basic | **Beastcaller** | Summon Trolls creates 25% more Troll HP. |
| Advanced | **Verdant Warden** | Dendroids created by Verdant Prison receive 25% more HP. |
| Advanced | **Geomancer** | Earthquake deals 25% more structural damage and Fractured Ground lasts 1 additional round. |
| Advanced | **Mire Shaper** | Quicksand creates one additional patch. |
| Advanced | **Verdant Communion** | Grants Mass Regeneration. |
| Expert | **Worldroot** | Nature's Wrath may chain through two additional valid stacks and its Spell Power-based effect is increased by 10%. |
| Expert | **Elemental Conjurer** | Elemental Convergence creates 30% more Elemental HP. Elementals summoned by the spell also gain +2 Initiative during their first round on the battlefield. |

### Havoc Magic

**Availability:** All heroes.

**Ranks:** follow the shared magic-school progression above.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Pyromancer** | Fireball, Fire Wall, and Inferno receive +15% to their Spell Power-based damage. |
| Basic | **Cryomancer** | Ice Bolt and Frost Ring receive +20% to their Spell Power-based damage. |
| Basic | **Stormcaller** | Lightning Bolt and Chain Lightning receive +15% to their Spell Power-based damage. |
| Basic | **Demolitionist** | Havoc spells deal +50% damage to destructible obstacles and fortifications when they can affect them. |
| Advanced | **Mine Layer** | Land Mine creates one additional mine. |
| Advanced | **Conductor** | Chain Lightning jump multipliers become 100% / 75% / 55% / 40% / 30%. |
| Advanced | **Meteorologist** | Meteor Shower deals +25% damage to battlefield objects and fortifications inside its impact area. |
| Advanced | **Controlled Blast** | When Fireball, Inferno, or Meteor Shower is centered on a hex occupied by a friendly stack, that stack is excluded from the spell's damage. Frost Ring already has a safe center. Armageddon remains explicitly indiscriminate. |
| Expert | **Cataclysm** | Armageddon receives +20% to its Spell Power-based damage and destroys ordinary magical obstacles as well as physical ones. |
| Expert | **Annihilator** | Disintegrate ignores 20% of the target's Magical Damage Reduction; casualties it destroys remain unusable remains as normal. |

### Sorcery Magic

**Availability:** All heroes.

**Ranks:** follow the shared magic-school progression above.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Overcharger** | Magic Arrow gains +1 maximum Overcharge and each Overcharge point increases base damage by 17.5% instead of 15%. |
| Basic | **Selective Dispel** | Dispel automatically removes only hostile temporary magical effects from a friendly target and only beneficial temporary magical effects from an enemy target. |
| Basic | **Temporalist** | Slow lasts 1 additional round. |
| Basic | **Matter Shaper** | Transfigure Matter creates 25% more Diamond Golem HP. |
| Advanced | **Teleporter** | A stack moved by Teleport gains +2 Speed during its next creature activation. |
| Advanced | **Arcane Ballistics** | When an enemy stack has 3 Arcane Breach marks from Focus Magic, friendly ranged creature attacks against it also ignore 25% of its Physical Damage Reduction. |
| Advanced | **Illusionist** | Phantom Army receives 25% more phantom integrity. |
| Advanced | **Temporal Field** | Grants Mass Slow. |
| Expert | **Chronomancer** | Time Stop's maximum radius increases by 1. |
| Expert | **Spellbinder** | Spell Lock's ordinary duration lasts 1 additional round, to a maximum of 4 rounds. Echoed Duration is then applied separately to an eligible Metamagic cast, so both perks together may produce a 5-round Spell Lock. |

### Chaos Magic

**Availability:** All heroes.

**Ranks:** follow the shared magic-school progression above.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Misfortune Weaver** | Misfortune reduces favorable random-effect probability by an additional 10 percentage points of the normal probability multiplier, respecting its normal minimum. |
| Basic | **Blinkmaster** | Blink draws two legal random destinations and uses the one farther from the original hex. Ties use fixed battlefield hex order. |
| Basic | **Confounder** | Confusion avoids repeating the target's previous resolved behavior whenever a different legal behavior exists. If only one behavior is legal, repetition is allowed. A trapped Wander result becomes Defend. |
| Basic | **Frenzied Curse** | A Berserked creature gains +2 Speed during its forced activation. |
| Advanced | **Mindbreaker** | Forgetfulness also suppresses the target's passive offensive creature abilities for its duration. |
| Advanced | **Shapeshifter** | Polymorph draws two random same-tier forms; the same form may be drawn twice. It chooses the lower-value resulting stack, accounting for the number of creatures produced by HP conversion. Ties follow the fixed creature order. |
| Advanced | **Fate Dealer** | Hand of Fate draws two legal random spill targets; repeats are possible. If exactly one drawn target is an enemy, it takes the spill damage. Otherwise a fair coin chooses between the draws. |
| Advanced | **Reality Breaker** | Reality Warp may target any two legal stacks rather than requiring one friendly and one enemy stack. |
| Expert | **Pandemonium Master** | Pandemonium deals +25% damage per counted debuff. |
| Expert | **Paradox Shield** | Shield of Chaos gains +10 percentage points to both damage resistances, without changing the -10 Morale and -10 Luck penalties. |

### Spellcraft

**Availability:** All heroes.

**Ranks:** The numerical parts of spells determined by Spell Power operate at **110% / 120% / 130%** of their normal value. Fixed portions are unchanged.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Cross-School Formula** | Your next combat spell this round or next gains +10% to its Spell Power-based effect if it shares no school with your previous combat spell. Both spells must have a school. Always compare with your most recent cast, including a countered or resisted spell. |
| Basic | **Spell Penetration** | Hostile spells ignore 20% of the target's Magical Damage Reduction. |
| Basic | **Concentration** | A combat spell aimed at exactly one unique stack gains +15% to its Spell Power-based effect. Count intended recipients before resistance or random extra-target selection, not just stacks ultimately affected. |
| Basic | **Arcane Focus** | The first spell cast in each combat receives +20% to its Spell Power-based effect. |
| Advanced | **Extend Spell** | Once per round, your first combat spell that creates a temporary effect measured in rounds lasts 1 additional round. Permanent, combat-long and action-bound effects do not qualify. |
| Advanced | **Precise Casting** | When a conventional area spell with a selected central hex is centered on a hex occupied by a friendly stack, that stack is excluded from the spell's effect. Spells explicitly defined as indiscriminate, such as Armageddon, cannot benefit. |
| Advanced | **Empower Spell** | Spells whose final Wisdom-adjusted Mana cost is 12 or higher receive +25% to their Spell Power-based effect. |
| Advanced | **Counterpressure** | After an enemy hero casts a spell that affects your army, your next spell before the end of the next round receives +20% to its Spell Power-based part. |
| Expert | **Grand Formula** | The first Level 4 or Level 5 spell cast in each combat uses 150% of its normal Spell Power-based effect before other multipliers. |
| Expert | **Overwhelming Formula** | The first hostile spell each combat that deals magical damage to one or more stacks with Magical Damage Reduction ignores 50% of each affected target's current Magical Damage Reduction. |

### Wisdom

**Availability:** Magic-exclusive.

**Ranks:** Listed spell Mana costs fall by **10% / 20% / 30%**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Intelligence** | Maximum Normal Spell Points equal floor(1.30 × effective Knowledge). Intelligence is the sole general multiplicative capacity effect. |
| Basic | **Mysticism** | At the start of each day, recover Mana equal to the greater of 5 or 10% of maximum Mana. |
| Basic | **Arcane Memory** | After completing a cast using a scroll, permanently learn the spell if you meet its normal learning and School requirements. This does not bypass paid Adventure Guild unlocks. A cast from your book or Tome takes priority and does not count as a scroll cast; merely carrying a scroll teaches nothing. Reusable scrolls take priority over charged artifacts and remain reusable. |
| Basic | **Prepared Caster** | After Wisdom calculates the spell's percentage discount, the first spell cast in each combat costs 2 additional Mana less, to a minimum of 1. |
| Advanced | **Sage** | On your first visit to a town with a built Mage Guild, reveal one eligible hidden spell from that Guild's school list and built levels: choose the highest level, then spellbook order for ties. Everyone can see the extra spell in a separate row. A first visit without this perk or a spellbook still uses the opportunity; visits before any Guild exists do not. |
| Advanced | **Meditation** | If the hero ends the day with at least 25% of maximum Movement unspent, recover an additional 15% of maximum Mana. |
| Advanced | **Mana Conservation** | After combat, recover 20% of the Mana spent during that combat, up to 20 Mana. |
| Advanced | **Deep Knowledge** | Wisdom's chance to grant +1 Knowledge at level-up increases by 10 percentage points. |
| Expert | **Archmage** | After Wisdom calculates its percentage discount, the first Level 4 or Level 5 spell cast in each combat costs 3 additional Mana less, to a minimum of 1. |
| Expert | **Arcane Reservoir** | Maximum Mana increases by a flat 25 in addition to Knowledge and other modifiers. |

### Warcasting

**Availability:** All heroes.

**Ranks:** Alternate your ordinary Hero Actions. After a Spell, your next Order this round or next gains **+10 / +20 / +30 percentage points** of efficiency on its attribute-based portions. After an Order, your next Spell within that window gains **+10% / +20% / +30%** to its Spell Power-based effect. This improves the next action; it does not itself grant an extra action.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Martial Channeling** | The Spell-to-Order Warcasting bonus gains an additional +10 percentage points of efficiency. |
| Basic | **Arcane Channeling** | The Order-to-Spell Warcasting bonus gains an additional +10% to the spell's Spell Power-based part. |
| Basic | **Spellward** | Friendly stacks receive 10% less magical damage. This is the successor to the old Resistance secondary skill. |
| Basic | **Battle Meditation** | Once per round, after an Order-to-Spell Warcasting bonus is consumed, recover 3 Mana. |
| Advanced | **Enchanted Command** | A friendly stack affected by an Order empowered by Warcasting gains +1 Morale until its next activation. Protect grants this only to its Protector/Ward pair; Second Wind grants it only to its selected stack, not the entire army. |
| Advanced | **Combat Casting** | A hostile spell empowered by Warcasting ignores 15% of the target's Magical Damage Reduction. |
| Advanced | **Tactical Weaving** | An armed Warcasting bonus remains available for two rounds instead of expiring at the end of the next round. |
| Advanced | **Reactive Weave** | After an enemy hero casts a spell that affects your army, your next Order before the end of the next round receives half your normal Spell-to-Order Warcasting bonus. |
| Expert | **Perfect Rhythm** | If the hero's last three Hero Actions alternate Spell / Order / Spell or Order / Spell / Order, the third action receives double the normal Warcasting bonus. |
| Expert | **Master Synthesis** | The first Warcasting bonus consumed each combat is automatically treated as +50% / +50 percentage points instead of its normal value. |

## Adventure and fortune Skills

### Logistics

**Availability:** All heroes.

**Ranks:** Adventure Movement increases by **10% / 20% / 30%**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Pathfinding** | Difficult-terrain movement penalties are reduced by 50%. |
| Basic | **Navigation** | Sea Movement increases by 25%, and embark/disembark Movement costs are halved. |
| Basic | **Scouting** | Adventure-map sight radius increases by 5 hexes. |
| Basic | **Forced March** | Once per day after exhausting normal Movement, gain additional Movement equal to 10% of maximum Movement. The army begins its next combat that day with -1 Morale during round 1. |
| Advanced | **Roadmaster** | Movement cost while travelling on roads is reduced by an additional 25% relative to the normal road cost. |
| Advanced | **Mountaineer** | Rough and Subterranean terrain impose no additional movement penalty. Impassable Rock remains impassable; other terrain penalties are unchanged. |
| Advanced | **Rapid Embarkation** | Embarking or disembarking costs only 10% of maximum daily Movement. |
| Advanced | **Pursuit March** | After winning a combat, recover 10% of maximum daily Movement. Once per day. |
| Expert | **Wayfarer** | Passable terrain can never cost more than 125% of its base clear-terrain movement cost. |
| Expert | **Master Logistician** | Up to 15% of unused Movement at the end of a day carries over to the next day. |

### Diplomacy

**Availability:** All heroes.

**Ranks:** Eligible neutral stacks may offer to join if their Army Value is no more than **25% / 50% / 75%** of your current army's value. Joining normally costs their ordinary recruitment Gold; army-slot and Leadership limits still apply.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Negotiator** | Increase the Diplomacy joining threshold by 15 percentage points. |
| Basic | **Peacemaker** | The first eligible neutral stack encountered each week that would otherwise initiate combat instead allows the hero to pass without fighting and remains on the map. The hero may still deliberately attack it through the ordinary encounter interaction. |
| Basic | **Envoy** | From up to 5 hexes away, reveal whether a visible neutral stack is willing to negotiate and the gold required if it joins. |
| Basic | **Mercenary Captain** | Creatures recruited through Diplomacy gain +1 Morale for their first three combats under this hero. |
| Advanced | **Common Cause** | Neutral creatures of the hero's faction count as half their normal Army Value when checking the Diplomacy threshold. |
| Advanced | **Tribute** | Once per week, the first eligible neutral stack that refuses to join automatically accepts Gold equal to its normal recruitment value and leaves the map if the kingdom can pay that amount. If the treasury is insufficient, normal combat rules apply. |
| Advanced | **Loyal Mercenaries** | Creatures recruited through Diplomacy do not contribute to mixed-faction Morale penalties while commanded by this hero. |
| Advanced | **Recruitment Pact** | After successfully recruiting a neutral stack, the next neutral stack contacted within 7 days is treated as 15% lower Army Value for the joining threshold. |
| Expert | **Grand Diplomat** | Increase the Diplomacy joining threshold by a further 25 percentage points, to a maximum of 100% of the hero's Army Value. |
| Expert | **Legendary Reputation** | Once per calendar month, the first eligible neutral stack that qualifies to join through Diplomacy joins without a gold payment. |

### Estates

**Availability:** All heroes.

**Ranks:** While the hero is active, gain **125 / 250 / 500 Gold per day**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Tax Collector** | Gain +50 Gold per day for each owned town, capped at +500 Gold per day from this perk. |
| Basic | **Land Surveyor** | The first mine captured by this hero each week immediately produces three days of its normal output. |
| Basic | **Prospector** | The first owned mine visited by this hero each week immediately produces +2 of its common resource or +1 of its rare resource, according to the mine's normal output type. |
| Basic | **Merchant Prince** | While this hero is in a town, that town's Marketplace exchange rates behave as if the kingdom owned two additional Marketplaces. |
| Advanced | **Steward** | If the hero ends the day in an owned town, that town produces +250 Gold on the next day. |
| Advanced | **Investor** | At the start of each week, this hero's Estates daily income for that week increases by +50 Gold for each full 5,000 Gold currently in the treasury, capped at +250 Gold per day. |
| Advanced | **Resource Broker** | While this hero is in a town, Marketplace exchanges of Wood or Ore for rare resources in that town use a rate 20% more favorable than the ordinary Marketplace rate. |
| Advanced | **Estate Network** | At the start of each week, gain +1 Wood and +1 Ore for every three owned towns, rounded down, minimum 1 if at least one town is owned. |
| Expert | **Financier** | At the start of each week, gain 1% of the current Gold treasury as interest, capped at 1,000 Gold. |
| Expert | **Magnate** | At the start of each week, the most recently visited owned town from the previous week produces +500 Gold per day for the next 7 days. If no owned town was visited, the perk has no effect that week. |

### Learning

**Availability:** All heroes.

**Ranks:** Experience gained increases by **10% / 20% / 30%**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Scholar** | When allied heroes meet, each teaches the other the highest-level unknown spell the recipient can legally learn. Ties follow spellbook order. Once per hero pair per week. |
| Basic | **Eagle Eye** | After combat, automatically learn the highest-level Level 1-3 spell cast by the enemy hero during that combat that this hero does not know and could legally learn. If several qualify at the same highest level, learn the one cast earliest in the combat. |
| Basic | **Historian** | Adventure-map objects whose primary reward is Experience grant 50% more Experience. |
| Basic | **Mentor** | The first lower-level allied hero met each week automatically gains Experience equal to 250 times the mentor's level. |
| Advanced | **Academic Study** | The first time the hero visits each town, gain 250 Experience for each Mage Guild level already constructed in that town. |
| Advanced | **Quick Study** | At every fifth hero level, the initial level-up offer is automatically rerolled once before the choices are shown to the player. |
| Advanced | **Field Study** | Gain +25% additional Experience from defeating enemy heroes or wandering armies whose Army Value exceeded your army's at battle start. |
| Advanced | **Archivist** | When the hero acquires a spell scroll, permanently learn its spell immediately if legally eligible. |
| Expert | **Sage** | On your first visit to a town with a built Mage Guild, learn its highest-level eligible unknown spell, including hidden spells, after ordinary teaching and any Wisdom Sage reveal. Ties follow spellbook order. A first visit without this perk or a spellbook still uses the opportunity; visits before any Guild exists do not. |
| Expert | **Master Teacher** | Mentor automatically triggers for the first two different lower-level allied heroes met each week and grants Experience equal to 500 times the mentor's level to each. |

### Luck

**Availability:** All heroes.

**Ranks:** Your army gains **+1 / +2 / +3 Luck** in total.

Hero Luck checks the original action once. Mirrors and reflections follow that result rather than earning fresh rolls or Mana refunds; later damage from the same spell keeps its original result. Nonpositive hero Luck cannot trigger these hero perks. Heroic Spirit uses positive Morale instead, as described under Discipline.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Opportunist** | After an attack triggers positive Luck, the stack may move up to 2 hexes using its remaining movement. It receives no additional attack. |
| Basic | **Second Chance** | The first negative Luck trigger against the army each combat is ignored. |
| Basic | **Lucky Aim** | A ranged attack that triggers positive Luck ignores 25% of the target's Creature Defense. |
| Basic | **Fortune's Favor** | Positive Lucky Strike damage multipliers increase by +0.25x. |
| Advanced | **Serendipity** | If no friendly positive Luck trigger occurred during the previous round, the first friendly attack of the new round receives +2 Luck for that attack. |
| Advanced | **Lucky Day** | After issuing an Order, positive hero Luck may double the Order's own potency. Duration, targets, activation counts and extra effects from other perks are unchanged. Current chance: 4% per positive Luck point, up to 40%. |
| Advanced | **Erratic Mana** | After casting a paid hero spell, positive hero Luck may refund 50% of the Mana actually spent, rounded down, back to the same Mana pools. A resisted or otherwise negated spell still qualifies. Current chance: 4% per positive Luck point, up to 40%. |
| Advanced | **Chain of Fortune** | Once per round after a friendly positive Luck trigger, the next different friendly stack to attack gains +1 Luck for that attack. |
| Expert | **Druidic Luck** | Positive hero Luck may double a damaging spell's HP and structural damage, including later damage-over-time and zones from that cast. Does not double healing, creature counts or instant kills. Current chance: 4% per positive Luck point, up to 40%. |
| Expert | **Perfect Fortune** | The first eligible attack made by the hero's army each combat triggers positive Luck automatically. |

## Faction Skills

Each faction shares its Skill between its two hero classes. Academy uses the original Tower faction slot.

### Divine Mandate

**Availability:** Castle heroes only.

**Ranks:** An ordinary Hero Action used for a Light Spell can grant an Order-only follow-up; an ordinary Order can grant a Light-Spell-only follow-up. Use it before round end. Complete up to **1 / 2 / 3 pairs per combat**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Sacred Command** | An Order issued through Divine Mandate gains +10 percentage points of efficiency on its attribute-derived components. |
| Basic | **Consecrated Casting** | A Light Spell cast through Divine Mandate gains +10% to its Spell Power-based effect. |
| Basic | **Shared Purpose** | If the paired Light Spell and Order both affect the same friendly stack, that stack gains +1 Morale until its next activation. |
| Basic | **Chaplain's Reserve** | After the first Divine Mandate sequence each combat resolves, recover 3 Mana. |
| Advanced | **Divine Discipline** | An Order issued through Divine Mandate that would end this round remains on its original friendly recipients until each completes its first creature activation after the issuing round. Waiting or continuing the same activation does not spend this extension. |
| Advanced | **Purifying Mandate** | When a Light Spell cast through Divine Mandate removes a negative magical effect, it also removes one physical affliction from that target if present. |
| Advanced | **Knightly Sequence** | If the Order is performed first, the paired Light Spell costs 2 less Mana; if the Light Spell is performed first, the paired Order gains +5 additional percentage points of efficiency. |
| Advanced | **Royal Standard** | Friendly stacks affected by an Order issued through Divine Mandate treat negative Morale as 0 for that Order's duration. |
| Expert | **Mandate of Heaven** | The first Divine Mandate sequence each combat does not count against the Skill's normal per-combat usage limit. |
| Expert | **Crown and Altar** | The second action of a Divine Mandate pair gains +20% to its attribute-based effects on friendly stacks also affected by the first action. Works Spell→Order or Order→Spell. No extra targeting step; the first action is unchanged. |

### Sylvan Luck

**Availability:** Rampart heroes only.

**Ranks:** Gain **+1 / +2 / +3 Luck**. Creature Lucky Strike damage multipliers become **2.25× / 2.60× / 3.00×**.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Elven Precision** | Lucky ranged attacks ignore 25% Creature Defense. |
| Basic | **Forest's Favor** | The first positive Luck trigger for each friendly stack in a combat grants +2 Speed for the remainder of that activation. |
| Basic | **Serendipity** | Until a friendly stack has triggered positive Luck once in the current combat, it counts as having +1 additional Luck for trigger chance only. |
| Basic | **Lucky Recovery** | A melee Lucky Strike restores surviving creatures for 10% of actual damage dealt. |
| Advanced | **Shared Fortune** | After a friendly stack triggers positive Luck, adjacent friendly stacks gain +1 Luck until their next activation. |
| Advanced | **Nature's Providence** | The first negative Luck trigger against the army each round is ignored. |
| Advanced | **Fortunate Aim** | Friendly shooters gain +1 Luck when attacking the target designated by Focus Fire!. |
| Advanced | **Wild Chance** | Temporary creatures summoned by Nature spells inherit the hero's Sylvan Luck bonus and Lucky Strike multiplier. |
| Expert | **Perfect Moment** | The first eligible attack each combat made by a friendly stack with +5 or greater current Luck triggers positive Luck automatically. |
| Expert | **Cascading Fortune** | When a Lucky Strike destroys an enemy stack, the next friendly stack to act gains +3 Luck for its activation. |

### Metamagic

**Availability:** Academy (Tower) heroes only.

**Ranks:** After casting with your ordinary Hero Action, gain an optional extra Spell Action for this round while uses remain. Casting the extra Spell spends one of **1 / 2 / 3 uses per combat**; letting the opportunity expire does not. That extra Spell cannot trigger another Metamagic sequence.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Spell Sequencing** | A Spell cast with a Metamagic Spell Action gains +15% to its Spell Power-based effect if it belongs to a different school from the Spell that triggered Metamagic. |
| Basic | **Arcane Economy** | Spells cast with Metamagic Spell Actions cost 2 less Mana, to a minimum of 1. |
| Basic | **Focused Pairing** | If a hostile Spell cast with a Metamagic Spell Action targets the same stack as the triggering Spell, it ignores 20% of that stack's Magical Damage Reduction. |
| Basic | **Arcane Acquisition** | If Focus Magic was cast through Metamagic, each living enemy hit by the enchanted shooter receives 2 Arcane Breach marks instead of 1 when it has no marks. A target that already has marks receives the normal 1. The cap remains 3 marks. |
| Advanced | **Echoed Duration** | A temporary effect created by a Spell cast with a Metamagic Spell Action lasts 1 additional round. |
| Advanced | **Split Focus** | If a Spell cast with a Metamagic Spell Action targets a different stack from the triggering Spell, it gains +10% to its Spell Power-based effect. |
| Advanced | **Formula Reserve** | After each Metamagic sequence in which at least one additional Spell was cast is completed, restore 3 Normal Spell Points once. |
| Advanced | **Spell Buffer** | Once per combat, when a Spell Action granted by Metamagic expires unused at the end of the round, gain 6 Buffer Spell Points. The expired opportunity does not consume a Metamagic use. |
| Expert | **Grand Metamagic** | When you cast the extra spell in your third used Metamagic sequence, gain one further Spell Action: that sequence allows two extra spells. Both expire at round end, neither triggers another sequence, and the further action costs no additional Metamagic use. |
| Expert | **Perfect Sequence** | Each Spell cast with a Metamagic Spell Action gains +20% to its Spell Power-based effect if it is a different Spell from every earlier Spell in that Metamagic sequence. |

### Shroud of Malassa

**Availability:** Dungeon heroes only.

**Ranks:** Ghost Walk lets friendly stacks pass through occupied creature hexes, but not finish on them. Flanking melee damage gains **+25% / +40% / +60%**; at Expert, flanking melee attacks also prevent normal retaliation.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Veiled Movement** | Ghost Walk movement does not trigger explicitly movement-based reaction attacks. |
| Basic | **Backstab** | A melee attack from the target's rear-facing side gains an additional +15% damage. |
| Basic | **Ambusher** | The first flanking attack made by each friendly stack in a combat deals +20% damage. |
| Basic | **Shadow Assault** | The first flanking attack against each enemy stack ignores 25% Creature Defense. |
| Advanced | **No Escape** | After suffering a flanking melee attack, the target loses 2 Speed until its next activation. |
| Advanced | **Evasive Shroud** | After making a flanking attack, the attacker receives 15% physical damage reduction until its next activation. |
| Advanced | **Deep Flank** | Friendly ranged attacks against a target currently attacked from at least two distinct melee sides gain half the Shroud's current flanking damage bonus. |
| Advanced | **Night Prowler** | A stack that uses Ghost Walk to pass through an enemy stack's occupied hexes gains +10% damage on its next attack that activation. |
| Expert | **Encircled Doom** | Each additional distinct melee attack side beyond the first adds +10% damage to the Shroud flanking attack. |
| Expert | **Vanish** | After destroying an enemy with a flanking melee attack, the attacker may move up to half its Speed. It gains no additional attack. |

### Demonic Gating

**Availability:** Inferno heroes only.

**Ranks:** An eligible Inferno stack spends its activation to open a Gate on an empty legal hex within **3 hexes**. A stack from the Demonic Reserve arrives at the start of the next round. **Basic:** Core reserves; **Advanced:** Core or Elite; **Expert:** Core, Elite or Champion. Gating deploys stored reserves, not free copies of the stack opening the Gate.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Swift Gate** | A gated stack arrives at the end of the current round instead of the start of the next round. |
| Basic | **Wide Gate** | Gate placement range increases from 3 to 5 hexes from the gating stack. |
| Basic | **Hellfire Arrival** | When a gated stack arrives, adjacent enemy stacks suffer Fire damage equal to 15% of the gated stack's total current HP, divided evenly among them. |
| Basic | **Reinforced Gate** | A gated stack gains temporary HP equal to 20% of its current total HP, rounded down, until combat ends. This shield is lost before creature HP, creates no creatures, and cannot be healed or resurrected. |
| Advanced | **Mobile Gate** | In one creature activation, optionally move along a legal path up to half Speed, rounded down, then Gate. Choosing not to move is allowed. A rejected action or interrupted movement does not consume the selected reserve stack. |
| Advanced | **Infernal Beacon** | If the Gate is placed adjacent to another friendly Inferno stack, the gated stack gains +2 Initiative during its first round in which it can act. A delayed arrival never consumes the bonus before the gated stack can act. |
| Advanced | **Chain Gate** | After a gated stack destroys an enemy stack, the next Gate opened during that combat resolves one timing step sooner. |
| Advanced | **Reserve Discipline** | Gated stacks treat negative Morale as 0 during the round in which they arrive. |
| Expert | **Master Gate** | The first Gate opened each combat does not consume the gating stack's creature activation. After opening the Gate, that stack proceeds with its normal creature activation. |
| Expert | **Endless Legion** | After a victorious combat, 50% of casualties suffered specifically by gated Core and Elite reserve stacks are restored to the Demonic Reserve, rounded down. |

### Necromancy

**Availability:** Necropolis heroes only.

**Ranks:** After victory, raise Skeletons equal to **10% / 20% / 30%** of eligible living enemy creature casualties, rounded down. Ordinary magical casualties need Corpse Preservation; remains destroyed by effects such as Disintegrate remain unusable.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Bone Collector** | Necromancy raises an additional +5 percentage points of eligible casualties as Skeletons. |
| Basic | **Corpse Preservation** | Casualties caused by ordinary magical damage become eligible for Necromancy. Without this perk, ordinary magical casualties are excluded. Effects that explicitly destroy or invalidate remains, such as Disintegrate, remain excluded even with the perk. |
| Basic | **Dark Conversion** | When resolving Necromancy, every complete group of 3 Skeletons generated from eligible Core-tier casualties is automatically raised as 1 Zombie instead. |
| Basic | **Black Harvest** | After Necromancy raises at least 10 creatures, recover 1 Mana per 10 creatures raised, up to 10 Mana. |
| Advanced | **Soul Harvester** | When resolving Necromancy, every complete group of 6 Skeletons generated from eligible Elite-tier casualties is automatically raised as 1 Wight. Wights remain Core-tier; the conversion's input is Elite-tier casualties, not its output. |
| Advanced | **Death Lord** | Construct and Elemental casualties become eligible at 25% of the normal Necromancy conversion rate. |
| Advanced | **Grave Knowledge** | When defeating Undead enemies, 20% of their eligible casualties may be reclaimed as Skeletons even though they are not living. |
| Advanced | **Ossuary** | If the hero cannot accept the raised creatures because of unavailable army slots or Leadership limits, they are sent to the nearest owned Necropolis town instead of being lost. |
| Expert | **Master of Bones** | Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player. |
| Expert | **Lord of the Dead** | After defeating an army containing a living Champion, replace 12 Skeletons from a base Necromancy result of at least 12 with 1 Bone Dragon, once per combat. This happens before other conversions; Champion or unclassified contributions pay first, then Elite, then Core. Later conversions use only what remains. |

### Bloodrage

**Availability:** Stronghold heroes only.

**Ranks:** Each non-summoned stack destroyed adds **+5% / +8% / +12% damage** to surviving friendly stacks, up to **+20% / +40% / +60%** in total.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **First Blood** | The first qualifying stack destroyed in each combat grants two Bloodrage increments instead of one. |
| Basic | **Blood Scent** | When attacking an enemy below 50% maximum HP, treat Bloodrage as one increment higher for that attack. |
| Basic | **War Drums** | Begin each combat with one Bloodrage increment already active. |
| Basic | **Rage Through Pain** | The first time a friendly stack falls below 50% maximum HP, that stack gains one personal Bloodrage increment for the rest of combat. |
| Advanced | **Slayer** | Destroying an Elite or Champion stack grants two Bloodrage increments. |
| Advanced | **Unrelenting** | At half of the current Bloodrage cap or higher, friendly stacks gain +1 Speed. |
| Advanced | **Berserker** | At half of the current Bloodrage cap or higher, friendly stacks gain one additional retaliation. |
| Advanced | **Fury Unbound** | While at least one Bloodrage increment is active, friendly stacks treat negative Morale as 0. |
| Expert | **Avatar of Rage** | At maximum Bloodrage, friendly creature attacks ignore 25% Creature Defense. |
| Expert | **Endless Bloodshed** | Increase Bloodrage's maximum damage cap by 20 percentage points. |

### Bulwark of the Mire

**Availability:** Fortress heroes only.

**Ranks:** While Defending, gain physical damage reduction of **5% + 0.10% × Hero Defense / 7.5% + 0.15% × Hero Defense / 10% + 0.20% × Hero Defense** until the next activation. The first melee attacker suffers a pre-emptive hit at **50% / 75% / 100%** normal damage. At Advanced/Expert, reflect **25% / 50%** of melee physical damage actually received after reductions.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Mireborn** | On swamp or rough terrain, Bulwark grants an additional 5 percentage points of physical damage reduction. |
| Basic | **Thick Hide** | Bulwark's damage reflection also applies to ranged physical creature damage at half its normal reflection percentage. |
| Basic | **Bog Ambush** | Bulwark's pre-emptive attack gains +25 percentage points of normal damage, capped at 100%. |
| Basic | **Toxic Spines** | Once per Defending Bulwark stack per round, the first melee enemy damaged by its reflection is Poisoned. Base damage is reflected HP damage ÷ 4, rounded down, minimum 1. Its next three activations take base damage, 1.5× base rounded down, then 2× base. An equal or stronger application replaces and restarts it; weaker ones are ignored. Cure removes it; Dispel does not. |
| Advanced | **Deep Bulwark** | A stack currently benefiting from Bulwark cannot be forcibly displaced by non-magical effects. |
| Advanced | **Swamp Renewal** | At the beginning of the next activation after Defending, a surviving Bulwark stack restores 10% of physical creature damage suffered while Defending; casualties are not restored. |
| Advanced | **Mire Grip** | A melee attacker that damages a Bulwark stack loses 2 Speed until its next activation. |
| Advanced | **Shared Cover** | Adjacent friendly stacks that are also Defending receive half of this hero's Bulwark damage-reduction bonus. |
| Expert | **Immovable** | While Defending, the first physical creature attack against each Bulwark stack each round deals an additional 25% less final damage. |
| Expert | **Vengeful Mire** | Increase Bulwark's melee damage-reflection percentage by +25 percentage points, to a maximum of 75%. |

### Elemental Rebirth

**Availability:** Conflux heroes only.

**Ranks:** When an eligible non-summoned allied stack is destroyed, summon a random Elite Elemental at its position with **25% / 40% / 50%** of the destroyed stack's maximum total HP.

| Tier | Perk | Effect |
| --- | --- | --- |
| Basic | **Elemental Attunement** | A reborn Elemental that matches the current battlefield terrain receives +20% total HP. |
| Basic | **Swift Rebirth** | A newly reborn Elemental is inserted into the current Initiative cycle immediately after the current creature activation, but cannot receive more than one activation in the round. |
| Basic | **Elemental Memory** | A reborn Elemental inherits the destroyed stack's current positive Morale and Luck modifiers until the end of the round. |
| Basic | **Primal Burst** | When a reborn Elemental appears, adjacent enemies suffer elemental damage equal to 10% of the reborn stack's total HP, divided evenly among them. |
| Advanced | **Greater Essence** | Elemental Rebirth creates an additional +15 percentage points of the destroyed stack's maximum HP. |
| Advanced | **Elemental Ward** | Reborn Elementals receive 20% magical damage reduction. |
| Advanced | **Adaptive Element** | Instead of a fully random Elemental, Rebirth selects from Elemental types appropriate to the current battlefield terrain. |
| Advanced | **Rebirth Chain** | Once per combat, when a reborn Elemental is destroyed, summon a second random Elite Elemental with 25% of that reborn stack's original HP. This second Elemental cannot trigger Rebirth. |
| Expert | **Perfect Convergence** | Every Rebirth uses the primary Elemental type associated with the current battlefield terrain by the Elemental Convergence terrain mapping instead of a random Elemental. |
| Expert | **Phoenix Spark** | Once per combat, the first eligible original friendly Champion destroyed produces a temporary Phoenix instead of an Elite Elemental, with 25% of its battle-start maximum total HP. This replaces normal Rebirth and Greater Essence scaling. The use is spent even if placement fails. Elemental Ward, Primal Burst, Elemental Memory and Swift Rebirth still apply; terrain-matching bonuses, Rebirth Chain and recursive Rebirth do not. The Phoenix's own rebirth ability remains separate. |
