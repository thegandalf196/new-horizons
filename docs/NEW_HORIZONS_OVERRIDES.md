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

### 2026-09-23 — Reserved Gating arrival area

- **Status:** Accepted
- **Overrides:** Pending Demonic Gate placement and arrival presentation.
- **Rule:** A pending Gate visibly marks and reserves the arriving creature's
  full battlefield footprint, including both hexes for a double-wide creature.
  Other stacks cannot occupy that footprint before arrival. Use the existing
  Fire Wall flame animation as a visual marker only, without a hex outline
  (the later Pending Gate marker clarification supersedes the original outline);
  it does not deal Fire Wall damage. On successful creature arrival, remove the
  marker and play the Devil's movement sound once. Gate placement itself does
  not play the arrival sound.
- **Implementation evidence:** Shared accessibility and battle-proxy queries
  reserve the full footprint. `NewHorizonsDemonicGatingTest` covers exact arrival,
  blocked arrival retention, double-wide reservations, overlapping reservations,
  ground/flying landing exclusion, and the single Devil sound event.
  `HypotheticWallTest.PendingDemonicGateReservationsSurviveNestedBattleProxies`
  covers AI simulation forwarding. The original flame/outline renderer compiled;
  that historical result is not acceptance of the later outline-free design. Graphical
  appearance still requires in-game validation.

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
  Each card combines creature name, creature portrait,
  available count, weekly growth, and a compact statistic column. The statistic
  column must include Attack, Defense, Damage, Health, Speed, Initiative,
  Leadership Cost, and Growth. Increase the window and card height enough to
  give Initiative and Leadership Cost their own readable rows; do not compress,
  overlap, or abbreviate those statistics merely to retain the old dimensions.
  Do not show dwelling previews or dwelling names in this screen: the later
  user clarification removes them. Use the same statistic icons as the creature
  UI, not words alone. Rank is communicated by the band heading. Retain the town's Heroes III visual identity,
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
  capped healing, partial casualties, and Counterspell suppression. Elemental
  summons and Clone report the final authoritative creature identity and count;
  Clone is classified only after its construction update marks the new stack as
  a clone, and counterspelled creation emits no false outcome. Behavioral coverage is in
  `NewHorizonsMetamagicTest.FollowupLogNamesSecondAndThirdMagicArrowDamage`,
  `NewHorizonsMetamagicTest.FollowupLogReportsAffectedNonDamageOutcome`, and
  `NewHorizonsMetamagicTest.FollowupLogDoesNotCallSuccessfulObstacleSpellNoEffect`,
  plus the focused Summon and Clone outcome cases in that suite.

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

### 2026-09-23 — Magi melee penalty

- **Status:** Accepted
- **Rule:** Magi and Arch Magi do not have No Melee Penalty. Their ordinary
  shooter melee penalty applies. Their other abilities, including No Distance
  Penalty, are unchanged.

### 2026-09-23 — Art workflow for provisional assets

- **Status:** Accepted
- **Rule:** All New Horizons artwork, including provisional placeholders, uses
  the `homm3-art` art workflow. Provisional status does not waive that workflow
  or its Heroes III visual direction.

### 2026-09-23 — Pending Gate marker

- **Status:** Accepted
- **Rule:** Pending Gate hexes use the flame animation without a yellow hex
  outline. Their full reserved footprint remains impassable until arrival.

### 2026-09-23 — Metamagic grants a round-long Spell Action

- **Status:** Accepted; combat-allowance consumption timing awaits clarification.
- **Rule:** At each round's start, each hero receives one Hero Action usable
  until that round ends.
  When that Hero Action casts a spell, Metamagic can grant a separate Spell
  Action, with 1/2/3 uses per combat at Basic/Advanced/Expert rank. The Spell
  Action is usable until the round ends and can only cast a spell: it is not
  another Hero Action and cannot issue an Order. Creature actions do not force
  immediate use or discard of this Spell Action. Do not reopen the spellbook
  automatically or replace the normal Wait action with a mandatory decline.
