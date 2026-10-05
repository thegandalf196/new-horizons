#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the Warcasting battle indicator independent of skill rank/bonus size."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
status = (ROOT / "client/battle/HeroInfoWindow.cpp").read_text(encoding="utf-8")
layout = (ROOT / "client/battle/HeroInfoWindow.h").read_text(encoding="utf-8")
window = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")

icon_selector = window.split("std::string warcastingIconName(", 1)[1].split("\n}\n", 1)[0]
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

assert "warcastingIconName(action)" in window
assert "std::to_string(empowerment)" in window
assert "std::to_string(warcasting.expiryRound)" in window
assert '" Available through round "' in window
assert '" (inclusive)."' in window
assert 'entries.push_back({warcastingIconName(action), actionName, amount, tooltip});' in window

# Generic entries retain their own rows above one compact normal Hero Action state.
assert "constexpr int effectAreaWidth = 70;" in layout
assert "constexpr int effectAreaRowHeight = 32;" in layout
assert "constexpr int heroActionStateLineHeight = 14;" in layout
assert "constexpr int heroActionStateHeaderHeight = 18;" in layout
assert "constexpr int heroActionStatePanelPadding = 6;" in layout
assert "constexpr int heroActionStatePanelHeight = heroActionStateHeaderHeight" in layout
assert "\t+ heroActionStateLineHeight + heroActionStatePanelPadding;" in layout
assert 'ImagePath::builtin("DIBOXBCK")' in status
assert "ColorRGBA(145, 18, 12), 2" in status
assert "ColorRGBA(213, 185, 117)" in status
assert '"Hero Action"' in status
assert '"Available"' in status
assert '"Spent"' in status
assert "normalHeroActionAvailable" in status
assert "Additional Spell-only and Order-only opportunities are shown with their source and expiry" in status
for obsolete in ("actionCounts.orderActions", "actionCounts.spellActions", "Hero / Order / Spell Actions:", "addCount("):
    assert obsolete not in status, f"independent action counter presentation remains: {obsolete}"
assert "struct CombatStatusEntry" in layout
assert "std::vector<CombatStatusEntry> statusEntries;" in layout
assert "for(size_t row = 0; row < statusEntries.size(); ++row)" in status
assert "const int statusRows = static_cast<int>(statusEntries.size());" in status
assert "const bool hasIcon = !entry.icon.empty();" in status
assert "fitStatusRowText(entry.label, textWidth)" in status
assert "fitStatusRowText(entry.value, textWidth)" in status
assert "counterspellArmed" not in status
assert "warcastingState" not in status
assert "effectAreaMaxStatusRows" not in layout
assert "effectAreaHeight" not in layout
assert "outsideStackPanelOffsetY" not in layout
assert "(showNormalHeroAction ? HeroInfoPanelLayout::heroActionStatePanelHeight : 0)" in status
assert "const int stateTop = statusRows * HeroInfoPanelLayout::effectAreaRowHeight;" in status
assert "ENGINE->windows().totalRedraw();" in status
assert 'ENGINE->statusbar()->clearIfMatching(statusbarText);' in status

# The expanded outside layout cannot crop the active stack readout on short
# viewports; it falls back to the existing compact overlay placement.
outside_layout = window.split("bool BattleWindow::placeInfoWindowsOutside() const", 1)[1].split(
    "bool BattleWindow::quickActionsPanelActive() const", 1
)[0]
assert "heroBattleStatusHeight(BattleSide::ATTACKER)" in outside_layout
assert "heroBattleStatusHeight(BattleSide::DEFENDER)" in outside_layout
assert "stackPanelBottom <= ENGINE->screenDimensions().y" in outside_layout
assert "return false;" in outside_layout

print("PASS: Warcasting art is rank-neutral, generic statuses size dynamically, the normal Hero Action is contextual, and short-screen layout falls back")
