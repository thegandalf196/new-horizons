#!/usr/bin/env python3
"""Protect link source bindings; not rendered visual acceptance."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
helper = (ROOT / "client/battle/NewHorizonsProtectLink.h").read_text()
renderer = (ROOT / "client/battle/BattleFieldController.cpp").read_text()
actions = (ROOT / "client/battle/BattleActionsController.cpp").read_text()


def section(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


def require_tokens(source, tokens, description):
    for token in tokens:
        assert token in source, f"Missing {description}: {token}"

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

proposed = helper.split("inline std::optional<Link> proposedLink(", 1)[1]
require_tokens(proposed, (
    "side != BattleSide::ATTACKER && side != BattleSide::DEFENDER",
    "battlePrepareHeroOrderState(side, HeroCommand::PROTECT, {protectorUnitId, wardUnitId})",
    "footprintLink(battle.battleGetUnitByID(protectorUnitId), battle.battleGetUnitByID(wardUnitId))",
), "authoritative proposed Protect admission")
assert proposed.index("side != BattleSide::ATTACKER") < proposed.index("battlePrepareHeroOrderState")

context = section(actions, "bool BattleActionsController::heroOrderTargetingContextIsCurrent() const",
                  "std::vector<uint32_t> BattleActionsController::heroOrderTargetIds() const")
require_tokens(context, (
    "!selectedHeroOrderCommand", "!owner.currentHero()", "!owner.makingTurn()",
    "owner.curInt->isAutoFightOn", "owner.isInTacticsMode()", "heroSpellcastingModeActive()",
    "owner.getBattleID() == heroOrderTargetingBattleID", "side == heroOrderTargetingSide",
    "owner.getBattle()->battleGetRound() == heroOrderTargetingRound",
    "owner.currentHero()->id == heroOrderTargetingHeroID",
    "owner.curInt->playerID == heroOrderTargetingPlayer",
), "current Protect targeting context")
preview = section(actions, "BattleActionsController::getProposedProtectLink(",
                  "void BattleActionsController::cancelHeroOrderTargeting()")
require_tokens(preview, (
    "heroOrderTargetingContextIsCurrent()", "*selectedHeroOrderCommand != HeroCommand::PROTECT",
    "!heroOrderTargetingFirst", "!hoveredHex.isValid()", "if(!ward)", "return std::nullopt",
    # Position lookup resolves either occupied hex of a double-wide Ward to its unit ID.
    "battleGetStackByPos(hoveredHex, true)",
    "newHorizonsProtectLink::proposedLink(*owner.getBattle(), heroOrderTargetingSide,",
    "*heroOrderTargetingFirst, ward->unitId()",
), "selected Protector and hovered Ward preview")
for mutation in ("sendAndApply", "makePlayerBattleAction", "setPosition", "setOrder", "heroOrderTargetingFirst ="):
    assert mutation not in preview, f"Preview must remain read-only: {mutation}"
cancel = section(actions, "void BattleActionsController::cancelHeroOrderTargeting()",
                 "void BattleActionsController::selectHeroOrderTarget(")
require_tokens(cancel, (
    "heroOrderTargetingSide = BattleSide::NONE", "heroOrderTargetingRound = -1",
    "heroOrderTargetingHeroID = ObjectInstanceID::NONE", "heroOrderTargetingPlayer.reset()",
    "selectedHeroOrderCommand.reset()", "heroOrderTargetingFirst.reset()",
), "cancelled Protect preview cleanup")

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
    "getProposedProtectLink(getHoveredHex())", "drawLink(*link)", "drawLink(*proposed)",
    "if(proposed && *proposed == *link)", "proposed.reset()",
):
    assert token in links
print("PASS: active and proposed Protect links share admission/current footprints and read-only UI bindings; native behavior and rendered fit remain unverified")
