# New Horizons changelog

This is a player’s guide to what changes when coming from **Heroes III Complete**.
The first entry collects the main differences; later entries will track additions
and revisions as the game develops.

For individual perk effects, spell formulas, creature statistics and Order rules,
read the [player wiki](https://github.com/thegandalf196/new-horizons/wiki/Home).

## 0.92.4 Early Access

- The Windows and Linux launchers show first-run setup and artwork preparation
  progress. They can check for and install matching game updates.
- Quick combat reuses damage estimates when the active battle state permits
  it and evaluates the three most promising approaches to distant targets.
- Fixes a crash while the AI evaluates a spell against an affected stack.
- Interrupted artwork preparation can be retried on the next launch.
- Canceling artwork preparation or closing the Linux launcher during startup
  retains the log and allows the game to start normally on the next launch.
- Fixed a Linux image-scaling crash when the X11 display disconnects.
- Vidomina pays the normal Wight-to-Wraith upgrade price to convert each
  Wraith into a Ghost. Soul Harvest shows the Resurrection animation on the
  growing Ghost stack and plays the Vampire Lord's drain sound.
- Supported 1164 saves keep their captured rules.

## 0.92.3 Early Access

- Arcane Overflow recognizes ordinary hex-targeted spells, including Ice
  Bolt. Spells with projectiles launch their spill from the defeated stack.
- Town income opens Marketplace or Town Administration. Press S for
  administration. Accepted steward changes refresh projected daily Gold.
  The Town Hall's separate Steward button is retired.
- The Castle faction preview lists Swordsmen before Griffins, matching
  the town's construction order.
- Halon starts with Unraveling Magic and Dispel.
- Living Armor rises from blue bones and uses an aetheric portrait backdrop.
- Prayer and Order status cards show their own icons. Active Orders sit
  beneath the creature portrait. The Prayers page shows Faith once.
- Creature ability cards have new icons and use matching spell artwork
  for innate casts and spell effects triggered by attacks.

## 0.92.2 Early Access

- Fresh default heroes start with a spellbook, two Basic Skills and one
  authored Basic perk. Existing saves retain their captured starting rules.
- Castle spellbooks include a Prayers bookmark with the six automatic
  Consecrated Arms effects. Order feedback identifies the recipient and
  plays the prayer animation. The bookmark serves as a reference.
- Metamagic Overflow shows the follow-up spell impact. Focus Magic has a
  casting animation and a blue projectile. Force Lance has a blue icon and
  a new impact sound.
- Adds unique icons for all Consecrated Arms and Metamagic perks, new artwork
  for the three Metamagic ranks, and a new Forced Recall icon.

## 0.92.1 Early Access

- Corrects perk progression to one Basic, one Advanced and one Expert perk per
  Skill. Fresh default heroes have two Basic Skills and no perks. Skill ranks
  and perks remain separate level-up advancements.
- Simplifies the four-offer level-up display and confirms a perk with one click.
- Gives 11 skills distinct Basic, Advanced and Expert illustrations and updates
  the matching hero specialty icons.
- Revises Sorcery and Academy Metamagic, including Focus Magic, Forced Recall
  and Force Lance. Retires Counterspell and the previous time-stasis mechanics.
- Replaces Divine Mandate with Consecrated Arms. Issuing an Order grants a
  prayer to its recipients, with effects that scale with the hero's Faith.
- Removes the Conflux Vault of Ashes and its additional Fire Elemental growth.
- Compiles captured rules for battle and spell evaluation. The release uses the
  faster reference forecast. The compact forecast is available in test builds.

Start a new game in 0.92.1. The current save format is **1164**. Earlier saves
are rejected without changing the saved files.

## 0.92.0 Early Access

This initial package was withdrawn because its perk progression did not match
the intended rules. The following records that package's scope.

- Four fixed weighted offers grant one Skill or perk advancement per level.
  A perk offer opens its Skill’s complete currently eligible pool; each Skill
  holds at most three perks, with accessible earlier tiers remaining available
  under their rank and perk prerequisites. External Skill teaching grants no
  automatic perk; fresh default heroes retain their authored starting perks.
- Revises the ten Estates perks around location rewards, kingdom production,
  defended mines, remote stewards and shared purchase quotes. Rank income
  remains 125/250/500 Gold.

- Mixes Sylvan Tactics activation and Waiting perks with Hunting Pack, Evergreen
  Blessings, Kindred Flame and Ancient Accord. Perfect Moment and the rank
  Initiative bonuses remain. Existing games retain their captured perk effects.

- Hardens AI turn recovery, exhausted level-up handling and battle strength
  calculations for large stacks. Initial autofight work is owned and drained
  during battle changes, replay and shutdown rather than left detached.

- Corrects count-dependent creature bonuses at battle initialization. Galthran's
  Bone Formation now uses the completed stack count before the first action;
  its physical Defense bonus remains separate from percentage protection.
- Creature inspection distinguishes Physical Protection, Magical Protection
  and Magical Resistance. Magical Protection shows the common reduction for
  spells and ordinary magical creature attacks; conditional spell protection
  remains separate from this baseline and from resistance chance.
- The hero screen shows the captured per-level Movement gain beside current
  and maximum Movement, using the same green growth display as attributes.

Save format is **1161**, with minimum supported format **1159**. Supported
historical saves retain their captured progression, pending choices and Estates
rules. A new game is still required when upgrading from 0.90 or earlier; older
files are rejected without conversion or modification.

The withdrawn package is unavailable for download.

## 0.91.2 Early Access

- Fresh default starts for all 163 registered heroes, including campaign and
  portrait heroes, have two distinct Basic Skills and one Basic perk for each. Astral
  starts with Spellcraft/Concentration and Metamagic/Lingering Magic; Seth
  retains Grave Knowledge and Eagle Eye. Existing saves and authored scenario
  advancement remain intact. Later ranks offer the complete eligible perk tier.

- Seth's Ancient Oaths offers Royal Mummies only while he visits or garrisons
  an owned Necropolis with an upgraded Tomb of Souls. Royal Mummies share its
  Wraith stock; ordinary Wight/Wraith recruitment, costs and existing Mummies
  remain unchanged. Existing Mummies can still upgrade to Royal Mummies there.

- Hero initialization and saving reuse a validated perk catalogue within each operation,
  avoiding repeated full-catalogue scans while preserving admission checks.
- The first day retains setup packets in one replay chapter, avoiding a
  redundant full-world snapshot on both the simulation and client sides.
- Adventure maps use upstream VCMI's direct GPU composition where supported,
  with the existing layered rendering available for other views.
- Water and Ice Elementals use the corrected portrait backgrounds and indexed
  battle animations, preserving their colors during selection highlighting.
- Windows adds `Setup.exe` beside the game executable for manually selecting
  the original Heroes III asset folder again.

Extract the entire Windows ZIP into a new folder. Run `new-horizons.exe`, or use
`Setup.exe` to select the original Heroes III assets manually. See the
[Windows installation guide](players/Installation_Windows.md) and the
[0.91.2 downloads, checksums and corresponding sources](https://github.com/thegandalf196/new-horizons/releases/tag/v0.91.2).

## 0.91.1 Early Access

- Integrates the Conflux artwork omitted from 0.91: Pasis is now
  displayed as **Veyra** and Monere as **Orun**, with their approved biographies
  and portraits. Fiur, Lacus and Kalt receive their approved portraits.
- Replaces Water and Ice Elemental battle animations, adventure sprites,
  portraits and map attack artwork. Includes the approved Ice projectile art
  and alignment metadata while retaining its current melee role and abilities.
- Adds the aged, bloodied-linen Mummy artwork across battle animations, map
  sprites and portraits. Royal Mummy artwork and both creatures' rules are unchanged.
- Level-up offers place a weighted existing-Skill upgrade on the left and a
  weighted new Skill on the right whenever those choices are available.
- All standard heroes begin with fitting perks for their starting Skill ranks,
  without an opening perk-selection prompt. Seth begins with Grave Knowledge
  and Eagle Eye. Later Skill ranks still offer the complete eligible perk tier.
- Heroes gain one additional daily Movement point per level after the first,
  added after Logistics. Gaining a level does not refill remaining Movement.
- Reworks Sylvan Tactics around the Rampart army: Hunting Pack, Dwarven Hardiness,
  Disarming Shot, Briar Grip, Evergreen Blessings, Silver Wings, Sacred Grove,
  Kindred Flame and Ancient Accord. Perfect Moment retains its existing effect.
- Windows packages expose one launch executable at the root, with engine files
  in `runtime/`. First-run setup on Windows and Linux lets players select their
  original Heroes III assets and remembers the selection.

Save format remains **1159**, compatible with 0.91. Authored map/save hero names
and portraits retain their normal precedence over updated defaults. Existing
games retain their captured progression, Movement and Sylvan rules. Start a new
game to receive the revised defaults.

Extract the entire Windows ZIP into a new folder, then run `new-horizons.exe`.
See the [Windows installation guide](players/Installation_Windows.md) for setup.
An original Heroes III Complete installation is required; original game archives
are not included. Corresponding fork and dependency sources and SHA256 checksums
are available on the [release page](https://github.com/thegandalf196/new-horizons/releases/tag/v0.91.1).

Linux rendered checks cover the new artwork, representative Sylvan effects,
progression and save/load. Native regression evidence combines 556 distinct
passing cases across three reviewed runs; this is not a single all-green
full-suite run. Windows compilation, launcher, resource-decoder and static
package/source audits passed. Native Windows GUI and gameplay remain unverified.
Reports of uneven adventure-map movement remain under investigation.

## 0.91 Early Access

**A new game is required, including from 0.90.** Current and minimum save format
are **1159**. Older files are rejected before payload loading and remain unchanged;
use their matching older build to continue them. Development source, published
release assets and promoted playable snapshots are separate identities.

- **Seth** replaces Nimbus's default identity. His **Ancient Oaths** specialty
  adds Mummy and Royal Mummy alternatives to a locally attended owned Necropolis
  Tomb's existing Wight/Wraith stock. Royal Mummies shoot and retain normal melee
  damage; both preserve the ordinary Mummy combat profile and Curse.
- Restores numerical **Leadership**, derived from each class's actual Attack and
  Defense growth, across all 18 classes.
- Retires persistent army Command, slot-tier locks and repeating enhancements.
  Every hero has seven unrestricted slots and may command oversized stacks.
- **Square-root efficiency** reduces the offensive contribution of creatures
  above each stack's full-efficiency capacity. Additional creatures always add
  damage and retain full HP. The multiplier applies to direct melee, ranged and
  retaliation damage, including magical creature attacks; spells and war machines
  remain separate.
- **Authority** provides +15% / +30% / +45% effective Leadership, with ten passive
  alternatives for creature category, intrinsic shooting, faction and composition.
  Composition-sensitive perks use the permanent battle-start army and do not
  switch when stacks die or temporary creatures arrive.
- **Necromancy** restores 50% / 60% / 70% Skeleton-family Requirement discounts.
  **Lord of the Dead** replaces that discount with 90% total and grants no HP bonus.
- **Discipline** again names the +1 / +2 / +3 Morale Skill. **Warfare** retains
  battlefield Orders and the shared Spell-or-Order Hero Action, with no finite
  Command payment.
- Each acquired Skill rank immediately grants one mandatory choice from its
  complete corresponding perk tier. Starting Skills, external teachers,
  multi-rank rewards and AI heroes follow the same rule; pending choices persist.
- Keeps each Connoisseur reroll button with its corresponding Skill offer.
- Preserves the native Heroes III/VCMI XP curve and existing XP accelerators;
  this migration does not silently rebalance Learning, Prodigy or XP awards.
- Uses compact red broken-sword efficiency indicators on portraits and below
  battlefield creature counters. The default-enabled display setting is visual
  only; right-click inspection always provides detailed efficiency information.
- Adds **Retaliations**, **Physical Resistance** and **Magical Resistance** below
  Initiative in creature information. Magical Resistance shows spell-rejection
  chance, separately from magical damage reduction. Galthran's physical Defense
  bonus is included in the Defense readout, with damage-type details in its tooltip.
- Removes repeated rank labels and destination-slot text from recruitment dialogs;
  creature rank remains available in right-click information.
- Displays a lance cursor and the creature's current maximum attack distance for
  Reach. Wisps display Reach 6; approach selection follows a real clear corridor,
  including the exposed edge of doublewide targets.
- Fits full creature animations in both Conflux Fort overview layouts, preserving
  wings and other artwork previously cropped by the portrait viewport.
- Places Castle's Barracks before the Griffin Tower in construction, exchanges
  their base and upgraded building costs, and gives Core infantry earlier access
  while preserving unrelated buildings' existing Blacksmith requirements.
- Adds visual feedback to Straker's Undying restoration and Kalt's Rising Tide
  additions, using the existing Hydra regeneration and First Aid Tent effects
  without changing either specialty.
- Retains the independent Castle starting-package amendment, stock neutral counts,
  recruitment growth, Magic Arrow, Elemental Attunement and other faction rules.
- Renames Energy Elemental to **Plasma Elemental** without changing its stable ID.
- Makes Water and Ice Elementals occupy one battlefield hex and updates both Wisp portraits.

These entries describe the 0.91 rules. Compilation, native checks, rendered
acceptance, measured performance and release publication are separate gates.

## 0.90 Early Access

The published Command/Authority release requires format **1158** and a new game
from 0.89.x. It introduced persistent army-slot progression and captured
Command enhancements, universal Authority, passive Warfare and no finite Order
payment. Those army-progression mechanics are superseded in 0.91
by the Leadership migration above; existing files are not converted.

In-battle auto-resolve uses Quick Combat AI. Fast remains the default for human
Quick Combat and AI-only auto-resolved battles, with Full available in Battle
settings. Reduced search may change decisions and casualties; no universal
instant-combat timing is promised.

Fresh neutrals use 100% stock counts without Leadership calibration, while
recruitment growth remains independently 50%. The generated Castle starting
package replaces Griffins with Swordsmen except for genuine Griffin specialists.
Magic Arrow Overcharge and deterministic Elemental Attunement retain their
existing profiles; retired Reborn-stack Conflux effects are not restored.

Special thanks to **Matrix4767** for extensive discussions that helped shape
and refine the mechanics and direction of this release.

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

## Historical development baseline — 24 October 2026

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