- **Open detail:** Whether the combat allowance is consumed when the Spell
  Action is granted or when its spell is cast remains unconfirmed.

### 2026-09-23 — Shared typed action system

- **Status:** Accepted architecture direction; implementation audit required.
- **Rule:** Apply the Hero Action, Spell Action, and Order Action distinction
  consistently throughout combat. A Hero Action is the general allowance;
  a Spell Action permits spellcasting only, and an Order Action permits Orders
  only. Do not implement this distinction solely as a Metamagic UI workaround.
- **Required audit:** All grants, spending, expiry, perks, AI decisions, client
  controls, combat logs, authoritative validation, and save/load must agree.
  Creature actions must not accidentally spend a hero's typed allowances.

### 2026-09-23 — Hero combat action panel

- **Status:** Accepted
- **Rule:** The hero's combat sidebar includes a panel below Morale/Luck showing
  remaining Hero Actions, Order Actions, and Spell Actions as separate counts.
  Read these counts from authoritative battle state. A flexible Hero Action
  must not also appear in the specialized Spell or Order Action count.
  Metamagic's granted Spell Action must be visible here without automatically
  reopening the spellbook or preventing creature actions.

### 2026-09-23 — Fort category headings and Conflux layout

- **Status:** Accepted
- **Rule:** Core, Elite, and Champion headings all use the same yellow font
  color in every town, including fallback Fort category labels. Correct the
  Conflux-specific crowded layout and clipped creature previews without
  redesigning the working layouts of other towns.

### 2026-09-23 — Preserve the accepted inscribed-spell casting rule

- **Status:** Accepted; records the earlier explicit spellbook clarification.
- **Rule:** A combat spell already inscribed in a hero's spellbook is not locked
  for casting by insufficient School proficiency. This applies to every hero,
  including Solmyr's Master Chain Lightning, and does not require Spellbinder's
  Hat. It overrides the older scripture's proficiency gate for those spells.
- **Boundaries:** This grants no unknown spell and does not reactivate removed
  spells. Canonical roster eligibility, mana, action allowances, valid targets,
  and other ordinary casting restrictions still apply. Acquisition rules and
  the separate Adventure Spell system are not changed by this clarification.
- **Follow-up:** Reconcile Spellbinder's Hat's benefit with this broader rule;
  do not restore the proficiency lock merely to preserve its old distinction.

### 2026-09-23 — Different Orders coexist

- **Status:** Accepted
- **Rule:** When a perk permits additional Orders, different Orders remain active
  together for their normal durations. Issuing a new Order does not replace the
  previous Order. Preserve each Order's own targets, effects and expiration.
- **Boundaries:** This does not grant additional actions by itself or permit an
  otherwise forbidden repeat of the same Order. Perk prerequisites, usage limits,
  trigger timing and typed Order Action validation remain applicable.

### 2026-09-23 — Unified spell access and Normal/Buffer Spell Points

- **Status:** Accepted; supersedes the older Spellbinder's Hat no-grant design.
- **Rule:** [NEW_HORIZONS_SPELL_POINTS.md](NEW_HORIZONS_SPELL_POINTS.md) records
  the complete spell-access, Hat availability, Normal/Buffer pools, Knowledge,
  Intelligence, anti-equipment-swap clamping, restoration and UI requirements.
- **Mandatory reading:** That linked specification is part of this Accepted
  override, not optional background. Read it before implementing or reviewing
  spell acquisition, spellbook access, Mana costs, restoration, capacity,
  equipment effects, related AI or Spell Point presentation.
