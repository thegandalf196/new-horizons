# New Horizons changelog

This is a player’s guide to what changes when coming from **Heroes III Complete**.
The first entry collects the main differences; later entries will track additions
and revisions as the game develops.

For individual perk effects, spell formulas, creature statistics and Order rules,
read the [player wiki](https://github.com/thegandalf196/new-horizons/wiki/Home).

## 0.90 Early Access

**A new game is required, including when upgrading from 0.89 or 0.89.1.**
Current and minimum save format are **1158**. Earlier saves are rejected before
payload loading, not converted, rewritten or deleted. Keep backups and the
matching older version to continue older playthroughs.

Special thanks to **Matrix4767**, whose extensive discussions on Discord helped
shape and refine the mechanics, design, and direction of this release.

### Command and Authority redesign

- Replaces per-stack Leadership capacity and finite per-battle Order pools with
  persistent **Command**: an unspent Secondary Attribute developing seven
  persistent army slots.
- **Slot Tiers**: Heroes start with three **Core** slots (**I**), unlocking three
  **Elite** slots (**II**) and one **Champion** slot (**III**) as Command grows.
- **Slot Enhancements**: Continuing Command investment awards repeating enhancement
  choices (e.g. Core slot improvements shared across all three Core slots, preserving
  the hero level when acquired).
- **Creature Specialists**: Creature specialists gain Core placement permission
  for their resolved specialty line and its upgrades without changing the creatures'
  actual tier.
- **Streamlined Army Slot UI**: Removed intrusive "Locked" text boxes and colored
  borders from locked slots. Empty and locked slots now display clean, translucent
  Roman numerals (**I**, **II**, **III**) corresponding to their tier, with locked
  slots remaining transparent.
- **Concise Slot Tooltips**: Right-clicking an army slot now displays a concise,
  direct summary (slot name, requirement/status, allowed tiers, and active slot
  bonuses) without scrolling.
- **Orders Battle Icon**: Restored the classic golden gauntlet icon for the Orders
  button in the battle interface.

### Skills and perks

- **Authority**: A new secondary skill available to both Might and Magic hero
  archetypes, providing Command progression and passive slot training.
- **Warfare**: Might-exclusive skill specializing in Orders (replacing the old
  finite-point Command skill).
- **Leadership**: Refocused entirely on battlefield Morale and spirit (formerly
  Discipline). Orders still use the ordinary Hero Action without consuming army
  Command.
- **Metamagic**: Spells scale dynamically with hero round points (starting at 1,
  gaining 1 per round up to a maximum of 12; each point increases spell
  effectiveness and Magic Penetration). Includes ten revised perks such as
  **Arcane Wellspring** and Halon's **Metamagic Adept**.
- **Town & School Perks**:
  - **Dungeon**: **Shadow Hex** (+15% spell damage vs flanked enemies) and
    **Exposed Weakness** (+20% spell damage following flanking melee hits).
  - **Conflux**: **Elemental Conduit** (+15% spell damage per living Reborn stack)
    and **Elemental Cataclysm** (20% of fallen Reborn stack HP dealt to adjacent
    enemies).
  - **Light / Divine**: **Divine Mandate** automatically triggers a beneficial Light
    spell after an Order without Mana or Hero Action costs.
- **Comprehensive 32-Skill Matrix**: Full 32-skill progression with 10 authored
  perks per skill.

### Combat and Quick Combat

- **In-Battle Auto-Combat (`Q`)**: Pressing `Q` during manual tactical combat now
  resolves with fast Quick Combat AI (`BattleAIQuick`), instantly finishing the
  battle rather than running the deliberative, turn-by-turn tactical AI.
- **Quick Combat AI Settings**: Choose between **Fast** (default for Quick Combat
  and AI-vs-AI battles, drastically reducing auto-resolve duration) and **Full**
  (the comprehensive reference search) in Settings → Battle.
- **Magic Arrow Overcharge**: Selectable Overcharge tiers empower Magic Arrow with
  substantially amplified damage in exchange for non-linear Mana costs, giving
  the fundamental spell scaling utility into late-game engagements.
- **Elemental Attunement**: Replaces Conflux Elemental Rebirth; casting elemental
  spells directly empowers and augments matching elemental stacks on the field.

### Map and balance adjustments

- **Castle Starting Armies**: Default Castle starting setups replace Griffins with
  Swordsmen (and Royal Griffins with Crusaders); Griffin specialists uniquely
  retain their Griffin lineage.
- **100% Stock Neutrals**: Restored authentic 100% stock neutral encounter sizes
  without Leadership calibration. Map recruitment growth remains independently
  configured at 50%.
- **Pathfinding**: Fixed Water Walk landing rejection when a hero lands with
  exactly zero remaining Movement Points.

## 0.89.1 Early Access

- Auto-resolved battles now default to **Fast** combat AI for human Quick Combat
  and AI-versus-AI fights. Fast uses reduced search and can change decisions and
  casualties; it does not change combat rules.
- Adds **Quick combat AI** to the in-game Battle options. Choose **Full**
  for the reference search, which can take longer. Visible auto-fight and
  manually played battles retain their ordinary combat AI.
- This replaces the withdrawn 0.89 package. The 0.89 new-game requirement remains;
  this update introduces no additional save-format change.

## 0.89 Early Access

**A new game is required.** Older saves are not converted or rewritten; keep
backups and use the matching older version to continue those games.

- Revises 32 existing hero specialties with native creature and combat mechanics.
- Gives every fighting hero finite Command. Orders spend one Command point and
  the ordinary Hero Action; revised Command perks and Heroic Spirit provide
  passive benefits.
- Adds selectable Overcharge levels to Magic Arrow, with stronger damage and
  nonlinear Mana prices.
- Replaces Conflux Elemental Rebirth with **Elemental Attunement**: elemental
  spellcasting strengthens matching creatures already on the battlefield.
- Updates hero information and combat readouts for the revised rules.
- Reduces repeated combat-AI validation work. One controlled battle measured
  a 12.08% reduction in median quick-combat time; this is not an across-game gain.

## 0.88.2 Early Access

Published packages and platform notes are available on the
[release page](https://github.com/thegandalf196/new-horizons/releases).

**A new game is required.** Version 0.88.2 cannot load saves from 0.88.1 or
older development builds. Old save files are not converted or deleted; keep
backups and use the matching older version to continue those games.

### Skills and heroes

- **Metamagic** strengthens spells through hero points instead of target marks
  or extra spell actions. Start combat with one point, gain one each later
  round, and stop at twelve. Each point grants 1% / 2% / 3% spell effectiveness
  and Magic Penetration by rank.
- Metamagic has **ten revised perks**, including **Arcane Wellspring**, which
  starts combat with three points. Halon's **Metamagic Adept** grants one extra
  point after each spell he manually casts, within the same twelve-point cap.
- Dungeon's **Shadow Hex** grants +15% hero spell damage against flanked
  enemies. **Exposed Weakness** makes flanking melee hits add another +20%
  hero spell damage against that target until its next activation.
- Conflux's **Elemental Conduit** grants +15% hero spell damage per living
  friendly Reborn stack, including Rebirth Chain summons. **Elemental
  Cataclysm** splits magical damage equal to 20% of a fallen Reborn stack's
  maximum total HP among adjacent enemies.
- **Divine Mandate** automatically casts a useful beneficial Light spell after
  an Order, free of Mana and additional Hero Actions. Rank improves Order
  efficiency and grants one, two or three free spell triggers per combat.
- **Demonic Gating** summons temporary reinforcements of the same creature type
  instead of drawing from a reserve. Each original stack may Gate once per
  combat; ordinary reinforcements contain 30% of its current creatures and
  arrive at the start of the next round.
- Revised Gating perks include **Infernal Onslaught** (+15% attack and
  retaliation damage), **Hellstride** (+2 Speed) and **Endless Legion**
  (50% reinforcements). Gated creatures disappear after combat and cannot Gate.
- **First Blood** grants three Bloodrage increments on the first qualifying
  destruction. **Blood Scent** counts two extra increments against enemies
  below half Health.
- **Rage Through Pain** grants two personal increments when a stack first
  takes damage. **Avatar of Rage** ignores 5% Creature Defense per increment,
  up to 25%.
- **Wisdom** has ten revised perks for Mana efficiency and spell effectiveness.
  **Sage** grants +50 effective Knowledge; **Archmage** strengthens Level 4–5
  spells and their penetration.
- **Warcasting** grants friendly stacks **Arcane Inspiration** after a hero
  combat spell: +5% / +10% / +15% attack and retaliation damage until the next
  combat round. New perks develop its protection, penetration and retaliation.
- **Learning** expands Scholar and Eagle Eye to all eligible spells.
  **Connoisseur** rerolls one offered secondary Skill once at each level-up;
  **Prodigy** grants three normal level-ups.
- **Duel Lessons** grants 10% of a defeated enemy hero's **total Experience**,
  rounded down. Philosopher adds 25 percentage points to Learning's XP bonus.
- **Estates** keeps its 125 / 250 / 500 daily Gold income, with revised resource
  income, recipient-hero recruitment discounts, building discounts and weekly
  Rogue guards for owned mines.
- Halon starts with **Arcane Valor**. Saurug, Sephinroth, Rissa, Calid and Grindan
  start with **Prosperity**; their other specialties remain unchanged.
- Nimbus, Sanya and Geon retain their Learning XP scaling; their extra-spell
  bonuses are removed. Thane's **Sage of the Ages** remains a separate level-up
  specialty from Wisdom's Sage and the retired Learning Sage.
- In new games, **Fafner and Cyra** start with **Focus Magic** instead of the
  retired Haste spell; Haste is excluded from current hero spell access.
- **Zubin** becomes a Havoc Fireball specialist, starting with Fireball,
  Basic Havoc Magic, Bloodrage and Pyromancer.

### Combat and interface

- Skill, perk, spell, Order and hero-specialty descriptions state their effects
  in plain language. An **i** control in persistent help windows opens the
  detailed formulas and interactions.
- The hero screen has a **Temporary Effects** view and new information and
  hourglass buttons, with clearer information-panel layout.
- Experimental neutral-army calibration considers Leadership and available
  troops instead of relying solely on a flat size multiplier. The adjustable
  neutral-size slider defaults to **100% of the calibrated counts**; independent
  recruitment growth remains **50%**.
- Combat AI reuses equivalent damage and spell calculations while preserving
  its tactical scoring and legal actions. Some battles and AI turns remain
  slow.
- Spell previews retain hero-adjusted values, and magical damage-protection
  tooltips display their percentage correctly.

Further creature-specialist reworks are reserved for **0.88.3**. Detailed perk
and spell rules belong in the
[player wiki](https://github.com/thegandalf196/new-horizons/wiki/Home).

Balance and selected artwork remain provisional.

## 0.88.1 Early Access

### Heroes and army pacing

- Adds new specialties for **34 creature-specialist heroes**. These include
  Valeska's full-strength Archer melee, Wystan's long-range Lizardman shooting,
  Dace's stronger Minotaur retaliation, Ryland's Dendroid defense aura, and
  distinct maintenance, war-machine and attack-sequence specialties.
- Restores the earlier starting Leadership values, reversing the 0.87.1
  increase. Per-level Leadership and independent capacity for each army slot
  retain their existing rules.
- New scenarios default to **50% neutral army size** and **50% recruitment
  growth**. A new-game slider lets you choose neutral army size independently
  of recruitment. Recruitment growth rounds up after its ordinary modifiers;
  existing armies and recruitment stock retain their counts.
- Updates supported adventure-map Primary Attribute rewards to match the new
  attribute scale. Creature-bank troop rewards handle Leadership limits and
  full armies while retaining eligible unclaimed troops.
- Iona's **Wheel of Fortune** gives her Genies and Master Genies Bestow Luck
  (+3 friendly Luck); Master Genies also gain Turn of Fate (−3 enemy Luck).
  These choices share the stack's three Wishes charges.
- Thane becomes **Sage of the Ages**, starts with Historian, and gains an
  additional +1 Spell Power and +1 Knowledge on each level gained.
- Vidomina unlocks Ghosts as a Wraith upgrade. Soul Harvest temporarily grows
  their stack from the enemy creatures they kill; those gains end with combat.

### Skills, Orders and magic

- Rampart's **Sylvan Tactics** grants +1/+2/+3 Initiative by rank, with perks
  built around waiting, positioning, protection and battlefield opportunities.
- **Perfect Moment** makes a positive creature Lucky Strike deal **3.5× total
  normal physical damage**. It does not grant Luck or guarantee a Lucky Strike.
- Dungeon's faction Skill is now displayed as **Shroud of Nighon**.
- **Focus Magic** moves to Level 1 and replaces Haste in ordinary Sorcery
  availability. **Slow** moves to Level 3 and reduces both Speed and Initiative.
- **Summon Living Armor** is a new Level 1 Sorcery spell: a temporary blue
  armored Skeleton with strong physical protection and increased vulnerability
  to magical damage. Its base duration is three rounds.
- Adds dedicated Feint!, Vanguard! and Summon Living Armor icons.
- **Grave Knowledge** recovers 35% of eligible own Skeleton losses;
  **Bone Collector** raises 25% more Skeletons. The Mana-recovery perk is named
  **Soul Drinker** and restores 1 Mana per newly raised Skeleton, up to 30 Mana
  per battle and the hero's Normal Mana capacity.

### Interface and reporting

- Spellbook entries show names, costs and hero-adjusted effect summaries;
  right-click descriptions provide requirements and formulas.
- Adds dedicated quick-save and quick-load controls and revised specialty,
  spell and Order artwork. Selected new artwork remains provisional.
- In-game bug reporting offers an editable description and optional reviewed,
  sanitized log before opening a GitHub issue draft. Unexpected-exit reports
  use the same review and consent steps. See the
  [bug-reporting guide](players/Bug_Reporting_Guidelines.md).
- Optional main-menu release notifications link to a newer Windows or Linux
  player download. Downloads and installation remain under your control.

### Corrections and compatibility

- Corrects Slow applying twice when Initiative falls back to Speed, and Focus
  Magic parameter-validation warnings.
- Improves Poison and Hex targeting and status handling, Frozen/Cure/Purify
  interactions, and physical-protection handling for mixed damage types.
- Refines combat AI handling of the enabled specialties, Orders, status effects
  and visible battlefield obstacles.
- Existing saves retain their captured rules and values. Start a new scenario
  to use the revised defaults, spell roster and hero profiles.

Thanks to **Matrix4767** for playtesting feedback on Necromancy pacing.

[Download 0.88.1 and read installation instructions](https://github.com/thegandalf196/new-horizons/releases/tag/v0.88.1).
Linux gameplay checks passed for selected scenarios and interface flows. Windows
build and static package checks passed; native Windows startup, gameplay, audio
and save/reload have not been tested for this release. Combat performance remains
a known unresolved issue. Balance and selected artwork remain provisional.

## 0.87.1 Early Access

- Starting Leadership increases by 20% for every hero class; Leadership gained
  per level and ordinary creature Leadership costs remain unchanged.
- Necromancy's perk choices are revised: Death March and Grave Knowledge become
  Basic, Bone and Steel becomes Advanced, and Tomb Guard becomes Expert and
  grants Double Strike.
- Dead Man's Luck replaces Ossuary, granting Skeletons and Skeleton Warriors
  +3 Luck. Boneward grants a 50% chance to resist hostile spells.
- Grave Knowledge recovers 50% of eligible own Skeleton losses. Bone Collector
  raises 50% more Skeletons, while Soul Harvester restores 1 Mana per newly raised
  Skeleton, up to 50 Mana per battle and the hero's Normal Mana capacity.
- Fixes an AI turn failure after a hero exhausts movement during a visit.
- Existing saves retain their captured Leadership values and perk rules. Start
  a new scenario to use this revision.

## 0.87 Early Access

- Feint! and Vanguard! replace Protect! and Flank!: army-wide retaliation
  protection or temporary combat inspiration, without selecting a target.
- The Chosen replaces Crusade! with a level 5 Light spell granting Double Strike,
  unlimited retaliations, Flying and creature Charge to one friendly living,
  non-mechanical stack. Only one stack per army may benefit at a time.
- Necromancy reduces Skeleton Leadership costs. Revised perks develop Skeleton
  protection, retaliation, recovery and Speed rather than alternate raised troops.
- Related perks and hero specialties follow the revised Orders and Light spell.
- Includes targeted combat AI cleanup and a repair for quitting a loaded game.

Download the [Windows or Linux release](https://github.com/thegandalf196/new-horizons/releases/tag/v0.87).
Start a new scenario to use the revised rules; existing saves retain their captured
rules. Detailed effects and formulas are in the
[player wiki](https://github.com/thegandalf196/new-horizons/wiki/Home).

## Current development baseline — 24 October 2026

This comparison describes the current development version, including changes after
the published Early Access releases. Older saved games may retain earlier rules. New Horizons remains in development;
balance, artwork and some interactions are still being refined.

### Gameplay

- **Hero attributes and creature statistics are separate.** Heroes still develop
  Attack, Defense, Spell Power and Knowledge, but hero Attack and Defense no
  longer automatically add to every creature’s statistics. Their contribution
  comes through Orders and effects that explicitly use them. Base attribute
  growth follows a fixed pattern for each class, giving a Knight and a Wizard
  more distinct development paths.

- **Orders give heroes active military choices.** A hero normally has one Hero
  Action each combat round, spent on either a Spell or an Order. Orders cost no
  Mana. Charge! rewards an advance into melee; Hold the Line! protects troops
  that hold their positions; Focus Fire! directs shooters against a chosen enemy.
  Particular abilities can grant additional, restricted opportunities.

- **Skills now have perks.** Basic, Advanced and Expert ranks remain, but each
  Skill has ten alternative perks. A fully developed Skill has three selected
  perks: one Basic, one Advanced and one Expert. Ordinary advancement alternates
  between a Skill rank and a perk. A level-up can offer up to two Skill choices
  and two perk choices, and you choose one option. When choosing which Skill
  supplies a perk offer, the strongest class weighting is at most twice the
  weakest. Ordinary Skill offers use the full New Horizons class weights.

- **Familiar disciplines have new homes.** Navigation, Pathfinding and Scouting
  become Logistics perks; Scholar and Eagle Eye belong to Learning; Artillery,
  Ballistics and First Aid come together under War Machines. Command develops
  a Might hero’s Orders, Wisdom develops a Magic hero’s spell efficiency, and
  Warcasting rewards alternating between Spells and Orders. Each faction also
  has its own Skill, shared by its two hero classes.

- **Four magic schools become six.** Light, Shadow, Nature, Havoc, Sorcery and
  Chaos replace Air, Earth, Fire and Water as the schools for hero spellcasting.
  Spells and school development have been reorganized, and each town favors a
  pair of schools. All nine towns can build Mage Guilds through Level V,
  including Castle, Stronghold and Fortress.

- **Leadership limits each army slot.** Every creature has a Leadership
  requirement. Divide the hero’s Leadership by that requirement, rounding down,
  to find the maximum number in a stack. All seven slots receive that capacity
  independently: filling one does not consume the allowance in another.
  Leadership grows with the hero and differs by class.

- **Armies use Core, Elite and Champion categories.** These replace the familiar
  seven numbered tiers in New Horizons creature and recruitment displays.
  Categories also matter to abilities that name them; the seven army slots remain.

- **Speed and Initiative have different jobs.** Speed determines combat movement;
  Initiative determines when a stack acts. A hero’s adventure movement is also
  independent of the slowest creature in the army, so bringing slow troops no
  longer reduces the hero’s daily movement for that reason.

- **Academy and Conflux have changed rosters.** Gremlins become Cabirs and Cabir
  Masters, both shooters; Cabir Masters can repair Gargoyles and Golems. Conflux gains
  Wisps and Greater Wisps. The four classical Elemental lines have revised
  statistics and roles. Ice Elementals become melee creatures with a chance to
  Freeze their target. A Frozen stack forfeits its next normal activation unless
  freed beforehand. A physical creature attack Shatters the ice for extra damage,
  freeing the target.

- **Hero identities are being revised.** Starting development, biographies and
  specialties are being reworked. For example, Isra strengthens Necromancy, while
  Malekith strengthens Spellcraft’s contribution to direct spell damage. Rion
  improves Cure and First Aid Tent creature healing; Ayden can recover Normal
  Mana when entering combat with Intelligence active. Nimbus strengthens Learning
  and learns an additional eligible spell with Eagle Eye, while Oris strengthens
  Learning and authorized Mentor teaching. Rion and Verdish start with a First
  Aid Tent when using default equipment; explicitly authored equipment is respected.
  Axsis gains a Mysticism-based refill after victory; Jaegar gains extra day-end
  recovery when Mana is low; Rosic gains a refill on the day's first combat.
  Verdish improves Tent healing and Regeneration's Spell Power component.
  Sanya can teach an additional eligible Scholar spell, while Geon can receive
  one. Tiva gains experience from permanent magical study. Vidomina can turn
  leftover Necromancy value into Zombies after building their dwelling.
  Further individual hero redesigns remain in development.

### Artwork and presentation

- **Tower becomes Academy**, with a desert theme replacing its snowy setting.
  New town and adventure-map artwork, creature backgrounds and related interface
  details support that change.
- **Cabirs, turbaned Magi and Wisps have selected custom battle artwork and
  portraits.** Cabirs and Wisps also have new adventure-map sprites; Cabirs have
  new projectiles.
- **New Mage Guild artwork fills the missing upper levels** in Castle,
  Stronghold and Fortress.
- **The new systems have their own visual vocabulary:** six-school spellbook
  artwork, spell icons and effects, Skill and perk images, Order controls and
  revised hero-development presentation. Selected heroes also have new specialty
  icons; the full hero and specialty-art roster is still being developed.

AI-generated artwork remains **provisional**. Images already included in the game
may still be revised or replaced; their inclusion does not mean the visual work
is finished. Original Heroes III Complete game assets are still required
separately and are not included.

### Still ahead

Further integration, AI and save/load checks, balance work and visual refinement
continue. Governors, regional administration and Caravans are future systems,
rather than features included by this comparison.
