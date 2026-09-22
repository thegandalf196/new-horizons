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

### 2026-09-22 — Replace Metamagic's countered-spell perk

- **Status:** Accepted
- **Overrides:** Metamagic's Advanced **Spell Buffer** perk.
- **Rule:** Replace Spell Buffer and its Counterspell-dependent trigger with
  **Spell Echo**. When the additional Spell repeats the first Spell in its
  Metamagic sequence, that additional Spell gains +25% to its Spell
  Power-derived numerical component. This creates a deliberate repeat-casting
  path and does not depend on an enemy knowing, arming, or successfully using
  Counterspell.
- **Implementation evidence:** The live perk registry and shared spell-mechanics
  path implement Spell Echo, with data/runtime coverage in
  NewHorizonsMetamagicTest.SpellEcho*. JSON and binary save migration is covered
  by NewHorizonsPerkState.RetiredSpellBufferMigratesToSpellEchoAcrossJsonAndBinaryLoads.

### 2026-09-22 — Core, Elite, and Champion recruitment layout

- **Status:** Accepted
- **Overrides:** The town creature-recruitment screen's original seven-tier
  presentation and any tabbed rank-selection proposal.
- **Rule:** Present the complete town roster in three simultaneously visible
  horizontal rank bands: **Core**, **Elite**, and **Champion**. The supplied Fort
  reference uses three equal Core cards, three equal Elite cards, and one
  Champion card centered in its own lower band. Other factions retain every
  authored creature row in its assigned band; the UI must adapt the row layout
  rather than dropping, merging, or duplicating creatures to force a universal
  3/3/1 count.
  Each card combines creature name, dwelling preview, creature portrait,
  available count, weekly growth, and a compact statistic column. The statistic
  column must include Attack, Defense, Damage, Health, Speed, Initiative,
  Leadership Cost, and Growth. Increase the window and card height enough to
  give Initiative and Leadership Cost their own readable rows; do not compress,
  overlap, or abbreviate those statistics merely to retain the old dimensions.
  Rank is communicated by the band heading rather than repeated as text over
  individual dwelling images. Retain the town's Heroes III visual identity,
  resource bar, date, and confirmation control.
- **Implementation evidence:** Commit `704f5e98d` implements the adaptive
  simultaneous rank bands, full authored-roster preservation, eight-stat cards,
  localized headings and compact geometry for both 3/3/1 and 4/3/1 rosters.
  `check-new-horizons-recruitment-category-ui.py` covers the data/UI contract.

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
- **Implementation evidence:** Partial. Metamagic follow-up casts are described by the
  authoritative spell-mechanics path after their effects resolve. The log names
  the casting hero, second/third sequence position, spell, Metamagic cause, and
  damage, kills, resistance, affected-target count, ward creation, or successful
  non-unit resolution as applicable. Direct and area damage use the authoritative
  injury packets to name every affected stack with exact damage and pre-Rebirth
  casualties in packet order. Timed effects and Dispels compare authoritative
  whole-cast bonus state, reporting applications, refreshes, removals, durations,
  mixed damage/status results, or an actual no-op; counterspelled effects cannot
  mutate the target. Healing and Resurrection report realized restored health and
  resurrected creature counts from authoritative unit-state updates, including
  capped healing, partial casualties, and Counterspell suppression. Behavioral coverage is in
  `NewHorizonsMetamagicTest.FollowupLogNamesSecondAndThirdMagicArrowDamage`,
  `NewHorizonsMetamagicTest.FollowupLogReportsAffectedNonDamageOutcome`, and
  `NewHorizonsMetamagicTest.FollowupLogDoesNotCallSuccessfulObstacleSpellNoEffect`.

### 2026-09-21 — Tower class and creature presentation

- **Status:** Accepted
- **Overrides:** Tower hero-class naming and the transitional Mage/Genie dwelling order.
- **Rule:** New Horizons renames **Alchemist** to **Battle Mage**. Swap the Mage
  and Genie dwelling levels before replacing tier presentation with the
  Core/Elite/Champion grouping; both Mage and Genie belong to Elite in that
  final grouping.
- **Implementation evidence:** `Mods/new-horizons/Content/config/creatures/tower.json`
  swaps Mage/Genie creature levels and applies the scripture's prototype combat,
  growth, shooting, and recruitment-cost profiles, while
  `Mods/new-horizons/Content/config/factions/towerCreatureRanks.json` swaps the
  recruitment rows, dwelling presentation, and Library prerequisites as one
  coherent town definition. The live module version is `0.8.0` so the roster
  change is not silently accepted by older New Horizons saves.

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
- **Implementation evidence:** Creature JSON accepts an optional independent
  `initiative`; battle units use it for turn order and otherwise retain the
  legacy Speed fallback. `STACKS_MOVEMENT_RANGE` lets Frost Bolt alter movement
  without altering Initiative, while New Horizons Slow uses
  `STACKS_INITIATIVE`. Mage and Arch Mage share Speed 5, with Initiative 5 and
  7 respectively. `CStackWindow` renders distinct Speed, Initiative, and
  Leadership Cost rows in the taller New Horizons creature panel.

