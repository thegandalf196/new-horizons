#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-only contract for Summon Trolls target selection and hover preview."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
    require(CONTROLLER_HEADER, "isSummonTrollsSpell", "spell identity helper")
    require(CONTROLLER_HEADER, "getSummonTrollsTargetHexes", "legal placement helper")

    require(CONTROLLER, '"new-horizons:summonTrolls"', "canonical spell identity")
    require(CONTROLLER, "index < GameConstants::BFIELD_SIZE", "battlefield placement enumeration")
    require(CONTROLLER, "hex.isAvailable() && isCastingPossibleHere(spell, nullptr, hex)",
            "mechanics-validated legal placement")
    require(CONTROLLER, "prepareSummonTrollsText(spell, *spellEffectValue)", "hover preview formatting")
    require(CONTROLLER, "value.unitsDelta", "summoned Troll count preview")
    require(CONTROLLER, "value.unitType->getNamePluralTranslated()", "summoned creature type preview")
    require(CONTROLLER, "value.hpDelta", "aggregate HP preview")
    require(CONTROLLER, '"temporary Troll stack: + "', "temporary stack label")
    require(CONTROLLER, '"footprint: 1 hex"', "one-hex footprint preview")

    status_message = CONTROLLER.split("std::string BattleActionsController::actionGetStatusMessage(", 1)[1].split(
        "std::string BattleActionsController::actionGetStatusMessageBlocked(", 1)[0]
    any_location = status_message.split("case PossiblePlayerBattleAction::ANY_LOCATION:", 1)[1].split(
        "case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:", 1)[0]
    free_location = status_message.split("case PossiblePlayerBattleAction::FREE_LOCATION:", 1)[1].split(
        "case PossiblePlayerBattleAction::HEAL:", 1)[0]
    require(any_location, "prepareSummonTrollsText", "ANY_LOCATION hover preview")
    require(free_location, "getSpellEffectValue", "FREE_LOCATION effect-value preview")
    require(free_location, "prepareSummonTrollsText", "FREE_LOCATION hover preview")

    require(FIELD, "isSummonTrollsSpell", "target-overlay identity gate")
    require(FIELD, "getSummonTrollsTargetHexes", "legal empty-hex overlay")

    print("Summon Trolls client targeting source checks passed")
    print("NOT compiled, native input/rendering or authoritative spell acceptance")


if __name__ == "__main__":
    main()
