#!/usr/bin/env python3
"""Protect link source bindings; not rendered visual acceptance."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
helper = (ROOT / "client/battle/NewHorizonsProtectLink.h").read_text()
renderer = (ROOT / "client/battle/BattleFieldController.cpp").read_text()

for token in (
    "battleGetHeroOrderState(side, HeroCommand::PROTECT)",
    "battleOrderBenefitAppliesTo(*order, side, protector)",
    "battleOrderBenefitAppliesTo(*order, side, ward)",
    "protector->getPosition()", "ward->getPosition()",
    "protector->occupiedHex()", "ward->occupiedHex()",
    "protectorHead.isAvailable()", "wardHead.isAvailable()",
    "protectorRear.isAvailable()", "wardRear.isAvailable()",
):
    assert token in helper, f"Missing shared Protect readback: {token}"
for mutation in ("sendAndApply", "makePlayerBattleAction", "setPosition", "setOrder"):
    assert mutation not in helper

render = renderer.split("void BattleFieldController::renderBattlefield(", 1)[1].split(
    "void BattleFieldController::showProtectLinks(", 1
)[0]
assert render.index("showBackground(clippedCanvas)") < render.index("showProtectLinks(clippedCanvas)")
assert render.index("showProtectLinks(clippedCanvas)") < render.index("renderer.execute(clippedCanvas)")
links = renderer.split("void BattleFieldController::showProtectLinks(", 1)[1].split(
    "void BattleFieldController::showDemonicGateReservations(", 1
)[0]
for token in (
    "BattleSide::ATTACKER, BattleSide::DEFENDER", "newHorizonsProtectLink::activeLink(*battle, side)",
    "hexPositionLocal(head).center()", "hexPositionLocal(rear).center()",
    "canvas.drawLine", "wardCenter - protectorCenter",
):
    assert token in links
print("PASS: Protect ground link consumes shared current pair state; rendered fit remains unverified")
