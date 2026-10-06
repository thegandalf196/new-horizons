#!/usr/bin/env python3
"""Focused source guard for the New Horizons compact battle Luck readback."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PANEL_CPP = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text()
PANEL_H = (ROOT / "client/battle/StackInfoBasicPanel.h").read_text()
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text()
TEXTS = (ROOT / "config/newHorizonsCombatTexts.json").read_text()

for field in (
    "available",
    "ordinaryAttackLuck",
    "noLuck",
    "maxLuck",
    "maximumLuckLimitPresent",
    "maximumLuckLimit",
    "bonusDescriptions",
):
    assert field in STATUS, f"Missing cached Luck readback field: {field}"

readback = STATUS.split("struct BattleLuckReadback", 1)[1].split(
    "struct SylvanLuckStackStatus", 1
)[0]
assert "bool operator==(const BattleLuckReadback &) const = default;" in readback
assert "bool active() const { return available; }" in readback
assert "bool hasSources() const { return !bonusDescriptions.empty(); }" in readback
factory = STATUS.split("inline BattleLuckReadback makeBattleLuckReadback(", 1)[1].split(
    "struct SylvanLuckStackStatus", 1
)[0]
assert "if(!enabled)" in factory and "return {};" in factory
assert "if(noLuck || maxLuck)" in factory and "bonusDescriptions.clear();" in factory
assert "maximumLuckLimitPresent" in factory and "maximumLuckLimit" in factory

current = PANEL_CPP.split("newHorizonsBattleStatus::BattleLuckReadback currentBattleLuckReadback(", 1)[1].split(
    "newHorizonsBattleStatus::BattleMoraleReadback currentBattleMoraleReadback(", 1
)[0]
for token in (
    "getMagicRules()",
    "newHorizonsMagic::rulesActive",
    "BonusType::NO_LUCK",
    "BonusType::MAX_LUCK",
    "BonusType::MAXIMUM_LUCK",
    "battleGetAttackLuck(stack, nullptr, false, false)",
    "cachedBonusDescriptions",
):
    assert token in current, f"Missing saved-state/scoped Luck readback behavior: {token}"
assert "battleGetFightingHero" not in current, "The readback must work for neutral/enemy stacks without a hero query"
assert "stack->luckVal()" not in current, "Use the shared battle query as the New Horizons value authority"

source_cache = PANEL_CPP.split("void StackInfoBasicPanel::refreshBonusDescriptionCache(", 1)[1].split(
    "void StackInfoBasicPanel::initializeData(", 1
)[0]
for token in (
    "newHorizonsMagic::rulesActive",
    "stack->unitId()",
    "stack->CBonusSystemNode::getTreeVersion()",
    "GAME->interface()->cb.get()",
    "BonusType::LUCK",
    "bonus->Description(descriptionCallback)",
    "*bonusDescriptionCacheKey == cacheKey",
    "unitId, treeVersion, descriptionCallback",
    "BonusType::NO_LUCK",
    "BonusType::MAX_LUCK",
):
    assert token in source_cache, f"Missing saved-state keyed Luck source cache behavior: {token}"
assert source_cache.index("*bonusDescriptionCacheKey == cacheKey") < source_cache.index("bonus->Description(descriptionCallback)"), (
    "The cache-key early return must precede source-description formatting"
)
assert "struct BattleBonusDescriptionCacheKey" in STATUS
assert "bool operator==(const BattleBonusDescriptionCacheKey &) const = default;" in STATUS

tooltip = PANEL_CPP.split("std::string battleLuckReadbackTooltip(", 1)[1].split(
    "newHorizonsBattleStatus::StackInfoStatusSnapshot currentStackInfoStatus(", 1
)[0]
for text_id in (
    "new-horizons.combat.luck.readback.value",
    "new-horizons.combat.luck.readback.sourceHeader",
    "new-horizons.combat.luck.readback.noLuck",
    "new-horizons.combat.luck.readback.maxLuck",
    "new-horizons.combat.luck.readback.maxLuckNoLuck",
    "new-horizons.combat.luck.readback.maximumLuckLimit",
    "new-horizons.combat.luck.readback.noSources",
):
    assert text_id in tooltip, f"Missing localized Luck readback text: {text_id}"
assert 'replaceTokenNumber("%VALUE%", status.ordinaryAttackLuck)' in tooltip
assert 'replaceTokenNumber("%VALUE%", status.maximumLuckLimit)' in tooltip
assert "status.maximumLuckLimitPresent && (!status.noLuck || status.maxLuck)" in tooltip
assert "appendRawString(description)" in tooltip

for text_id in (
    "new-horizons.combat.luck.readback.value",
    "new-horizons.combat.luck.readback.sourceHeader",
    "new-horizons.combat.luck.readback.noLuck",
    "new-horizons.combat.luck.readback.maxLuck",
    "new-horizons.combat.luck.readback.maxLuckNoLuck",
    "new-horizons.combat.luck.readback.maximumLuckLimit",
    "new-horizons.combat.luck.readback.noSources",
):
    assert f'"{text_id}"' in TEXTS, f"Missing localized config entry: {text_id}"

assert "BattleLuckReadback displayedLuckReadback;" in PANEL_H
assert "displayedLuckReadback = luckReadback;" in PANEL_CPP
assert "luckReadback.active() ? luckReadback.ordinaryAttackLuck : stack->luckVal()" in PANEL_CPP
assert "currentLuckReadback == displayedLuckReadback" in PANEL_CPP
assert "sylvanLuckStackTooltip" in PANEL_CPP, "Keep the existing faction-specific help"
assert "if(!displayedLuckReadback.noLuck || displayedLuckReadback.maxLuck)" in PANEL_CPP
assert PANEL_CPP.count("Rect(7, 141, 67, 14), luckTooltip, luckTooltip") == 1, (
    "Generic Luck and Sylvan details must share exactly one native row hitbox"
)
assert "currentStackInfoStatus(stack, battleCallback.get(), luckReadback)" in PANEL_CPP
assert "currentStackInfoStatus(updatedInfo, battleCallback.get(), currentLuckReadback)" in PANEL_CPP

print("PASS: saved New Horizons battle Luck value, exceptional overrides, scoped sources, cache refresh, and single native tooltip row")