### 2026-09-22 — Demonic Reserve and Gating interaction

- **Status:** Accepted
- **Overrides:** Any interpretation of Gating as a free summon or passive proc.
- **Rule:** Inferno heroes own a persistent Demonic Reserve outside the seven
  active army slots. From the hero screen, selected Inferno troops can be moved
  into that reserve and withdrawn later. In battle, an active friendly Inferno
  stack may spend its Creature Activation to choose an eligible reserve stack
  and a legal empty Gate hex. The whole selected reserve stack arrives at the
  next round boundary. Its casualties are permanent; survivors return to the
  reserve after combat. Basic permits Core, Advanced adds Elite, and Expert adds
  Champion. The server owns all validation and state transitions, and BattleAI
  must understand the action.
- **Implementation evidence:** Persistent reserve and transfer foundation in
  commit `9b0ba2f3b`; full battle action, delayed arrival, survivor reconciliation,
  hero/battle controls, battle log, BattleAI choice, and behavioral coverage are
  implemented in the following Demonic Gating checkpoint.

### 2026-09-22 — First Demonic Gating perk tranche

- **Status:** Accepted
- **Rule:** Swift Gate resolves a pending Gate at the end of the current round,
  before the authoritative round counter advances. Wide Gate changes the legal
  placement radius from three to five hexes for the player, server validator,
  and Battle AI. Hellfire Arrival deals Fire damage equal to 15% of the arriving
  stack's current aggregate HP, divided evenly among adjacent enemy stacks.
- **Implementation evidence:** The authoritative round-boundary resolver now has
  distinct end-of-round and start-of-round phases; placement range is perk-aware
  in all three consumers; arrival publishes direct injury and battle-log packets.
  Focused live-content tests cover both sides of Wide Gate authority, Swift timing,
  exact Hellfire damage, base arrival, invalid-action atomicity, reserve transfers,
  and wire compatibility.

### 2026-09-22 — Second Demonic Gating perk tranche

- **Status:** Accepted
- **Rule:** Infernal Beacon grants exactly +2 flat Initiative during an arriving
  stack's first actionable round when its Gate is adjacent to another friendly
  Inferno stack. Reserve Discipline floors negative Morale at 0 only during that
  arrival round; it does not suppress positive Morale. Endless Legion restores
  half of gated Core and Elite casualties after victory, rounded down, and never
  restores Champion casualties or losses suffered by the defeated side.
- **Implementation evidence:** Reusable `STACKS_INITIATIVE_FLAT` and
  `MINIMUM_MORALE` timed bonuses preserve speed/initiative separation and ordinary
  positive Morale. The Gate resolver applies them with duration adjusted for
  ordinary versus Swift arrival timing. Post-battle reserve reconciliation uses
  each gated stack's authoritative initial and surviving counts and the saved
  creature category. Live-content integration tests cover Beacon and Reserve
  Discipline; exact Endless Legion rounding has direct rules coverage.

### 2026-09-22 — Reinforced Gate temporary health

- **Status:** Accepted
- **Rule:** Reinforced Gate grants the arriving reserve stack battle-only hit
  points equal to 20% of its aggregate health at arrival, rounded down. These
  points are consumed before creature health, never create creatures, cannot be
  healed or resurrected, survive battle-state serialization, and disappear with
  the battle.
- **Implementation evidence:** The battle health ledger carries an explicit
  temporary-hit-point pool. Authoritative arrival state grants and publishes the
  pool through `BattleUnitsChanged`; ordinary damage consumes it first while the
  creature count and post-battle reserve reconciliation remain unchanged.

### 2026-09-22 — Mobile Gate combined action

- **Status:** Accepted
- **Rule:** Mobile Gate uses one combined Creature Activation. The player first
  chooses a legal movement destination at a path distance no greater than half
  the stack's current Movement Range, rounded down, and then places the Gate from
  that destination. Choosing the current hex performs an ordinary stationary
  Gate. The action never grants a second activation.
- **Implementation evidence:** The submitted action carries the movement hex and
  Gate hex as two ordered targets. The server validates the complete request,
  moves the stack authoritatively, revalidates placement from its actual ending
  hex, and only then transfers the selected reserve stack into the pending Gate.
  An invalid request changes neither position, reserve, nor activation state;
  an in-path battlefield interruption may consume movement/activation but never
  consumes reserve troops unless the Gate remains legal from the actual endpoint.
