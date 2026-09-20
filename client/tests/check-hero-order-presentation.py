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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path)
    args = parser.parse_args()
    presentation = body("presentAcceptedHeroOrder", "appendBattleLog")
    start = body("startAction", "stackRemoved")
    end = body("endAction", "presentAcceptedHeroOrder")
    expected = {
        "CHARGE": ("C15SPA0", "TAILWIND"),
        "HOLD_THE_LINE": ("C16SPE", "TUFFSKIN"),
        "RIPOSTE": ("C04SPA0", "CNTRSTRK"),
        "BRACE": ("C13SPE0", "SHIELD"),
        "PROTECT": ("C01SPA0", "AIRSHELD"),
        "FOCUS_FIRE": ("C12SPA0", "PRECISON"),
        "FLANK": ("C07SPA1", "DISRUPTR"),
        "SECOND_WIND": ("C09SPW0", "MIRTH"),
    }
    mapping = {command: (animation, sound) for command, animation, sound in re.findall(
        r'case HeroCommand::(\w+): animation = "([^"]+)"; sound = "([^"]+)";', presentation)}
    assert mapping == expected
    assert len({value[0] for value in mapping.values()}) == len(mapping)
    commands = json.loads((ROOT / "config/newHorizonsCombat.json").read_text())["combat"]["heroCommands"]["commands"]
    assert {key.replace("_", "").lower() for key in mapping} == {key.lower() for key in commands}
    for token in ("!action.metamagicDecline", "!getBattle()->getBattle()->getHeroCommandUsed(action.side)",
                  "pendingHeroOrderRound = getBattle()->battleGetRound()"):
        assert token in start, token
    for token in ("std::move(pendingHeroOrderPresentation)", "pending->side != action.side",
                  "pending->command != action.command", "pendingHeroOrderRound != getBattle()->battleGetRound()",
                  "!getBattle()->getBattle()->getHeroCommandUsed(action.side)",
                  "getActiveOrder(action.side) != action.command",
                  "state->issuedRound != pendingHeroOrderRound",
                  "setHeroAnimation(action.side, EHeroAnimType::VICTORY)",
                  "state->primaryTargetUnitId", "state->secondaryTargetUnitId",
                  "effectsController->displayAnimation"):
        assert token in presentation, token
    assert presentation.index("std::move(pendingHeroOrderPresentation)") < presentation.index("setHeroAnimation(")
    assert end.index("waitForAnimations();") < end.index("presentAcceptedHeroOrder(action);")
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
    print("PASS: eight unique Order effects, Mirth Second Wind, accepted-transition routing and resource references")


if __name__ == "__main__":
    main()
