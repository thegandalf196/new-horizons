#!/usr/bin/env python3
"""Source wiring guard for the Phantom Army Integrity and legal-landing preview.

This checks that the client consumes the shared spell-effect and landing
queries. It is not a render test and does not establish visual acceptance.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTIONS_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
ACTIONS_CPP = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
FIELD_CPP = (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")
SPELL_SCRIPT = (ROOT / "scripts/spells/phantomArmy.lua").read_text(encoding="utf-8")
COMBAT_TEXTS = (ROOT / "config/newHorizonsCombatTexts.json").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    """Return a C++ function body while skipping braces in comments/strings."""
    signature_start = source.index(signature)
    opening = source.index("{", signature_start)
    depth = 0
    index = opening
    while index < len(source):
        current = source[index]
        following = source[index:index + 2]
        if following == "//":
            newline = source.find("\n", index + 2)
            index = len(source) if newline < 0 else newline + 1
            continue
        if following == "/*":
            end = source.find("*/", index + 2)
            if end < 0:
                raise AssertionError(f"unterminated comment after {signature}")
            index = end + 2
            continue
        if current in ('"', "'"):
            quote = current
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    index += 2
                    continue
                if source[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if current == "{":
            depth += 1
        elif current == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
        index += 1
    raise AssertionError(f"unterminated function body: {signature}")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {context}: {token}")


def main() -> None:
    require(ACTIONS_H, "struct PhantomArmyPlacementPreview", "placement preview data")
    require(ACTIONS_H, "BattleHex landingHex", "predicted landing")
    require(ACTIONS_H, "BattleHexArray footprint", "predicted landing footprint")
    require(ACTIONS_H, "getPhantomArmyPlacementPreview", "shared placement query")

    formatter = function_body(ACTIONS_CPP, "static std::string preparePhantomArmyText(")
    for token in (
        "value->unitsDelta",
        "value->hpDelta",
        "value->unitType",
        "placement->landingHex",
        "placement->footprint.size()",
        '"new-horizons.combat.phantomArmy.preview"',
        '"new-horizons.combat.phantomArmy.noLegalPlacement"',
    ):
        require(formatter, token, f"count/Integrity/landing formatter {token}")
    for forbidden in ("getSpellEffectValue", "getAvailableHex", "CRandomGenerator", "sendAndApply"):
        if forbidden in formatter:
            raise AssertionError(f"formatter must only present supplied read-only data, found {forbidden}")

    landing = function_body(ACTIONS_CPP, "BattleActionsController::getPhantomArmyPlacementPreview(")
    for token in (
        "const auto * creature = source->unitType();",
        "battle->getAvailableHex(creature, side, source->getPosition())",
        "battle::Unit::getHexes(landingHex, doubleWide, side)",
        "expectedFootprintSize",
        "std::nullopt",
    ):
        require(landing, token, f"shared legal landing/footprint query {token}")
    for forbidden in ("castOn", "makePlayerBattleAction", "sendAndApply", "CRandomGenerator"):
        if forbidden in landing:
            raise AssertionError(f"landing query must be read-only, found {forbidden}")

    target_highlight = function_body(ACTIONS_CPP, "BattleActionsController::getPhantomArmyTargetHexes(")
    require(target_highlight, "getPhantomArmyPlacementPreview(source)", "preview-backed legal target hexes")
    require(target_highlight, "return preview ? preview->footprint", "legal landing footprint return")

    status = function_body(ACTIONS_CPP, "BattleActionsController::actionGetStatusMessage(")
    require(status, "getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex)",
        "shared SpellEffectValue query")
    require(status, "preparePhantomArmyText(spell, targetStack, spellEffectValue.get()",
        "Phantom Army-specific count and Integrity readback")
    require(status, "getPhantomArmyPlacementPreview(targetStack)", "same landing query in hover readback")

    highlight = function_body(FIELD_CPP, "BattleFieldController::getHighlightedHexesForSpellRange(")
    require(highlight, "getPhantomArmyTargetHexes(spell, hoveredHex)", "field highlight from legal landing footprint")

    blocked = function_body(ACTIONS_CPP, "BattleActionsController::actionGetStatusMessageBlocked(")
    require(blocked, "getSpellEffectValue(", "blocked-cast validity/value check")
    require(blocked, "getPhantomArmyPlacementPreview(source)", "blocked-cast landing validation")
    require(blocked, '"new-horizons.combat.phantomArmy.noLegalPlacement"', "no-legal-placement feedback")

    require(SPELL_SCRIPT,
        "battle:getAvailableHex(creature, mechanics:getCasterSide(), source:getPosition())",
        "authoritative summon landing query")
    require(SPELL_SCRIPT,
        "hpDelta = integrityPool(mechanics, source)",
        "shared SpellEffectValue Integrity basis")
    require(SPELL_SCRIPT, "unitsDelta = source:getCount()", "shared SpellEffectValue count basis")
    require(COMBAT_TEXTS,
        '"new-horizons.combat.phantomArmy.preview": "%s (count: %s); Phantom Integrity: %s; landing: %s; hex footprint: %s."',
        "localized count/Integrity/landing text")
    require(COMBAT_TEXTS,
        '"new-horizons.combat.phantomArmy.noLegalPlacement": "No legal placement for the Phantom stack."',
        "localized blocked-placement text")

    print("PASS: Phantom Army UI wiring reads shared Integrity/count and legal landing/footprint sources")
    print("Static source guard only; native tests cover mechanics parity and rendering still requires visual review.")


if __name__ == "__main__":
    main()