- **Core invariants:** Legitimately inscribed combat spells have no School-rank
  casting lock. Maximum Spell Points equal effective Knowledge, or
  `floor(1.30 * effective Knowledge)` with Intelligence. Normal points never
  exceed the current maximum; capacity increases do not refill them, and
  capacity reductions immediately discard excess Normal points. Buffer is
  independent, survives capacity reductions, and is spent before Normal.
  Ordinary restoration fills Normal only; Buffer grants do not refill Normal.
  Arcane Reservoir grants +50 Buffer; Magic Spring fills Normal and grants
  +25 Buffer, preserving their usage restrictions. These grant amounts remain
  balance parameters. Spellbinder's Hat supplies eligible Level 5 combat spells
  while equipped, without removing independently learned spells on unequip.
  In `310 / 460 +50`, 310 already includes the 50 Buffer; do not add it twice.
- **Priority:** Implement this as one coherent authoritative model, with AI,
  save compatibility and all resource consumers updated together. Do not
  simulate Buffer as increased maximum capacity or allow it to refill Normal.

## Conversation audit — explicit clarifications consolidated 2026-09-23

These entries record earlier explicit user instructions and the latest visual
clarifications. The date is the consolidation date, not an inferred date for
each original message. They are design requirements, not implementation claims.

### Deterministic growth and secondary-skill offers

- **Status:** Accepted
- **Rule:** Every class has a fixed four-attribute growth vector totaling ten.
  Every level adds exactly that vector; there is no primary-stat roll or
  level-10 transition. Before other bonuses, `Attribute(L) = growth * (L + 4)`.
  Knight's example is `3 / 4 / 1 / 2`. Preserve the authored class vectors,
  rather than replacing them with low/high-level probability tables.
  Secondary-skill selection uses the authored class weights, without the
  original forced Wisdom or magic-school offer cadence. Wisdom remains a Skill.
  Solmyr is a Wizard, not a Battle Mage; use the Wizard table for his offers.
- **Boundary:** An observed run without Wisdom is not by itself evidence of
  incorrect probabilities. Validate the actual eligible pool and weighted draw.

### Perk ranks, missing choices, and empty slots

- **Status:** Accepted
- **Rule:** Each Skill permits at most one Basic, one Advanced, and one Expert
  perk. Once a hero chooses a perk of one rank, other perks of that same rank
  from that same Skill must not be offered. This restriction does not suppress
  that rank's choices in other Skills. Advancing a Skill through a teacher must
  not silently erase an unchosen lower-rank perk opportunity.
  Unfilled perk slots look empty, not like an "Unbound" perk. Perk help identifies
  both the owning Skill's name and its icon, including level-up right-click help.
- **Boundary:** The reported teacher-induced skipped choice requires runtime
  verification; this entry does not claim it is fixed.

### All skill-teaching sources use the current learning rules

- **Status:** Accepted
- **Rule:** Every source that teaches a Skill asks whether the hero wants to
  learn it. If already known, it can advance to Advanced or Expert as applicable;
  an Expert hero receives an explanation instead of a redundant grant.
  Apply current New Horizons eligibility and the live Skill registry consistently
  to Witch Huts, Scholars/teachers, universities and unique buildings; do not
  leak retired vanilla skills. A later building-specific replacement, such as
  House of Wisdom selling scrolls, takes precedence over generic teaching rules.

### Hero initialization and stale spells

- **Status:** Accepted
- **Rule:** Audit starting Skills, spells and specialties together when their
  vanilla mechanic is removed or replaced. Halon's removed Mysticism must not
  leave him with a missing starting Skill or an obsolete specialty. Apply the
  current spell roster to hero starts/specialties, map teachers, Mage Guilds and
  other acquisition sources, not just the spellbook display. Starting armies
  must respect the hero's Leadership rules. The three-starting-development
  direction applies to all heroes eventually, with Solmyr implemented first.
- **Boundary:** Reports naming Cure, Shield, Stone Skin or Haste are audit
  requests, not permission to remove a spell that the controlling detailed
  scripture retains. Verify membership and revised behavior separately; see
  [NH_USER_FEEDBACK.md](NH_USER_FEEDBACK.md). Unspecified replacement specialties
  and starting choices must be authored, not presented as user-approved facts.

### Leadership displays

