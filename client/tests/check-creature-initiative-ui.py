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
ASSET_HEADER = (ROOT / "client/render/AssetGenerator.h").read_text(encoding="utf-8")

assert "INITIATIVE" in HEADER
assert "std::array<std::shared_ptr<CIntObject>, 10> statIcons" in HEADER
assert "std::array<std::string, 10> statNames" in HEADER
assert "std::array<std::string, 10> statFormats" in HEADER
assert '"Initiative"' in WINDOW
assert '"Leadership Cost"' in WINDOW

# Speed is movement range; Initiative is the turn-order value. Both base and
# effective values are shown when the New Horizons capability snapshot is live.
assert "getMovementRange()" in WINDOW
assert "getBaseInitiative()" in WINDOW
assert "getInitiative()" in WINDOW
assert "addStatLabel(EStat::INITIATIVE" in WINDOW
assert "showNewHorizonsStats" in WINDOW
assert '"stackWindow/iconInitiative"' in WINDOW

# Legacy panels retain their old dimensions; NH panels get a tenth row.
assert '"stackWindow/info-panel-nh-0.png"' in ASSET
assert 'createCreatureInfoPanel(2, true)' in ASSET
assert "const int statRows = showNewHorizonsStats ? 10 : 9;" in ASSET
assert "createCreatureInitiativeIcon" in ASSET_HEADER
assert "createCreatureInitiativeIcon" in ASSET

print("New Horizons creature Speed/Initiative UI: PASS")
