#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-only contract for Verdant Prison target and ring previews."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text()


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
    require(CONTROLLER, '"new-horizons:verdantPrison"', "canonical spell identity")
    require(CONTROLLER_HEADER, "isVerdantPrisonSpell", "spell identity helper declaration")
    require(CONTROLLER_HEADER, "getVerdantPrisonTargetHexes", "shared legal-ring preview declaration")

    legal_ring = function_body(
        CONTROLLER,
        "BattleActionsController::getVerdantPrisonTargetHexes(",
    )
    require(legal_ring, "isCastingPossibleHere(spell, nullptr, targetHex)", "authoritative cast-legality query")
    require(legal_ring, "mechanics->rangeInHexes(targetHex)", "effect-provided legal placement ring")
    if "GameConstants::BFIELD_SIZE" in legal_ring or "for(" in legal_ring or "for (" in legal_ring:
        raise AssertionError("Verdant Prison preview must not scan or rebuild battlefield ring geometry")
    for forbidden in ("battleMakeSpellAction", "applyEffects", "sendAndApply"):
        if forbidden in legal_ring:
            raise AssertionError(f"preview helper must not mutate battle state: {forbidden}")

    preview = function_body(CONTROLLER, "prepareVerdantPrisonText(")
    require(preview, "value.unitsDelta", "summoned creature-count preview")
    require(preview, "value.unitType->getNamePluralTranslated()", "Dendroid creature-type preview")
    require(preview, '"temporary "', "temporary summon status")
    require(preview, '" count: "', "total Dendroid count label")
    require(preview, "value.hpDelta", "aggregate HP result")
    require(preview, '"aggregate HP: "', "whole-pool HP preview")
    require(preview, '"legal ring footprint: "', "legal ring footprint count")

    status_message = function_body(
        CONTROLLER,
        "BattleActionsController::actionGetStatusMessage(",
    )
    targeted_spell = status_message.split("case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:", 1)[1].split(
        "case PossiblePlayerBattleAction::LIFE_DRAIN:", 1
    )[0]
    require(targeted_spell, "getSpellEffectValue", "shared effect-value callback")
    require(targeted_spell, "prepareVerdantPrisonText", "enemy-creature hover preview")
    require(targeted_spell, "getVerdantPrisonTargetHexes(spell, targetHex).size()",
            "legal ring footprint count")
    if "ANY_LOCATION" in targeted_spell or "FREE_LOCATION" in targeted_spell:
        raise AssertionError("Verdant Prison preview must stay on the single enemy-creature target path")

    field_preview = function_body(FIELD, "BattleFieldController::getHighlightedHexesForSpellRange(")
    require(field_preview, "BattleActionsController::isVerdantPrisonSpell", "spell-specific ring overlay gate")
    require(field_preview, "getVerdantPrisonTargetHexes(spell, hoveredHex)", "hovered-target ring overlay")

    for source in (CONTROLLER, CONTROLLER_HEADER, FIELD):
        if "VerdantPrisonWindow" in source:
            raise AssertionError("Verdant Prison targeting must not add a dialog")

    print("Verdant Prison client targeting source checks passed")
    print("NOT compiled, rendered, or validated against an authoritative cast")


if __name__ == "__main__":
    main()