- **Status:** Accepted
- **Rule:** Beside the hero's current Leadership total, show `+x` for the amount
  gained per level, analogous to primary-attribute growth. Artifact and other
  bonuses contribute to the total; they are not what this `+x` represents.
  In the creature window, Leadership Cost is a full stat row like Attack and
  Defense, with room for its icon. Also display current stack size / maximum
  commandable count, such as `5 / 11`. Keep per-creature cost distinct from
  total-stack cost if both are shown; do not confuse either with that count.

### Creature rank row and latest icon direction

- **Status:** Accepted
- **Rule:** Core / Elite / Champion is a horizontal creature-stat row like
  Attack, not a floating badge. Its icon is a simple yellow monochrome ascending
  stair-step line, as in the user's final shape reference, NOT a ladder with
  rails and rungs. Creature Leadership uses a simple yellow monochrome crown.
  Preserve readable icon/label/value spacing and the existing stat-row style.
- **Art status:** Generated drafts are not automatically approved or installed.
  The hero Movement and hero Leadership replacement-icon requests remain active;
  the generated winged boot and command banner are proposals, not final art.

### Skills probability control and familiar panel styling

- **Status:** Accepted
- **Rule:** Replace the obsolete class-growth button with the skill-probability
  control beside the Skills / learned perks heading. The latest redraw specifies
  a small gold circled serif `i`, close to the heading, with leather showing
  through and no rectangular button background. The pane shows Skill icons and
  excludes other factions' unique Skills. The hero combat action/status panel
  must use the surrounding leather texture, red outlines and gold detailing,
  with legible spacing; a bare rectangular box is not accepted visual completion.

### Spellbook inspection and casting dialogs

- **Status:** Accepted
- **Rule:** The player can open and inspect the spellbook after casting actions
  are exhausted; inspection does not permit an illegal cast. Center the Magic
  Arrow Mana/Overcharge calculation dialog on the screen. Metamagic must never
  automatically reopen the spellbook or require a repurposed Wait button to
  clear a pending mandatory follow-up.

### Orders targeting, indication, animations, and art

- **Status:** Accepted
- **Rule:** Targeted Orders such as Flank are selected in the Orders interface,
  then aimed by clicking the stack on the battlefield, not by choosing that
  stack in a separate menu. Active Orders have visible creature-window effect
  indicators, but are not thereby made dispellable spells or subject to spell
  duration modifiers. Heroes likewise need visible active-effect indications,
  including Warcasting, in addition to their typed-action counts.
  Orders remain right-click inspectable when unavailable. Their chooser uses
  the game's textured visual style, not a flat grey rectangle. The combat Orders
  button uses a readable monochrome yellow gauntlet consistent with the spellbook
  control; do not accept clipped or partially visible art.
  Reuse suitable existing animations for Orders with distinct animation choices,
  not repeated animations across different Orders. The mounted celebration and
  former Mirth effect were suggestions; exact per-Order assignments were delegated
  to implementation judgment rather than fixed by those examples.

### Meaningful logs and direct wording

- **Status:** Accepted
- **Rule:** Battle logs explain actual numerical outcomes and causes, including
  how much damage an Order or perk adds or prevents. A Charge/Warcasting message
  that only says "gains damage" is insufficient: expose the applicable amount
  or formula and conditions, then log realized outcomes when the attack resolves.
  Use direct Heroes III-style language; omit repetitive "New Horizons:" prefixes
  and obsolete "legacy artillery" labels from live player-facing presentation.
  Attribute mitigation to the actual mechanic; user examples do not redefine
  Brace or other Orders' canonical effects.

### Art coverage and status tracking

- **Status:** Accepted
- **Rule:** Provide role-appropriate provisional art for new Skills, perks,
  Orders and derived attributes including Leadership and Siege. Transfigure
  Matter uses Remove Obstacle's spell icon. Maintain the UI/asset register across
  the complete live content, not only the user's examples, with Not done,
  Provisional and Final classifications. An unrelated borrowed icon is Not done,
  not a finished provisional design. The six school bookmarks are user-classified
  Final; Metamagic's prior art is Provisional. All new art, even provisional art,
  uses the HoMM3 Art skill. Preserve reference and approval evidence separately
  from runtime installation and functionality checks.

