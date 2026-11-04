#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard native Inspiration readback and the shared contextual Hero Action UI."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
status = (ROOT / "client/battle/HeroInfoWindow.cpp").read_text(encoding="utf-8")
layout = (ROOT / "client/battle/HeroInfoWindow.h").read_text(encoding="utf-8")
window = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")

inspiration = (ROOT / "client/battle/ArcaneInspirationPresentation.h").read_text(encoding="utf-8")
strip = (ROOT / "client/battle/StackTemporaryStatusPresenter.cpp").read_text(encoding="utf-8")
stack_panel = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
creature_rows = (ROOT / "client/windows/CreatureBonusStatusPresentation.cpp").read_text(encoding="utf-8")
for obsolete in ("getWarcastingState", "AlternatingHeroActionState", "Next eligible action:",
                 "Reactive Weave", "readinessExpiryFor", "effectiveBonus"):
    assert obsolete not in window, f"retired action readiness remains: {obsolete}"
for readback in ("battle->battleGetOwner(&stack)", "activeProfile(stack, owner)", "retainedPercent(stack, owner)",
                 "effectiveCorePercent(hero", "isSpellshock"):
    assert readback in inspiration, f"shared Inspiration readback missing: {readback}"
assert "stack.getOwner()" not in inspiration, "Inspiration readback must follow combat allegiance under Hypnotize"
assert "next attack or retaliation" in inspiration
assert "even while Inspiration is active" in inspiration
assert "beginning of the next combat round" in inspiration
assert "ArcaneInspirationPresentation.h" in creature_rows
assert "inspiration.details" in creature_rows
assert "if(entry.inspiration)" in strip
assert "inspiration.details, inspiration.details" in strip
assert "arcaneInspirationPresentation::forStack(*updatedInfo, battleCallback.get()) == displayedInspiration" in stack_panel
for mutation in ("activeMarker(", "retainedMarker(", "advanceRound(", "addNewBonus("):
    assert mutation not in inspiration, f"presentation mutates Inspiration: {mutation}"

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

print("PASS: native Inspiration markers replace readiness, statuses refresh, Hero Action remains contextual, and short-screen layout falls back")
