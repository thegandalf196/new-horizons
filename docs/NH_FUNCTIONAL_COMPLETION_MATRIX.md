# New Horizons implementation coverage

Current phase: Phase 2 — stabilization and integration. The
[Phase 1 exit audit](NH_PHASE1_EXIT_AUDIT.md) records the decision and its limits.
The withdrawn 0.85 preview is not a current release candidate.

## Measured catalogue and scope

The canonical specification supplies 31 Skills, 93 rank entries and 310 perk
entries. These are catalogue denominators, not blanket gameplay verification.
Recompute registry states from `config/newHorizonsPerks.json` after changes;
an active flag alone does not establish effects, AI, persistence or presentation.

Coverage must include Orders, combat and Adventure spells, generic/faction Skills
and perks, Hero Actions and Creature Activations, Leadership, Siege/War Machines,
Luck/Morale, creatures, towns/buildings, artifacts/specialties, recruitment,
Diplomacy, UI, save representation and minimum AI consumers. Do not infer an
overall completion percentage from test count or use dated per-Skill counts as
current status.

## Maintained evidence homes

- [Content source map](developers/NewHorizonsContent.md): registration, principal
  consumers and focused test families.
- [Phase 1 exit audit](NH_PHASE1_EXIT_AUDIT.md): bounded implementation reconciliation.
- [Integration gaps](NH_INTEGRATION_GAPS.md): unresolved cross-system evidence.
- [Regression guards](NH_REGRESSION_GUARDS.md): protected failures and invariants.
- [Orders](NH_COMMAND_ACCEPTANCE.md) and [content](NH_CONTENT_ACCEPTANCE.md):
  current acceptance contracts.
- [Asset status](NH_UI_ASSET_STATUS.md): selection/approval and binding coverage,
  distinct from mechanics and rendered acceptance.

Source implementation, native verification, rendered acceptance and playable
delivery are separate states. No category-wide certification follows merely
from a principal-path audit. Historical run diaries and raw receipts are retained
privately rather than appended to this contributor-facing index.
