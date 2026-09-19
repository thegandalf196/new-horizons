#!/usr/bin/env python3
"""Source-only contract for Transfigure Matter's client target UX.

The authoritative spell script decides whether a cast is accepted. This
focused guard checks that the client does not offer fortifications, moats or
magical obstacles as targets, that every ordinary footprint is highlighted,
and that hover text consumes the existing effect-value preview API.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
    require(CONTROLLER_HEADER, "isTransfigureMatterSpell", "spell identity helper")
    require(CONTROLLER_HEADER, "isValidTransfigureMatterTarget", "click-time target helper")
    require(CONTROLLER_HEADER, "getTransfigureMatterTargetHexes", "overlay target helper")

    require(CONTROLLER, '"new-horizons:transfigureMatter"', "canonical spell identity")
    require(CONTROLLER, "CObstacleInstance::USUAL", "ordinary physical obstacle filter")
    require(CONTROLLER, "battleGetAllObstacles()", "visible obstacle enumeration")
    require(CONTROLLER, "getAffectedTiles()", "whole obstacle footprint")
    require(CONTROLLER, "isCastingPossibleHere(spell, nullptr, hex)", "authoritative legality-backed overlay")
    require(CONTROLLER, "PossiblePlayerBattleAction::ANY_LOCATION", "normalized location action")
    require(CONTROLLER, "getSpellEffectValue", "existing health-change preview facility")
    require(CONTROLLER, "value.unitsDelta", "Diamond Golem count preview")
    require(CONTROLLER, "value.hpDelta", "total HP preview")
    require(CONTROLLER, '"total HP: "', "total HP label")

    require(FIELD, "isTransfigureMatterSpell", "target-overlay identity gate")
    require(FIELD, "getTransfigureMatterTargetHexes", "target-overlay footprint set")

    print("Transfigure Matter client targeting source checks passed")
    print("NOT compiled, native input/rendering or authoritative spell acceptance")


if __name__ == "__main__":
    main()
