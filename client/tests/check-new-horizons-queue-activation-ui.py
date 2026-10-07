#!/usr/bin/env python3
"""Source bindings only; this is not rendered initiative-bar acceptance."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[2]
netpacks = (ROOT / "client/NetPacksClient.cpp").read_text()
player = (ROOT / "client/CPlayerInterface.cpp").read_text()
battle = (ROOT / "client/battle/BattleInterface.cpp").read_text()
queue = (ROOT / "client/battle/StackQueue.cpp").read_text()
texts = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text())

event = netpacks.split("void ApplyClientNetPackVisitor::visitBattleSetActiveStack(", 1)[1].split(
    "void ApplyClientNetPackVisitor::visitBattleLogMessage(", 1
)[0]
assert event.index("battleActiveStackReasonChanged") < event.index("AUTOMATIC_ACTION")
assert "pack.battleID, pack.stack, pack.reason" in event
forwarder = player.split("void CPlayerInterface::battleActiveStackReasonChanged(", 1)[1].split(
    "void CPlayerInterface::actionStarted(", 1
)[0]
assert "BATTLE_EVENT_POSSIBLE_RETURN" in forwarder
assert "battleInt->getBattleID() != battleID" in forwarder
assert "battleInt->activeStackReasonChanged(stackID, reason)" in forwarder

readback = battle.split("void BattleInterface::activeStackReasonChanged(", 1)[1].split(
    "const newHorizonsQueueActivationStatus::Status &", 1
)[0]
for token in (
    "newHorizonsMagic::rulesActive", "getMagicRules()", "HeroCommand::SECOND_WIND",
    "secondWindActive", "primaryTargetUnitId == activeUnit->unitId()",
    "battle->battleGetRound()", "newHorizonsQueueActivationStatus::update",
    "windowObject->updateQueue()",
):
    assert token in readback, f"Missing authoritative activation readback: {token}"

assert "boxIndex);" in queue, "Only the first queue box may carry the marker"
assert "battleActiveUnit()" in queue, "Use shared state, not a human-input stack pointer"
assert "activationStatus.round == battle->battleGetRound()" in queue
assert "marksCurrentEntry(activationStatus, unit->unitId(), queueIndex)" in queue
new_round = battle.split("void BattleInterface::newRound()", 1)[1].split(
    "void BattleInterface::", 1
)[0]
assert "queueActivationStatus = {};" in new_round
assert "Colors::YELLOW" in queue
assert 'extraActivation->setText(extraActivationHelp.empty() ? "" : "+")' in queue
assert "clearIfMatching(extraActivationHelp)" in queue
for key in ("queueExtraMorale", "queueExtraQuartermaster", "queueExtraSecondWind"):
    text_id = f"new-horizons.combat.{key}"
    assert text_id in texts and text_id in queue

print("PASS: post-apply initiative origin event, saved rules, exact recipient and first-entry bindings (not rendered acceptance)")
