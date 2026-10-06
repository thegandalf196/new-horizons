#!/usr/bin/env python3
"""Focused source guard for the New Horizons battle Morale row readback."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PANEL_CPP = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text()
PANEL_H = (ROOT / "client/battle/StackInfoBasicPanel.h").read_text()
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text()
CALLBACK_H = (ROOT / "lib/battle/CBattleInfoCallback.h").read_text()
TEXTS = (ROOT / "config/newHorizonsCombatTexts.json").read_text()

for field in (
    "available",
    "real",
    "effective",
    "standardBearerBonus",
    "firstRoundModifier",
    "steadfastAdjustment",
    "commandingPresenceFloorApplied",
    "furyUnboundFloorApplied",
    "unaffectedByMorale",
    "bonusDescriptions",
):
    assert f"{field}" in STATUS, f"Missing cached Morale readback field: {field}"

factory = STATUS.split("inline BattleMoraleReadback makeBattleMoraleReadback(", 1)[1].split(
    "struct SylvanLuckStackStatus", 1
)[0]
assert "if(!enabled)" in factory and "return {};" in factory
assert "if(unaffectedByMorale)" in factory
assert "std::move(bonusDescriptions)" in factory
assert "bool operator==(const BattleMoraleReadback &) const = default;" in STATUS
assert "int32_t real = 0;" in CALLBACK_H and "int32_t effective = 0;" in CALLBACK_H

readback = PANEL_CPP.split("newHorizonsBattleStatus::BattleMoraleReadback currentBattleMoraleReadback(", 1)[1].split(
    "std::string battleMoraleReadbackTooltip(", 1
)[0]
for token in (
    "getMagicRules()",
    "newHorizonsMagic::rulesActive",
    "battleGetMoraleInfo(stack)",
    "morale.real",
    "morale.effective",
    "morale.standardBearerBonus",
    "morale.firstRoundModifier",
    "morale.steadfastAdjustment",
    "morale.commandingPresenceFloorApplied",
    "morale.furyUnboundFloorApplied",
    "stack->unaffectedByMorale()",
    "cachedBonusDescriptions",
):
    assert token in readback, f"Missing saved-state or scoped Morale source readback: {token}"
assert "battleGetFightingHero" not in readback, "Do not require a hero to read neutral/enemy battle Morale"
assert "stack->moraleVal()" not in readback, "Use shared pre-floor Morale, not a client substitute"

source_cache = PANEL_CPP.split("void StackInfoBasicPanel::refreshBonusDescriptionCache(", 1)[1].split(
    "void StackInfoBasicPanel::initializeData(", 1
)[0]
for token in (
    "newHorizonsMagic::rulesActive",
    "stack->unitId()",
    "stack->CBonusSystemNode::getTreeVersion()",
    "GAME->interface()->cb.get()",
    "BonusType::MORALE",
    "bonus->Description(descriptionCallback)",
    "*bonusDescriptionCacheKey == cacheKey",
    "unitId, treeVersion, descriptionCallback",
):
    assert token in source_cache, f"Missing saved-state keyed Morale source cache behavior: {token}"
assert source_cache.index("*bonusDescriptionCacheKey == cacheKey") < source_cache.index("bonus->Description(descriptionCallback)"), (
    "The cache-key early return must precede source-description formatting"
)

tooltip = PANEL_CPP.split("std::string battleMoraleReadbackTooltip(", 1)[1].split(
    "newHorizonsBattleStatus::StackInfoStatusSnapshot currentStackInfoStatus(", 1
)[0]
for text_id in (
    "new-horizons.combat.morale.readback.values",
    "new-horizons.combat.morale.readback.sourceHeader",
    "new-horizons.combat.morale.readback.noSources",
    "new-horizons.combat.morale.readback.standardBearer",
    "new-horizons.combat.morale.readback.firstRound",
    "new-horizons.combat.morale.readback.steadfast",
    "new-horizons.combat.morale.readback.commandingPresence",
    "new-horizons.combat.morale.readback.furyUnbound",
):
    assert text_id in tooltip, f"Missing localized Morale readback text: {text_id}"
assert 'replaceTokenNumber("%REAL%"' in tooltip
assert 'replaceTokenNumber("%EFFECTIVE%"' in tooltip
assert 'replaceTokenNumber("%VALUE%"' in tooltip
assert 'appendTextID("core.arraytxt", 113)' in tooltip

for include in (
    '#include "../GameInstance.h"',
    '#include "../CPlayerInterface.h"',
    '#include "../../lib/callback/CCallback.h"',
):
    assert include in PANEL_CPP, f"Missing explicit GUI/callback declaration: {include}"

assert "BattleMoraleReadback displayedMoraleReadback;" in PANEL_H
assert "stack, battleCallback.get(), cachedMoraleBonusDescriptions" in PANEL_CPP
assert "displayedMoraleReadback = moraleReadback;" in PANEL_CPP
assert "Rect(7, 129, 67, 12), tooltip, tooltip" in PANEL_CPP
assert "currentMoraleReadback == displayedMoraleReadback" in PANEL_CPP
assert "Rect(7, 141, 67, 14), luckTooltip, luckTooltip" in PANEL_CPP, "Preserve the single composed Luck/Sylvan help row"
assert "sylvanLuckStackTooltip" in PANEL_CPP

for text_id in (
    "new-horizons.combat.morale.readback.values",
    "new-horizons.combat.morale.readback.sourceHeader",
    "new-horizons.combat.morale.readback.noSources",
    "new-horizons.combat.morale.readback.standardBearer",
    "new-horizons.combat.morale.readback.firstRound",
    "new-horizons.combat.morale.readback.steadfast",
    "new-horizons.combat.morale.readback.commandingPresence",
    "new-horizons.combat.morale.readback.furyUnbound",
):
    assert f'"{text_id}"' in TEXTS, f"Missing localized config entry: {text_id}"

print("PASS: saved New Horizons battle Morale readback uses shared real/effective data, scoped sources, localized text, and cached refresh")
