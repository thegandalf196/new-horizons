#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the Warcasting battle indicator independent of skill rank/bonus size."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
status = (ROOT / "client/battle/HeroInfoWindow.cpp").read_text(encoding="utf-8")
layout = (ROOT / "client/battle/HeroInfoWindow.h").read_text(encoding="utf-8")
window = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")

icon_selector = status.split("std::string warcastingIconName(", 1)[1].split("\n}\n", 1)[0]
spell = icon_selector.split(
    "if(action == AlternatingHeroActionState::Action::SPELL)", 1
)[1].split("if(action == AlternatingHeroActionState::Action::ORDER)", 1)[0]
order = icon_selector.split(
    "if(action == AlternatingHeroActionState::Action::ORDER)", 1
)[1]

assert 'return "NH_perk_arcane_channeling_normal.png";' in spell
assert 'return "NH_perk_martial_channeling_normal.png";' in order
assert "empowerment" not in icon_selector
assert "NH_warcasting_basic_small.png" not in status
assert "NH_warcasting_advanced_small.png" not in status
assert "NH_warcasting_expert_small.png" not in status

assert "warcastingIconName(action)" in status
assert "std::to_string(empowerment)" in status
assert "std::to_string(warcastingState.expiryRound)" in status
assert "Available through round " in status
assert "(inclusive)." in status

# The 70px status strip stays within the side panel, while Counterspell and
# Warcasting each retain their own row above a readable three-line action area.
assert "constexpr int effectAreaWidth = 70;" in layout
assert "constexpr int effectAreaRowHeight = 32;" in layout
assert "constexpr int actionCountLineHeight = 14;" in layout
assert "constexpr int actionCountHeaderHeight = 18;" in layout
assert "constexpr int actionCountPanelPadding = 6;" in layout
assert "constexpr int actionCountPanelHeight = actionCountHeaderHeight + actionCountLineHeight * 3 + actionCountPanelPadding;" in layout
assert 'ImagePath::builtin("DIBOXBCK")' in status
assert "ColorRGBA(145, 18, 12), 2" in status
assert "ColorRGBA(213, 185, 117)" in status
assert 'addCount(0, "Hero", actionCounts.heroActions);' in status
assert 'addCount(1, "Order", actionCounts.orderActions);' in status
assert 'addCount(2, "Spell", actionCounts.spellActions);' in status
assert "ETextAlignment::BOTTOMRIGHT, count > 0" in status
assert "constexpr int effectAreaMaxStatusRows = 2;" in layout
assert "constexpr int effectAreaHeight = effectAreaRowHeight * effectAreaMaxStatusRows + actionCountPanelHeight;" in layout
assert "constexpr int outsideStackPanelOffsetY = effectAreaTop + effectAreaHeight + 3;" in layout
assert "const int statusRows = static_cast<int>(counterspellArmed) + static_cast<int>(warcastingActive);" in status
assert "(showActionCounts ? HeroInfoPanelLayout::actionCountPanelHeight : 0)" in status
assert "const int countsTop = statusRows * HeroInfoPanelLayout::effectAreaRowHeight;" in status
assert "ENGINE->windows().totalRedraw();" in status
assert 'ENGINE->statusbar()->clearIfMatching(statusbarText);' in status

# The expanded outside layout cannot crop the active stack readout on short
# viewports; it falls back to the existing compact overlay placement.
outside_layout = window.split("bool BattleWindow::placeInfoWindowsOutside() const", 1)[1].split(
    "bool BattleWindow::quickActionsPanelActive() const", 1
)[0]
assert "stackPanelBottom <= ENGINE->screenDimensions().y" in outside_layout
assert "return false;" in outside_layout

print("PASS: Warcasting art is rank-neutral, statuses remain distinct, counts fit in their own rows, and short-screen layout falls back")
