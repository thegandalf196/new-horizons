#!/usr/bin/env python3
"""Static guard for the human New Horizons Recruitment/Muster entry points.

This guard checks the client wiring only. Authority, weekly markers, and the
Muster packet are validated by the server-side tests; this file ensures that
the human flows expose the same perk-aware contract without mutating state.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(text: str, needle: str, description: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: {description}: missing {needle!r}")


UI = (ROOT / "client/windows/NewHorizonsMusterUI.cpp").read_text(encoding="utf-8")
UI_HEADER = (ROOT / "client/windows/NewHorizonsMusterUI.h").read_text(encoding="utf-8")
RECRUITMENT = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
QUICK = (ROOT / "client/windows/QuickRecruitmentWindow.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "client/CMakeLists.txt").read_text(encoding="utf-8")
TEXTS = (ROOT / "config/newHorizonsMusterTexts.json").read_text(encoding="utf-8")

require(UI, 'getPerkSkillRank(std::string(::newHorizonsMuster::RECRUITMENT_SKILL))',
        "Muster is gated by the saved Recruitment skill rank")
require(UI, 'newHorizonsHeroes::usesPerkRules',
        "legacy worlds do not expose the New Horizons action")
require(UI, 'getNewHorizonsMusterLastWeek() == week',
        "settlement weekly-use marker is read from authoritative state")
require(UI, 'absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek())',
        "client uses the same zero-based absolute week as server and AI")
require(UI, 'getNewHorizonsMusterUsesThisWeek(week)',
        "client uses the authoritative per-week Muster use count")
require(UI, 'maximumUsesPerWeek(modifiers)',
        "client exposes Master Recruiter availability")
require(UI, 'getCreatureCategory(creature)',
        "targets use the saved Core/Elite/Champion mapping")
require(UI, 'amountForCategory', "rank and active Recruitment perk amounts are explicit")
require(UI, 'musterCreatures(hero, targetTown, targets[index].creature)',
        "confirmed row selection sends the authoritative callback")
require(UI, 'Choose one town dwelling to reinforce.',
		"Muster instructions use player-facing Heroes III language")
require(UI_HEADER, 'std::vector<Target> targetsFor',
        "UI exposes legal dwelling-row targets")
require(RECRUITMENT, '#include "NewHorizonsMusterUI.h"',
        "standard recruitment flow includes Muster")
require(RECRUITMENT, 'newHorizonsMusterUI::open(town)',
        "standard recruitment flow opens Muster")
require(QUICK, '#include "NewHorizonsMusterUI.h"',
        "quick recruitment flow includes Muster")
require(QUICK, 'newHorizonsMusterUI::open(town)',
        "quick recruitment flow opens Muster")
require(CMAKE, 'windows/NewHorizonsMusterUI.cpp', "Muster UI source is in the client target")
require(CMAKE, 'windows/NewHorizonsMusterUI.h', "Muster UI header is in the client target")
for key in (
    'new-horizons.muster.title',
    'new-horizons.muster.available',
    'new-horizons.muster.used',
    'new-horizons.muster.targetUsed',
    'new-horizons.muster.masterAvailable',
    'new-horizons.muster.chooseRow',
    'new-horizons.muster.noTargets',
    'new-horizons.muster.rankOnlyNote',
):
    require(TEXTS, key, f"Muster translation {key}")

print("New Horizons Muster UI: PASS")