## Approved replacement primary-attribute table — 2026-09-23

- **Status:** Accepted; canonical JSON activation and native regression checks implemented. Playable promotion is tracked separately in `NH_PRIMARY_PROGRESSION.md`.
- **Rule:** Store the class profiles in editable JSON. Attributes are deterministic:
  `Attribute(L) = starting + (L - 1) * growth`, before other bonuses.
  This supersedes the earlier `growth * (L + 4)` rule and ten-point growth total.
  Every class starts with 100 total points and gains 18 each level. No level-10
  transition or probability roll. Solmyr remains a Wizard.
- **Order:** Attack / Defense / Spell Power / Knowledge.

| Faction | Class | Type | Starting | Growth |
| --- | --- | --- | --- | --- |
| Castle | Knight | Might | 30 / 45 / 10 / 15 | 6 / 7 / 2 / 3 |
| Castle | Cleric | Magic | 10 / 15 / 30 / 45 | 2 / 3 / 6 / 7 |
| Rampart | Ranger | Might | 35 / 35 / 15 / 15 | 6 / 6 / 3 / 3 |
| Rampart | Druid | Magic | 5 / 10 / 30 / 55 | 1 / 2 / 6 / 9 |
| Tower | Battle Mage | Might | 30 / 20 / 20 / 30 | 5 / 4 / 4 / 5 |
| Tower | Wizard | Magic | 5 / 5 / 45 / 45 | 1 / 1 / 8 / 8 |
| Inferno | Tyrant | Might | 55 / 20 / 20 / 5 | 9 / 4 / 4 / 1 |
| Inferno | Cultist | Magic | 20 / 5 / 50 / 25 | 4 / 1 / 8 / 5 |
| Necropolis | Death Knight | Might | 45 / 20 / 30 / 5 | 7 / 4 / 6 / 1 |
| Necropolis | Necromancer | Magic | 5 / 20 / 50 / 25 | 1 / 4 / 8 / 5 |
| Dungeon | Overlord | Might | 50 / 25 / 20 / 5 | 8 / 5 / 4 / 1 |
| Dungeon | Warlock | Magic | 15 / 5 / 60 / 20 | 3 / 1 / 10 / 4 |
| Stronghold | Barbarian | Might | 55 / 35 / 5 / 5 | 9 / 7 / 1 / 1 |
| Stronghold | Shaman | Magic | 45 / 5 / 30 / 20 | 7 / 1 / 6 / 4 |
| Fortress | Beastmaster | Might | 35 / 55 / 5 / 5 | 7 / 9 / 1 / 1 |
| Fortress | Witch | Magic | 5 / 15 / 20 / 60 | 1 / 3 / 4 / 10 |
| Conflux | Planeswalker | Might | 35 / 20 / 30 / 15 | 6 / 4 / 5 / 3 |
| Conflux | Elementalist | Magic | 5 / 5 / 60 / 30 | 1 / 1 / 10 / 6 |

Keep existing saved profiles explicit; changing JSON must not silently rewrite
an ongoing game's captured class rules. Update validation, UI and new-game
generation together when installing this table.

## Skill perk browser — 2026-09-23

- **Status:** Accepted; source implementation and native client build verified
  on 2026-09-24. Rendered layout and interaction acceptance remain pending.
- **Rule:** Left-clicking a skill in the hero screen opens a read-only browser
  of all its perks, showing names and icons. Right-clicking any listed perk
  shows the same explanation used by that perk in the existing hero UI,
  including perks the hero has not learned. Browsing never acquires a perk.
- **Presentation:** Group by Basic, Advanced and Expert and distinguish learned
  perks from unlearned ones. Use the game's existing leather, borders and text
  conventions, with no new unrelated placeholder artwork.
