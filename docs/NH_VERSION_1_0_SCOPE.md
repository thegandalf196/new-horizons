# New Horizons Version 1.0 scope

## Authoritative scope clarification

The current user-supplied gameplay authority is
[New Horizons.docx](design-sources/New%20Horizons.docx). Detailed system sections
in that document override earlier summary lists and older repository planning.
The DOCX is the sole canonical design specification. Legacy Overrides is retained
only for the [transition audit](NH_OVERRIDE_MIGRATION.md), not as a precedence
layer. Temporary amendments belong in [Pending Changes](NEW_HORIZONS_PENDING_CHANGES.md).
Version 1.0 replaces the seven-tier organization with Core, Elite, and Champion.
Governors, regional administration, and Caravans are explicitly future work, not
Version 1.0 dependencies.

### Reworks

1. **Primary attributes:** class-based deterministic development and coherent
   scaled formulas/readouts.
2. **Secondary skills:** Basic/Advanced/Expert ranks with a ten-perk pool for each
   Skill. The old one-per-rank restriction conflicts with the current DOCX's
   three-per-Skill rule and is flagged for migration review; this summary must
   not resolve that conflict. A level-up may offer up
   to two Skill choices and two perk choices, from which the hero selects one.
   The full source roster and prerequisites replace the older three-ability and
   post-Expert mastery targets. Save migration remains explicit and versioned.
3. **Magic schools:** Light, Nature, Sorcery/Arcane, Havoc, Shadow and Chaos, with
   coherent spell assignments, actual effects, AI support and school/spell UI art.
   School-family artwork alone does not complete the spell system.
4. **Creature progression:** replace the seven-tier organization with
   **Core / Elite / Champion**. Category labels alone do not prove all intended
   creature/recruitment changes complete; pin the remaining faction mappings and
   actual rules before claiming this rework delivered.

### Additions

5. **Commands:** Orders, with separate combat Orders/Spellbook entry controls and
   the established shared hero-action budget. The superseded Doctrine experiment
   is explicitly outside New Horizons and must not be exposed as active gameplay.
6. **Adventure systems:** hero Movement is independent of creature combat Speed;
   Speed and Initiative are separate combat statistics; five neutral Adventure
   Spells use fixed Mage Guild I–V unlocks and a once-per-day casting limit; all
   factions reach Mage Guild V.

## Boundaries and supporting requirements

The experimental single-expeditionary-Hero restriction is not part of Version 1.0.
Governors, regional administration, anti-chaining rules, and Caravan logistics
remain future systems until their open rules are resolved. Do not create UI or
state that implies those unsettled mechanics merely to fill scope.

Siege as a spendable resource remains exploratory, not an established economy.
The user's revised Hero-screen layout and associated-ability presentation are the
UI direction; example values/icons/effects are not approved gameplay definitions.

Working AI, save integrity/explicit compatibility, native Linux and Windows product
support, clear integrated UI and required asset provenance remain cross-cutting
requirements. Fun first, balance later does not excuse crashes or inert features.
Incremental previews continue; existing previews and narrow test passes are not
Version1.0 completion. Keep current playable artifacts intact while developing
versioned replacements. This document records scope, not active Goal tooling state,
new ownership permissions, GUI execution authorization or final release approval.

This source supersedes conflicting old mastery, three-ability, governor, and
seven-tier wording for Version 1.0.
Preserve old design/history and compiled save formats until deliberate migration;
do not delete working code simply because the target design changed.
