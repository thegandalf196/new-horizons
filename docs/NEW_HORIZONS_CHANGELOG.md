# New Horizons changelog

This is a player’s guide to what changes when coming from **Heroes III Complete**.
The first entry collects the main differences; later entries will track additions
and revisions as the game develops.

For individual perk effects, spell formulas, creature statistics and Order rules,
read the [player wiki](https://github.com/thegandalf196/new-horizons/wiki/Home).

## 0.88 — in development

- Focus Magic moves to Level 1 and replaces Haste in ordinary Sorcery spell
  availability. Slow moves to Level 3 and reduces both Speed and Initiative.
- Summon Living Armor is a new Level 1 Sorcery spell: a temporary blue armored
  Skeleton summon with strong physical protection and increased vulnerability
  to magical damage. Its base duration is three rounds.
- Adds dedicated Feint!, Vanguard! and Summon Living Armor icons.
- Grave Knowledge recovers 35% of eligible own Skeleton losses; Bone Collector
  raises 25% more Skeletons. Soul Harvester retains 1 Mana per newly raised
  Skeleton, with a reduced maximum of 30 Mana per battle.
- Corrects Slow applying twice to creatures whose Initiative falls back to
  Speed, and Focus Magic parameter validation warnings.
- Existing saves retain their captured rules and balance values. Start a new
  scenario to use the revised spell roster and Necromancy values.

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
