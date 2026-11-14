#!/usr/bin/env python3
"""Static guard for the New Horizons creature-stat presentation.

The rules and the battle model own the Speed/Initiative distinction.  This
guard keeps the creature window from silently collapsing them back into one
row, while preserving the compact legacy panel when no NH capability snapshot
is active.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/windows/CCreatureWindow.h").read_text(encoding="utf-8")
ASSET = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "client/windows/CreatureStatPresentation.cpp").read_text(encoding="utf-8")
LAYOUT = (ROOT / "client/windows/CreatureStatLayout.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/render/AssetGeneratorCreaturePanel.cpp").read_text(encoding="utf-8")
ASSET_HEADER = (ROOT / "client/render/AssetGenerator.h").read_text(encoding="utf-8")

assert "INITIATIVE" in LAYOUT
assert "std::array<std::shared_ptr<CIntObject>, creatureStatLayout::STAT_COUNT> statIcons" in HEADER
assert "std::array<std::string, creatureStatLayout::STAT_COUNT> statNames" in HEADER
assert "std::array<std::string, creatureStatLayout::STAT_COUNT> statFormats" in HEADER
assert '"Initiative"' in PRESENTATION
assert '"Leadership Cost"' in PRESENTATION

# Speed is movement range; Initiative is the turn-order value. Both base and
# effective values are shown when the New Horizons capability snapshot is live.
assert "getMovementRange()" in PRESENTATION
assert "getBaseInitiative()" in PRESENTATION
assert "getInitiative()" in PRESENTATION
assert "addStatLabel(EStat::INITIATIVE" in PRESENTATION
assert "showNewHorizonsStats" in WINDOW
assert '"stackWindow/iconInitiative"' in PRESENTATION

# The ordinary and battle inspectors must include physical-only Defense from
# the real bonus graph, without turning it into magical Defense or re-creating
# a hero specialty's count formula in the client.
DEFENSE = (ROOT / "client/windows/CreatureDefensePresentation.h").read_text(encoding="utf-8")
STAT_LABELS = (ROOT / "client/windows/CreatureStatPresentation.cpp").read_text(encoding="utf-8")
assert "addDefenseReadout(creature, *battleStack, battleStack->isShooter())" in PRESENTATION
assert "addDefenseReadout(creature, armyStack, shooter)" in PRESENTATION
assert "subject.getDefense(ranged)" in DEFENSE
assert "subject.getBonusBearer()->valOfBonuses(BonusType::PHYSICAL_DEFENSE)" in DEFENSE
assert "boneFormationDefense" not in DEFENSE + STAT_LABELS
assert "addStatLabel(EStat::DEFENCE, base.physical, current.physical)" in STAT_LABELS
assert "Defense against magical creature attacks: " in STAT_LABELS
assert "Physical-only Defense bonuses do not apply against magical creature attacks." in STAT_LABELS

# The New Horizons panel has an Initiative row; the optional rank row adds a
# further row only for saved contexts with an explicit category.
assert '"stackWindow/info-panel-nh-0.png"' in PANEL
assert 'createCreatureInfoPanel(2, true)' in PANEL
assert "(newHorizons ? 13 : 9) + (rank ? 1 : 0)" in LAYOUT
assert "creatureStatLayout::panelHeight(showNewHorizonsStats, showRank)" in PANEL
assert "creatureStatLayout::panelRows(showNewHorizonsStats, showRank)" in PANEL
assert "registerCreaturePanelAssets();" in ASSET
assert "void AssetGenerator::registerCreaturePanelAssets()" in PANEL
assert "createCreatureInitiativeIcon" in ASSET_HEADER
assert "createCreatureInitiativeIcon" in PANEL


# Supplemental fields use the same ordered layout, readout and native glyphs.
assert "INITIATIVE,\n\tRETALIATIONS,\n\tPHYSICAL_RESISTANCE,\n\tMAGICAL_RESISTANCE," in LAYOUT
for field in ("RETALIATIONS", "PHYSICAL_RESISTANCE", "MAGICAL_RESISTANCE"):
    assert f"addStatLabel(EStat::{field}," in PRESENTATION
for icon in ("Retaliations", "PhysicalResistance", "MagicalResistance"):
    assert f"stackWindow/icon{icon}" in PRESENTATION
    assert f"stackWindow/icon{icon}" in PANEL
assert "creatureSupplementalStats::query(armyStack, battleStack, hero, rules)" in PRESENTATION
assert "hasBonusOfType(BonusType::IN_FRENZY)" in DEFENSE

print("New Horizons creature statistics UI: PASS (source guard)")
