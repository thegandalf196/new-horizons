#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the Warcasting battle indicator independent of skill rank/bonus size."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
status = (ROOT / "client/battle/HeroInfoWindow.cpp").read_text(encoding="utf-8")

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

print("PASS: Warcasting icon follows the pending action, not its rank or amount")
