#!/usr/bin/env python3
"""Source bindings for town Mana refill help, not rendered/runtime acceptance."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
helper = (ROOT / "client/windows/NewHorizonsBuildingVisitHelp.h").read_text()
town = (ROOT / "client/windows/CCastleInterface.cpp").read_text()

unlimited = helper.split("if(building.configuration.visitMode == Rewardable::VISIT_UNLIMITED)", 1)[1].split(
    "bool visited = false;", 1
)[0]
for token in (
    "!hero", "areSpellPointsInitialized()", "spellPointRulesActive(hero->getMagicRules())",
    "building.configuration.notVisitedTooltip.empty()", "getNormalSpellPoints()",
    "manaLimit()", "getBufferSpellPoints()", "static_cast<int64_t>(maximum) - normal",
):
    assert token in unlimited, f"Missing read-only refill status binding: {token}"
for token in ("NORMAL", "MAXIMUM", "RESTORED", "BUFFER"):
    assert f'replaceTokenNumber("%{token}%"' in unlimited
for mutation in ("setMana", "restoreNormal", "initializeSpellPoints", "grantBuffer", "onHeroVisit"):
    assert mutation not in unlimited

consumer = town.split("static MetaString getNewHorizonsBuildingVisitStatus(", 1)[1].split(
    "struct UpgradableSlotsResult", 1
)[0]
for token in (
    "town->hasBuilt(building)", "newHorizonsMagic::rulesActive(callback.getMagicRules())",
    "town->getVisitingHero()", "town->getGarrisonHero()",
    "newHorizonsBuildingVisitHelp::heroVisitStatus(*found->second, hero)",
):
    assert token in consumer
assert town.count("getNewHorizonsBuildingVisitStatus(") >= 3
print("PASS: unlimited building Mana status is read-only and uses existing saved-rule town help (not rendered acceptance)")
