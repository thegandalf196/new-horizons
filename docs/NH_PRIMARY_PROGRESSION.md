# Versioned primary-attribute progression

The accepted replacement table is in the canonical
[private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md). The legacy 18-point
proposal and its disposition are documented in
[NH_OVERRIDE_MIGRATION.md](NH_OVERRIDE_MIGRATION.md). The canonical
`config/newHorizonsHeroes.json` now contains all 18 approved profiles with
explicit progression version 3. The generated curated module
embeds the same data. This changes newly initialized games, not profiles already
captured in existing saves. Local playable-snapshot promotion remains a separate
validation step.

## Saved profile contract

Each class profile may carry `progressionVersion` alongside its `starting` and
`growth` arrays, in Attack / Defense / Spell Power / Knowledge order.

- Missing version or version 1 preserves historical behavior: positive growth
  entries total 10, and the base at level L is `growth * (L + 4)`. The stored
  starting vector remains available but does not change that historical formula.
- Version 2 requires a starting total of 100 and positive growth entries totaling
  18. Its base is `starting + (L - 1) * growth`.
- Version 3 requires a starting total of 100, positive growth entries totaling
  20, and `starting = 5 * growth` for every attribute. Its deterministic base is
  therefore `growth * (L + 4)`. Independently, the configured `extraGrowth`
  entries grant +1 Attack from Offense or Archery, +1 Defense from Armorer,
  +1 Spell Power from Spellcraft, or +1 Knowledge from Wisdom at
  10%/20%/30% for Basic/Advanced/Expert rank. These bonuses do not alter the
  fixed class vector or the captured progression profile.
- Unsupported versions and malformed/nonintegral ratings are rejected. Arithmetic
  uses 64-bit intermediates; the hero rules' existing primary cap still applies
  when ratings are installed on a hero.

The discriminator lives inside the JSON profile already captured in each hero's
saved rules. It adds no independent mutable state or per-update polling. Resolving
a new installed profile must not rewrite an older hero's captured profile. Hero
class identifiers remain stable: Tower `core:alchemist` is displayed as Battle
Mage; Stronghold `core:battlemage` is displayed as Shaman; Solmyr remains a Wizard.

## Historical version-2 integration coverage

The version-2 Knight fixture passed native checks for real map initialization
and Knowledge-derived mana, authored map experience, serialized randomizer growth,
and authoritative level-up gains. A mixed-version world also passed full binary
save/load and campaign crossover checks with a version-2 Knight and an unversioned
legacy Cleric retaining their respective profiles. Existing version-1 fixtures
remain separate and unchanged.

An additional installed-data fixture covers all 18 classes at authored levels
1, 2 and 20 without hero-rule or primary-rating overrides. Its level-1 case also
exercises authoritative level-ups for all 18 classes, followed by full binary
reload. Solmyr is explicitly selected as the Wizard representative. Mana checks
account for genuine Intelligence perks instead of assuming every hero lacks one.
These native checks do not establish rendered UI acceptance.

The historical version-2 evidence does not certify every later version-3 build or rendered UI. Verify current integrated growth/save/campaign/AI gates and displayed starts before broad acceptance. Explicit legacy fixtures must pair legacy development/perk policy rather than merge current discriminators into absent saved rules.
