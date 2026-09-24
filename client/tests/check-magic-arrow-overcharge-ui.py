#!/usr/bin/env python3
"""Focused source guard for the Magic Arrow Overcharge client bridge.

This is intentionally a static client test: the shared battle-mechanics
forecast needs a real battle to exercise. The guard proves that the UI exposes
the selected-target forecast and resistance caveat, target selection pauses
only through the generic action controller, and cancellation/confirmation are
wired to callbacks rather than mutating rules locally.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOW = (ROOT / "client/battle/MagicArrowOverchargeWindow.cpp").read_text()
WINDOW_HEADER = (ROOT / "client/battle/MagicArrowOverchargeWindow.h").read_text()
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
INTERFACE = (ROOT / "client/battle/BattleInterface.cpp").read_text()
MAGIC_ARROW_CONFIG = (ROOT / "config/spells/offensive.json").read_text()
MAGIC_ARROW_CONFIG_SECTION = MAGIC_ARROW_CONFIG.split('"magicArrow"', 1)[1].split('"iceBolt"', 1)[0]


def require(haystack: str, needle: str, label: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
    require(WINDOW, "center();", "screen-centered overcharge window")
    for forbidden in ("moveTo(context.anchor", "fitToScreen(4)"):
        if forbidden in WINDOW:
            raise AssertionError(
                f"Magic Arrow window still uses target-relative placement: {forbidden}"
            )

    for field in (
        "maximumOvercharge",
        "baseMana",
        "additionalMana",
        "totalMana",
        "availableMana",
        "baseDamage",
        "projectedDamage",
        "baseKills",
        "projectedKills",
        "magicResistancePercent",
        "previewAvailable",
        "legal",
    ):
        require(WINDOW_HEADER, field, f"preview field {field}")

    for text in (
        '"Overcharge "',
        '"Mana: base "',
        '"No Overcharge: "',
        '" estimated kills\\nWith Overcharge "',
        'Magic resistance: ',
        '"% chance; estimates assume the spell lands."',
        '"Forecast unavailable; estimate omitted. Confirm to cast or Cancel to return."',
        '-- damage, -- estimated kills',
        'constexpr int WINDOW_WIDTH = 420;',
        'CMultiLineLabel>(Rect(16, 190',
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
    require(INTERFACE, "canBeCastAt", "stale target legality guard")
    require(INTERFACE, 'effect.name != "directDamage"', "single direct damage effect forecast")
    require(INTERFACE, "effect.transformTarget", "canonical target effect filtering")
    require(INTERFACE, "effect.getHealthChange", "shared health and casualty forecast")
    require(INTERFACE, "target->magicResistance()", "target resistance estimate")
    require(INTERFACE, "target->getCount()", "casualties clamped to live stack count")
    require(MAGIC_ARROW_CONFIG, '"magicArrow"', "core Magic Arrow configuration")
    require(MAGIC_ARROW_CONFIG_SECTION, '"directDamage" : {"type":"damage"}', "configured Magic Arrow damage effect")
    if "adjustEffectValue(target)" in INTERFACE:
        raise AssertionError("preview must use target health/casualty simulation, not raw adjusted damage")
    if "100 + 15 * overcharge" in WINDOW:
        raise AssertionError("window must not invent its own Overcharge damage formula")
    require(WINDOW, "confirmButton->block(!values.legal || !values.affordable)", "preview-independent cast eligibility")
    require(WINDOW, "if(!values.legal || !values.affordable)", "preview-independent confirmation callback")
    if "!values.previewAvailable ||" in WINDOW:
        raise AssertionError("missing forecast must not disable an otherwise legal cast")
    preview_selection = INTERFACE.split("const auto baseChange = previewChange(0);", 1)[1].split("MagicArrowOverchargeContext context;", 1)[0]
    if "values.legal = false" in preview_selection:
        raise AssertionError("missing effect forecast must not make a legal target uncastable")
    require((ROOT / "client/battle/BattleWindow.cpp").read_text(), "Grand ON", "persistent Grand selection label")
    require((ROOT / "client/battle/BattleWindow.cpp").read_text(), "Grand OFF", "persistent Grand deselection label")
    require((ROOT / "client/windows/CSpellWindow.cpp").read_text(), "metamagicGrandLabel", "spellbook Grand selection state")
    if "NEW_HORIZONS_MAGIC_MISSILE" in CONTROLLER + INTERFACE + WINDOW:
        raise AssertionError("frontend must not activate the abandoned Magic Missile draft")

    # Rendering is a pure operation.  CLabel::setText and CButton::block both
    # request a redraw; calling refresh() from show/showAll would synchronously
    # re-enter showAll and recurse until the stack overflows.  The widget is
    # refreshed only by construction and input callbacks.
    for method in ("void MagicArrowOverchargeWindow::show(Canvas & to)",
                   "void MagicArrowOverchargeWindow::showAll(Canvas & to)"):
        start = WINDOW.index(method)
        end = WINDOW.find("\n}", start)
        if end < 0:
            raise AssertionError(f"cannot locate {method}")
        if "refresh()" in WINDOW[start:end]:
            raise AssertionError(f"paint path must not refresh state: {method}")

    print("Magic Arrow Overcharge UI source checks passed")


if __name__ == "__main__":
    main()
