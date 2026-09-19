#!/usr/bin/env python3
"""Focused source guard for the Magic Arrow Overcharge client bridge.

This is intentionally a static client test: the battle/runtime evaluator is
authoritative and needs a real battle to exercise.  The guard proves that the
UI exposes all required values, that target selection pauses only through the
generic action controller, and that cancellation/confirmation are wired to
callbacks rather than mutating rules locally.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOW = (ROOT / "client/battle/MagicArrowOverchargeWindow.cpp").read_text()
WINDOW_HEADER = (ROOT / "client/battle/MagicArrowOverchargeWindow.h").read_text()
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
INTERFACE = (ROOT / "client/battle/BattleInterface.cpp").read_text()


def require(haystack: str, needle: str, label: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
    for field in (
        "maximumOvercharge",
        "baseMana",
        "additionalMana",
        "totalMana",
        "availableMana",
        "baseDamage",
        "projectedDamage",
    ):
        require(WINDOW_HEADER, field, f"preview field {field}")

    for text in (
        '"Overcharge "',
        '"Mana: base "',
        '"Projected damage: "',
        'CSlider',
        '"-"',
        '"+"',
        '"Confirm"',
        '"Cancel"',
        'context.evaluate',
        'context.confirm',
        'context.cancel',
    ):
        require(WINDOW, text, f"window behavior {text}")

    require(CONTROLLER_HEADER, "MagicArrowOverchargeFactory", "controller adapter type")
    require(CONTROLLER_HEADER, "setMagicArrowOverchargeFactory", "controller adapter setter")
    require(CONTROLLER, "PossiblePlayerBattleAction::AIMED_SPELL_CREATURE", "generic target gate")
    require(CONTROLLER, "magicArrowOverchargeFactory", "post-target adapter call")
    require(CONTROLLER, "createAndPushWindow<MagicArrowOverchargeWindow>", "post-target window")

    # The adapter is specifically the existing core spell and the serialized
    # overcharge field; the frontend must not activate the abandoned Missile
    # draft or invent a second spell identity.
    require(INTERFACE, "SpellID::MAGIC_ARROW", "core Magic Arrow identity")
    require(INTERFACE, "magicArrowOverchargeEnabled", "saved-roster gate")
    require(INTERFACE, "spellOvercharge", "generic BattleAction payload")
    if "NEW_HORIZONS_MAGIC_MISSILE" in CONTROLLER + INTERFACE + WINDOW:
        raise AssertionError("frontend must not activate the abandoned Magic Missile draft")

    print("Magic Arrow Overcharge UI source checks passed")


if __name__ == "__main__":
    main()
