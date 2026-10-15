# Original-feature compatibility follow-up

These source-backed distinctions prevent stale TODOs from becoming invented
gameplay fixes. Historical proprietary reference review is corroboration, not
permission to import Reconstruction code or redistribute purchaser content.

## Explicitly empty neutral-town garrisons

The old H3M reader lost the distinction between an unspecified neutral garrison
and an explicitly empty one, allowing random guards in both cases. The repaired
parser/materialization and persistence path must preserve that distinction.
Inspect `lib/mapping/MapFormatH3M.cpp`, `lib/mapObjects/CGTownInstance.cpp`,
`test/mock/TinyH3MBuilder.cpp` and `test/map/TinyH3MBuilderTest.cpp`.
Accepted focused parser/object tests and ordinary capture/reload evidence cover
the distinction; they do not establish all weekly-growth parity or every random
garrison outcome.

## Selective autocombat controls

`lib/battle/AutocombatPreferences.h` describes original creature/catapult/
ballista/first-aid preferences that remain absent rather than broken implemented
functionality. Spell/tactics controls are separate. A future implementation must
cover both `CPlayerInterface::activeStack` and
`BattleInterface::requestAutofightingAIToTakeAction`, manual UI blocking and
spell-only handling, not merely enable placeholder settings checkboxes.
Disabled unit categories must remain manual. This is deferred compatibility
breadth, not a current gameplay repair.

## Defend and spell-added defense

Current Defend applies its percentage to defense including spell bonuses.
A TODO asking to exclude Stone Skin is insufficient evidence of an original
parity defect; reviewed original behavior points the other way. Do not implement
an exclusion as a presumed fix. A supported future change would need independent
evidence and authoritative Defend/Stone Skin/Steel Skin rounding controls.

Likewise, a stale comment claiming antimagic garrisons are absent is not a gap:
`CGGarrison::initObj`/its bonus producer installs BLOCK_ALL_MAGIC and battle
casting checks consume it. Source inspection alone is not rendered acceptance.

See [integration gaps](NH_INTEGRATION_GAPS.md) and
[regression guards](NH_REGRESSION_GUARDS.md).
