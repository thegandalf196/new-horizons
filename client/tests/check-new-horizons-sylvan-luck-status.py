#!/usr/bin/env python3
"""Focused wiring guard; native presentation tests cover state transitions."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text()
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text()

ROWS = (ROOT / "client/windows/SylvanFactionStatusPresentation.h").read_text()
readback = ROWS.split("currentSylvanLuckStatus(", 1)[1].split("/// The existing creature bonus list", 1)[0]
for token in (
    "isOrdinaryCreatureAttacker(stack)",
    "battleGetOwner(stack)",
    "battleGetFightingHero(ownerSide)",
    "usesNewHorizonsBattleRules(hero)",
    "getPerkSkillRank(std::string(SYLVAN_LUCK_SKILL_KEY))",
    "getSylvanLuckState(ownerSide)",
    "battleGetAttackLuck(stack, nullptr, false, false)",
):
    assert token in readback, f"Missing readback/privacy gate: {token}"

assert "result.sylvanLuck = newHorizonsSylvanFactionPresentation::currentSylvanLuckStatus(stack, battleCallback, luckReadback);" in PANEL
assert "SylvanLuckStackStatus sylvanLuck;" in STATUS
assert "bool operator==(const StackInfoStatusSnapshot &) const = default;" in STATUS
assert "if(current == displayedStatus" in PANEL
assert "if(displayedStatus.sylvanLuck.active())" in PANEL
assert "newHorizonsSylvanTactics::enabled(hero)" in readback
assert '\"Sylvan Luck\",' in PANEL, "Legacy status title must not read the renamed installation registry"
assert "Rect(7, 141, 67, 14), luckTooltip, luckTooltip" in PANEL

CREATURE = (ROOT / "client/windows/CCreatureWindow.cpp").read_text()
for query in ("rangedProtectionContributions(battle, unit)", "speedContributions(battle, unit)",
              "battleSylvanTacticsInitiativePercent(unit)", "battleGetFightingHero(side)"):
    assert query in ROWS, f"Missing shared current-status/privacy query: {query}"
BUILDER = (ROOT / "client/windows/CreatureBonusStatusPresentation.cpp").read_text()
assert "currentRows(*battle, battleStack)" in BUILDER
assert "creatureBonusStatusPresentation::supplementalRows(" in CREATURE
assert "activeBonuses.push_back(std::move(status))" in CREATURE
assert "15" not in ROWS and "20" not in ROWS, "UI must not duplicate rule values"
print("PASS: synchronized current-controller Sylvan Luck state reaches existing Luck help and refresh snapshot")

NAMES = (ROOT / "client/windows/NewHorizonsSkillPresentation.h").read_text()
PERK_HELP = (ROOT / "client/windows/NewHorizonsPerkHelp.h").read_text()
PURE = (ROOT / "test/hero/NewHorizonsSkillPresentationTest.cpp").read_text()
assert 'skillId == "new-horizons:shroudOfMalassa" || captured.empty() ? installed : captured' in NAMES
assert "newHorizonsSkillPresentation::perkSkillName(skillId," in PERK_HELP
assert "toEntity(LIBRARY)->getNameTranslated(), capturedName" in PERK_HELP
assert PERK_HELP.index("capturedSkillName(hero->getPerkState().rules)") < PERK_HELP.index("perkSkillName(skillId,")
assert "LegacyDungeonDisplayRenameKeepsOtherCapturedNames" in PURE

CURRENT = ROWS.split("inline std::vector<std::string> currentRows(", 1)[1]
FORMAT = ROWS.split("inline std::vector<std::string> formatCurrentRows(", 1)[1].split("/// The existing creature bonus list", 1)[0]
assert CURRENT.index("rangedProtectionContributions(battle, unit)") < CURRENT.index("battleGetFightingHero(side)")
assert "return formatCurrentRows(observedProtection, ownerSpeed, ownerPerksVisible," in CURRENT
assert "if(ownerPerksVisible)" in CURRENT and "ownerSpeed = newHorizonsSylvanTactics::speedContributions" in CURRENT
assert FORMAT.index("for(const auto & [name, percent] : observedProtection)") < FORMAT.index("if(ownerPerksVisible)")
NATIVE_STATUS = (ROOT / "test/battle/SylvanLuckStatusPresentationTest.cpp").read_text()
assert "ObservedGuardSurvivesNonTacticsOwnerWithoutCatalogueLeak" in NATIVE_STATUS
assert "formatCurrentRows(observedGuard, privateSpeed, false, 0)" in NATIVE_STATUS
assert "formatCurrentRows({}, privateSpeed, false, 0).empty()" in NATIVE_STATUS
