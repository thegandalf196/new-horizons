#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard authoritative Order presentation routing and unique recycled assets.

Optional --data checks purchaser archive directory entries read-only, without
extracting, copying or requiring original assets for the default source guard.
"""
import argparse
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "client/battle/BattleInterface.cpp").read_text()


def body(name, following):
    return SOURCE.split("void BattleInterface::" + name, 1)[1].split(
        "void BattleInterface::" + following, 1)[0]



def order_switch_cases(presentation):
    switch = presentation.split("switch(action.command)", 1)[1].split("BattleHexArray targets;", 1)[0]
    cases = set(re.findall(r"case HeroCommand::(\w+):", switch))
    assert "FEINT" in cases, "Feint must reach troop and hero presentation"
    return cases


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path)
    args = parser.parse_args()
    presentation = (ROOT / "client/battle/BattleOrderPresentation.cpp").read_text()
    start = body("startAction", "stackRemoved")
    end = body("endAction", "presentHeroPositiveProcs")
    expected = {
        "CHARGE": ("C15SPA0", "TAILWIND"),
        "HOLD_THE_LINE": ("C16SPE", "TUFFSKIN"),
        "RIPOSTE": ("C04SPA0", "CNTRSTRK"),
        "BRACE": ("C13SPE0", "SHIELD"),
        "PROTECT": ("C01SPA0", "AIRSHELD"),
        "FOCUS_FIRE": ("C12SPA0", "PRECISON"),
        "FLANK": ("C07SPA1", "DISRUPTR"),
        "SECOND_WIND": ("C09SPW0", "MIRTH"),
        "VANGUARD": ("C01SPW", "BLESS"),
    }
    mapping = {command: (animation, sound) for command, animation, sound in re.findall(
        r'case HeroCommand::(\w+): animation = "([^"]+)"; sound = "([^"]+)";', presentation)}
    assert mapping == expected
    assert len({value[0] for value in mapping.values()}) == len(mapping)
    commands = json.loads((ROOT / "config/newHorizonsCombat.json").read_text())["combat"]["heroCommands"]["commands"]
    cases = order_switch_cases(presentation)
    assert cases == set(expected) | {"FEINT"}
    assert {key.lower() for key in commands} <= {key.replace("_", "").lower() for key in cases}
    # Reproduce the reported missing-case defect against the actual source:
    # the guard must fail even while the native defence branch still exists.
    missing_feint = re.sub(r"case HeroCommand::FEINT:[^\n]*", "", presentation, count=1)
    try:
        order_switch_cases(missing_feint)
    except AssertionError:
        pass
    else:
        raise AssertionError("Deleting the Feint switch case escaped the guard")
    for token in ("!action.metamagicDecline", "!getBattle()->getBattle()->getHeroCommandUsed(action.side)",
                  "pendingHeroOrderRound = getBattle()->battleGetRound()"):
        assert token in start, token
    for token in ("std::move(pendingHeroOrderPresentation)", "pending->side != action.side",
                  "pending->command != action.command", "pendingHeroOrderRound != getBattle()->battleGetRound()",
                  "!getBattle()->getBattle()->getHeroCommandUsed(action.side)",
                  "getActiveOrder(action.side) != action.command",
                  "state->issuedRound != pendingHeroOrderRound",
                  "hero->setPhase(EHeroAnimType::VICTORY)",
                  "std::make_unique<HeroOrderGestureAnimation>(*this, hero,",
                  "new DefenceAnimation(*this, stack)", "state->revision->contains(unit->unitId())",
                  "none_of(pendingHeroOrderGrantIds, wasConsumed)",
                  "state->primaryTargetUnitId", "state->secondaryTargetUnitId",
                  "effectsController->displayAnimation"):
        assert token in presentation, token
    assert presentation.index("std::move(pendingHeroOrderPresentation)") < presentation.index("std::make_unique<HeroOrderGestureAnimation>(")
    assert end.index("waitForAnimations();") < end.index("presentAcceptedHeroOrder(action);")
    assert end.index("presentAcceptedHeroOrder(action);") < end.index("waitForAnimations();", end.index("presentAcceptedHeroOrder(action);"))
    assert "pendingHeroOrderGrantIds.push_back(grant.id)" in start
    assert "battleGetOrderActionAllowance" not in start
    assert "usesLedger && ledger.currentRound == getBattle()->battleGetRound() && !ledger.grants.empty()" in start
    assert "if(!pendingHeroOrderGrantIds.empty())" in presentation
    assert presentation.index("none_of(pendingHeroOrderGrantIds, wasConsumed)") < presentation.index("presentHeroPositiveProcs(action.side")
    assert "hero->onPhaseFinished([this]() { finished = true; })" in presentation
    assert "hero->onPhaseFinished({})" in presentation
    assert "if(finished)" in presentation
    assert "hero->play()" in presentation
    assert "const auto complete = onFinished;" in presentation
    assert "stacksController->addNewAnim(heroOrderGesture.get())" in presentation
    destructor = SOURCE.split("BattleInterface::~BattleInterface()", 1)[1].split("void BattleInterface::redrawBattlefield", 1)[0]
    assert "heroOrderGesture.reset()" in destructor
    assert "std::unique_ptr<BattleAnimation> heroOrderGesture" in (ROOT / "client/battle/BattleInterface.h").read_text()
    assert "CAST_SPELL" not in presentation
    assert "state->revision->contains(unit->unitId()) && !unit->isTimeStopped()" in presentation
    assert "!stack->isTimeStopped() && !stack->isFrozen() && !newHorizonsFrozen::isFrozen(*stack)" in presentation
    assert "BattleOrderPresentation.cpp" in (ROOT / "client/CMakeLists.txt").read_text()
    for forbidden in ("makingTurn()", "battleGetMySide()", "castThisSpell", "SpellID::", "battleMakeSpellAction"):
        assert forbidden not in presentation, forbidden
    network = (ROOT / "client/NetPacksClient.cpp").read_text()
    assert "ApplyFirstClientNetPackVisitor::visitStartAction" in network
    assert "ApplyClientNetPackVisitor::visitEndAction" in network
    graphics = (ROOT / "config/battles_graphics.json").read_text()
    for animation, _ in mapping.values():
        assert animation + ".DEF" in graphics
    mirth = (ROOT / "config/spells/timed.json").read_text().split('"mirth" : {', 1)[1].split('"levels"', 1)[0]
    assert '"C09SPW0"' in mirth and '"MIRTH"' in mirth
    if args.data:
        with (args.data / "H3sprite.lod").open("rb") as stream:
            stream.seek(8)
            count = struct.unpack("<I", stream.read(4))[0]
            stream.seek(92)
            sprites = {stream.read(32)[:16].split(b"\0")[0].decode().upper() for _ in range(count)}
        with (args.data / "Heroes3.snd").open("rb") as stream:
            count = struct.unpack("<I", stream.read(4))[0]
            sounds = {stream.read(48)[:40].split(b"\0")[0].decode().upper() for _ in range(count)}
        for animation, sound in mapping.values():
            assert animation + ".DEF" in sprites, animation
            assert sound in sounds, sound
    print("PASS: ten Orders; missing-Feint deletion rejected; frozen bodies preserved; owned hero gesture and receipt routing")


if __name__ == "__main__":
    main()
