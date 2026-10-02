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
require(UI, 'musterCreatures(hero, targetDwelling, targets[choice.firstTarget].creature)',
        "confirmed solo row selection sends the authoritative callback")
require(UI, 'BROAD_MUSTER_PERK', "Broad Muster choices require the active shared perk identity")
require(UI, 'town && hero->hasActivePerk', "Broad Muster is exposed only for a town hero")
require(UI, 'std::optional<size_t> secondTarget', "each allocation names at most two destination rows")
require(UI, 'firstTarget.row == secondTarget.row || firstTarget.creature == secondTarget.creature',
        "split choices reject duplicate row or creature identities")
require(UI, 'offer.externalDwelling || !dynamic_cast<const CGTownInstance *>(offer.dwelling)',
        "external dwellings never expose Broad Muster splits")
require(UI, 'firstAmount = 1; firstAmount < totalAmount',
        "split allocations use bounded positive counts below the generated total")
require(UI, 'const int secondAmount = first.amount - choice.firstAmount;',
        "second row receives the exact remainder of the generated total")
require(UI, 'std::to_string(choice.firstAmount)', "split labels show the first exact row amount")
require(UI, 'std::to_string(secondAmount)', "split labels show the second exact row amount")
require(UI, 'first.creatureType->getNamePluralTranslated()', "split labels name the first destination")
require(UI, 'second.creatureType->getNamePluralTranslated()', "split labels name the second destination")
require(UI, 'status(*offer) + ": " + amountSummary',
        "Muster offer summary shows the current rank and perk totals")
require(UI, 'targets[*choice.secondTarget].creature, choice.firstAmount)',
        "confirmed split choice sends both distinct destinations and the first allocation")
require(UI, 'Choose one town dwelling to reinforce.',
		"Muster instructions use player-facing Heroes III language")
require(UI_HEADER, 'std::vector<Target> targetsFor',
        "UI exposes legal dwelling-row targets")
require(RECRUITMENT, '#include "NewHorizonsMusterUI.h"',
        "standard recruitment flow includes Muster")
require(RECRUITMENT, 'newHorizonsMusterUI::open(Dwelling, destinationHero)',
        "standard recruitment flow opens Muster")
require(UI, 'amountForExternalCategory', "external Core Muster uses the fixed shared amount")
require(UI, 'EXTERNAL_RECRUITER_PERK', "external offer requires the active perk")
require(RECRUITMENT, 'getRecruitmentCost', "price presentation uses authoritative dwelling costs")
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
    'new-horizons.muster.externalTargetUsed',
    'new-horizons.muster.externalRecruiterNote',
    'new-horizons.muster.noExternalTargets',
):
    require(TEXTS, key, f"Muster translation {key}")

print("New Horizons Muster UI: PASS")
