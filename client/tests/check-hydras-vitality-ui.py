#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-only contract for Hydra's Vitality battle preview and status text."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
CREATURE_WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing function body: {signature}")

    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> None:
    require(CONTROLLER, '"new-horizons:hydrasVitality"', "canonical spell identity")
    require(CONTROLLER_HEADER, "isHydrasVitalitySpell", "spell identity helper declaration")

    preview = function_body(CONTROLLER, "prepareHydrasVitalityText(")
    require(preview, "capacityIncreasePercentMillionths", "shared full-precision percentage input")
    require(preview, "formatPercentMillionths(capacityIncreasePercentMillionths)", "percent-millionths preview formatting")
    require(preview, "std::to_string(durationRounds)", "shared adjusted duration")
    require(preview, "no immediate healing", "no fake cast-time healing")
    require(preview, "no immediate healing or casualty restoration", "no casualty restoration")
    require(preview, "genuine activation start", "activation timing")
    require(preview, "10% of ", "per-survivor regeneration rate")
    if "hpDelta" in preview or "unitsDelta" in preview:
        raise AssertionError("capacity preview must not claim immediate healing or creature restoration")

    percent_formatter = function_body(CONTROLLER, "formatPercentMillionths(")
    require(percent_formatter, "1'000'000", "percent-millionths scale")
    require(percent_formatter, "fractionWidth", "fixed fractional precision")
    require(percent_formatter, "while(!fraction.empty()", "trailing-zero trimming")

    status_message = function_body(
        CONTROLLER,
        "BattleActionsController::actionGetStatusMessage(",
    )
    targeted_spell = status_message.split("case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:", 1)[1].split(
        "case PossiblePlayerBattleAction::LIFE_DRAIN:", 1
    )[0]
    require(targeted_spell, "isHydrasVitalitySpell(spell)", "targeted spell hover branch")
    require(targeted_spell, "mechanics->getEffectValue()", "shared mechanics percentage preview")
    require(targeted_spell, "mechanics->adjustEffectDuration(3)", "shared metamagic-adjusted duration")
    require(targeted_spell, "prepareHydrasVitalityText", "non-healing target preview")
    if "hpDelta =" in targeted_spell or "unitsDelta =" in targeted_spell:
        raise AssertionError("target preview must not repurpose immediate-health or casualty fields")

    status = function_body(CREATURE_WINDOW, "CStackWindow::initBonusesList(")
    require(status, "BonusType::HP_REGENERATION", "active Hydra marker type")
    require(status, "BonusSource::SPELL_EFFECT", "active spell source")
    require(status, '"new-horizons:hydrasVitality"', "active Hydra spell identity")
    require(status, "battleStack->getMaxHealth()", "enhanced capacity display")
    require(status, "state->capacityRegenerationProjectedHeal()", "shared next-activation healing forecast")
    require(status, "marker.turnsRemain", "active spell duration status")
    require(status, "hydraMarkerAlreadySelected", "coalesced regeneration marker fallback")

    for source in (CONTROLLER, CONTROLLER_HEADER, CREATURE_WINDOW):
        if "HydrasVitalityWindow" in source or "HydrasVitalityDialog" in source:
            raise AssertionError("Hydra's Vitality must use existing hover/status UI, not add a window")

    print("Hydra's Vitality battle UI source checks passed")
    print("NOT compiled, rendered, or validated against an authoritative cast")


if __name__ == "__main__":
    main()
