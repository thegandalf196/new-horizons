# Versioned primary-attribute progression

The shipped profile format is defined by config/newHorizonsHeroes.json, its schema
and shared hero-rule validation. The detailed class table and design rationale
live in the private
[canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md);
selected legacy rationale is indexed
[privately](https://github.com/thegandalf196/new-horizons-docs/blob/main/proposals/legacy-amendments.md#primary-progression-design-record).
The generated curated module must match the authored JSON. Resolving installed
profiles for a new hero does not replace a profile already captured in a save.

## Saved profile contract

Each class profile may carry progressionVersion with starting and growth vectors,
in Attack / Defense / Spell Power / Knowledge order.

- Missing version or version 1 retains positive growth totaling10 and base
  growth × (L + 4). The stored starting vector does not replace that historical
  formula.
- Version 2 requires starting total100 and positive growth totaling18; base is
  starting + (L − 1) × growth.
- Version 3 requires starting total100, positive growth totaling20 and
  starting = 5 × growth per attribute, so deterministic base is growth × (L + 4).
  Configured extraGrowth opportunities are independent of that fixed vector.
  Skill/rank chance rows come from captured rules, not newly installed defaults.
- Unsupported versions and malformed/nonintegral ratings are rejected.
  Calculations use 64-bit intermediates and the existing primary cap when ratings
  are installed. Neither a new mutable counter nor per-update polling is needed.

The discriminator belongs inside the JSON profile already captured for each hero.
Do not add current progression or extra-roll policies to historical absent rules
during reload, campaign crossover or fixture setup. Preserve the serialized
randomizer and authoritative level-up order; a selected perk must not retroactively
change a roll taken from the pre-offer view.

Class IDs and displayed names are different concerns: Tower core:alchemist is
Battle Mage and Stronghold core:battlemage is Shaman; Solmyr is a Wizard.
Prototype class changes do not prove that historical class identity was captured.

## Source and regression ownership

The shared implementation is in lib/entities/hero/NewHorizonsPrimaryProfile.cpp,
NewHorizonsPrimaryGrowth.cpp and NewHorizonsHeroRules.cpp. Protect:

- NewHorizonsHeroRulesTest.ResolvedSnapshotDoesNotFollowChangedInstalledProfile
  and ExplicitProgressionVersionKeepsOldAndNewSnapshotsIndependent.
- NewHorizonsPrimaryGrowthTest's independent-roll boundaries and ignored legacy
  chance rows.
- Server NewHorizonsHeroGrowthTest.cpp's real initialization, authored map XP,
  Knowledge-derived mana, authoritative level-ups, randomizer serialization,
  mixed-profile world reload and campaign crossover.
- Installed-class fixtures without hero-rule overrides, including actual
  Intelligence and legal captured development rather than an assumed empty perk
  state. Pair historical hero rules with matching legacy perk/development policy.

These are regression requirements and source entry points, not a new execution
receipt. Historical version-2 native evidence cannot certify a version-3 build,
ordinary rendered starts, campaigns, Windows or a playable-snapshot promotion.

## Default adventure-map Primary Attribute rewards

`config/newHorizonsPrimaryRewards.json` owns the build-time legacy-point value
for the listed default map trainers, Scholar primary fallback and Fortress Cage
of Warlords. The curated module generator resolves their amounts and text into
registered object/building overlays. These concrete rewards are captured when
objects and towns initialize; existing saves retain their captured amounts and
visit history, including unvisited objects and unbuilt town trainers. This does
not migrate authored map rewards or hero attributes. Artifact equipment and
other authored town effects retain their separate content definitions.
