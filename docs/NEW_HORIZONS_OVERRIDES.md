# New Horizons — focused scripture overrides

This file records narrow, user-approved changes to
[`design-sources/New Horizons.docx`](design-sources/New%20Horizons.docx) without
requiring the complete source document to be replaced.

## Authority and precedence

1. An **Accepted** entry in this file overrides the source document only for the
   exact mechanic, entity, building, spell, hero, value, or presentation rule
   named by that entry.
2. Everything not explicitly changed by an Accepted entry continues to follow
   the source document and the repository contracts in
   [`NEW_HORIZONS_DESIGN.md`](NEW_HORIZONS_DESIGN.md) and
   [`NEW_HORIZONS_MVP.md`](NEW_HORIZONS_MVP.md).
3. If two Accepted entries address the same rule, the later entry supersedes the
   earlier one. Keep the older entry for history and mark it **Superseded**.
4. Draft notes, questions, implementation proposals, and agent inferences have
   no authority. Only instructions confirmed by the user may be marked
   **Accepted**.
5. An override changes the design contract; it is not proof that the change has
   been implemented. Record implementation evidence separately in the entry.

## Entry template

```markdown
### YYYY-MM-DD — Short rule name

- **Status:** Draft | Accepted | Superseded
- **Overrides:** Exact scripture section, entity, or rule.
- **Rule:** Complete replacement rule.
- **Implementation evidence:** Commit/tests, once implemented.
```

## Accepted overrides

### 2026-09-21 — Astral Nexus function

- **Status:** Accepted
- **Overrides:** The unique-building function assigned to Dungeon's Astral Nexus.
- **Rule:** Visiting heroes immediately replenish their spell points to their
  normal maximum.
- **Implementation evidence:** Commit `b8bd9ff28`; behavioral coverage in
  `NewHorizonsUniqueBuildingTrainingTest.TrainingPersistsAndAstralNexusAlwaysRefillsToNormalMaximum`.

### 2026-09-21 — House of Wisdom replaces Magic University

- **Status:** Accepted
- **Overrides:** Conflux's Magic University name and function.
- **Rule:** The building is named **House of Wisdom** and sells randomly
  generated spell scrolls instead of teaching secondary skills.
- **Implementation evidence:** Commit `b8bd9ff28`; authoritative purchase,
  insufficient-funds and save/load coverage in `NewHorizonsMagicUniversityTest`.

### 2026-09-21 — Solmyr Tower-versus-Inferno playtest profile

- **Status:** Accepted
- **Overrides:** Solmyr's New Horizons starting development and Chain Lightning
  specialty for the immediate Tower-versus-Inferno playtest milestone.
- **Rule:** New Horizons Solmyr starts with Metamagic, Havoc Magic, and the
  Stormcaller perk. He knows **Master Chain Lightning** instead of ordinary
  Chain Lightning and cannot learn the ordinary version. Master Chain Lightning
  has the same Mana cost and first-target damage as ordinary Chain Lightning,
  loses less damage on later jumps, and improves that jump retention as Solmyr
  gains levels. Legacy-mode Solmyr remains unchanged. This is the first hero in
  the new three-starting-development direction; do not silently assign arbitrary
  third choices to every other hero before their intended choices are authored.
- **Implementation evidence:** Commit `4452c273b`; Solmyr initialization and
  castability coverage in
  `NewHorizonsHalonInitializationTest.FreshSolmyrUsesMasterChainLightningAndStormcaller`,
  plus the private Linux scenario smoke recorded on 2026-09-21.

### 2026-09-21 — exhaustive meaningful battle logging

- **Status:** Accepted
- **Overrides:** Battle-log coverage and implementation priority after the
  Solmyr Tower-versus-Inferno milestone.
- **Rule:** Record meaningful combat interactions with actors, causes, targets,
  effects and results. Metamagic follow-ups must explicitly identify the hero,
  ordinal follow-up spell, triggering Metamagic effect, spell name, and resulting
  damage or other outcome. Extend the same causal standard across other combat
  mechanics rather than emitting generic New Horizons prefixes.
- **Implementation evidence:** Pending.

### 2026-09-21 — Tower class and creature presentation

- **Status:** Accepted
- **Overrides:** Tower hero-class naming and the transitional Mage/Genie dwelling order.
- **Rule:** New Horizons renames **Alchemist** to **Battle Mage**. Swap the Mage
  and Genie dwelling levels before replacing tier presentation with the
  Core/Elite/Champion grouping; both Mage and Genie belong to Elite in that
  final grouping.
- **Implementation evidence:** Pending.

### 2026-09-21 — skill-development and action-help presentation

- **Status:** Accepted
- **Overrides:** The hero class skill-odds pane, perk ownership help, and Orders
  chooser interaction.
- **Rule:** A class's skill-odds pane never lists another faction's unique Skill,
  displays Skill icons rather than a names-only list, and uses a smaller polished
  button placed close to the Skills/Learned Perks heading. Perk help also shows
  the owning Skill's icon. Stormcaller and the Movement derived attribute each
  have an appropriate visible icon. Disabled Orders remain inspectable with
  right-click; the Orders chooser uses a Heroes III-style background rather than
  a flat grey rectangle.
- **Implementation evidence:** Pending.

### 2026-09-21 — separate creature Speed and Initiative

- **Status:** Accepted
- **Overrides:** Creature turn-order and battlefield-movement use of the original
  shared Speed statistic.
- **Rule:** Speed controls battlefield movement distance; Initiative controls
  turn order. Slow reduces Initiative only, while Frost Bolt reduces Speed only.
  Creature data may therefore give upgrades such as Arch Magi higher Initiative
  without increasing their Speed. Creature UI must show both statistics.
- **Implementation evidence:** Pending.
