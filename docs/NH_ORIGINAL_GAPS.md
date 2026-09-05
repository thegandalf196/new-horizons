# Original-feature follow-up shortlist

Source inspected at `1a4b1184d31de06b6fa31717fa49f365f822ae25`; relevant
source unchanged through launcher-only `ef2fcda77`. No original/game execution
or gameplay edits performed for the initial review. The ordinary MVP journey
subsequently completed (see `NH_TESTER_RESULTS.md`). Item1 now has a native-tested
fix (see `NH_BUILD_HANDOFF.md`) and an exercised old-save/new-save compatibility
pass (see `NH_TESTER_RESULTS.md`). Focused synthetic explicit-empty GUI capture
and pre/post-capture reload also passed; populated GUI control remains pending. Item2 remains deferred; item3 is not an established defect. Source evidence
alone does not establish original-game parity.

1. **Explicitly empty neutral-town garrisons lost — high confidence omission.**
   `lib/mapping/MapFormatH3M.cpp:2827–2829` consumes `hasGarrison` without retaining
   it. `lib/mapObjects/CGTownInstance.cpp:487–526` randomizes empty neutral armies;
   its FIXME explicitly identifies the original editor's empty-garrison flag.
   Regression: extend `test/mock/TinyH3MBuilder.cpp:968` beyond hardcoded false,
   then add paired unspecified/explicit-empty initialization cases to
   `test/map/TinyH3MBuilderTest.cpp`. Preserve the distinction through save/load.
   Frontend's bounded original-reference review: original manual p17 allows
   undefended neutral-town capture but does not establish editor flag semantics.
   Original-address evidence retained in Reconstruction's analysis file
   `0049d160_004d8b30_005c0670_0050c740.asm:577–678` shows 005C0670 testing
   seed+0x19 at 005C06AB/AE. Present custom data processes seven slots (nonpositive
   counts become creature -1) and jumps at 005C0786 past neutral guard RNG;
   only absent custom data reaches the default owner/RNG branch. Associated
   `scenario_town_materialization.json` attributes target SHA-256
   `aac39e8cf7ff46fb009057bd53716f4be46a88a49b857165b8d19e04df9d3d7d`.
   Existing dynamic traces cover populated/default cases, not explicitly empty.
   This is original-address corroboration reported by Frontend, not permission to
   import Reconstruction implementation or a claim of a new original-game run.

2. **Selective autocombat controls absent — high confidence absence, intentional
   upstream deferral.** `lib/battle/AutocombatPreferences.h:16–20` documents original
   creature/catapult/ballista/first-aid controls but comments them out.
   `client/CPlayerInterface.cpp:2141–2143` supplies only spell/tactics preferences;
   `AI/BattleAI/BattleAI.cpp:170,252` consumes these supported options. Not a broken
   implemented feature. Future regression: preference routing must leave disabled
   unit categories under manual control. Frontend reports independent original
   manual p48 corroboration for separate controls, with manual war-machine control
   still skill-gated. Their source review finds four placeholder checkboxes in
   `config/widgets/settings/battleOptionsTab.json`,
   unconditional delegation in `CPlayerInterface::activeStack` and
   `BattleInterface::requestAutofightingAIToTakeAction`, and manual-UI blocking in
   `BattleWindow`. A future change needs both routing sites, manual UI, and
   spell-only handling, not just enabled checkboxes. This paragraph records the
   owner's evidence; Build has not independently read the proprietary manual.

3. **Defend includes spell-added defense — current calculation verified;
   NOT a corroborated original-feature gap.**
   `server/battles/BattleActionProcessor.cpp:180–195` applies PERCENT_TO_ALL to
   defense bonuses; TODO requests excluding spell boosts such as Stone Skin.
   `test/game/BattleSpellCastTest.cpp:1014–1025` covers Stone Skin specialties,
   not their Defend interaction. Future authoritative battle-action regression:
   Defend with/without Stone Skin/Steel Skin, including minimum-one and rounding.
   Runtime reports contrary reference evidence: Stone Skin/Prayer update the same
   defense field used by original Defend (13 + 4 gives bonus 3, total 20). That
   observation comes through Reconstruction reference review, not an independently
   verified original execution by Build. Therefore the TODO is insufficient and
   exclusion must not be implemented as a presumed fix. Remove this from actionable
   parity defects unless independent original evidence establishes a divergence.

**Do not count a stale TODO as a gap:** `lib/mapObjects/MiscObjects.cpp:964`
claims antimagic garrison effects are missing, but `initObj` and
`addAntimagicGarrisonBonus` at 1003–1022 install battle-wide BLOCK_ALL_MAGIC and
`lib/battle/CBattleInfoCallback.cpp:173` checks that bonus. Already implemented;
this inspection does not substitute for a runtime regression.

No upstream issue tracker was queried. Item1's native parser/initialization and
JSON/binary object regressions were implemented and passed after a recorded red
run; items2/3 remain unimplemented. Keep ordinary AI and essential core/vcmi
resources in the product; these observations justify no packaging removal.
