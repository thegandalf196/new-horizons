#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard the staged-animation pump used by immediate Metamagic casts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "client/battle/BattleInterface.cpp").read_text()
SPELLS = (ROOT / "config/spells/offensive.json").read_text()


def body(name: str, following: str) -> str:
    return SOURCE.split("void BattleInterface::" + name, 1)[1].split(
        "void BattleInterface::" + following, 1
    )[0]


def drain(stages):
    """Model executeStagedAnimations: stop only when animation work takes over."""
    executed = []
    animation_started = False
    while stages and not animation_started:
        earliest = min(stage for stage, _ in stages)
        current = [entry for entry in stages if entry[0] == earliest]
        stages = [entry for entry in stages if entry[0] != earliest]
        for stage, starts_animation in current:
            executed.append(stage)
            animation_started |= starts_animation
    return executed, stages, animation_started


def main():
    execute = body("executeStagedAnimations", "executeAnimationStage")
    wait = body("waitForAnimations", "hasAnimations")
    spell_cast = body("spellCast", "battleStacksEffectsSet")

    # Magic Arrow really uses the projectile path; this is not merely a generic
    # buff/effect test such as Curse.
    magic_arrow = SPELLS.split('"magicArrow"', 1)[1].split('"iceBolt"', 1)[0]
    assert '"projectile"' in magic_arrow
    assert "HeroCastAnimation" in spell_cast
    assert "EAnimationEvents::BEFORE_HIT" in spell_cast
    assert "EAnimationEvents::HIT" in spell_cast

    # Sound-only BEFORE_HIT must not strand the projectile/effect HIT stage.
    executed, remaining, started = drain([(0, False), (1, True), (2, False)])
    assert executed == [0, 1] and started and remaining == [(2, False)]
    executed, remaining, started = drain([(1, False), (2, False)])
    assert executed == [1, 2] and not started and not remaining

    for token in (
        "while(true)",
        "const auto startsBeforeStage = animationStartGeneration;",
        "executeAnimationStage(earliestStage);",
        "animationStartGeneration != startsBeforeStage",
        "earliestStage == EAnimationEvents::COUNT",
    ):
        assert token in execute, token
    for token in (
        "while(hasAnimations() || !awaitingEvents.empty())",
        "if(!hasAnimations())",
        "executeStagedAnimations();",
        "ongoingAnimationsState.waitWhileBusy();",
    ):
        assert token in wait, token
    assert "awaitingEvents.clear()" not in wait

    packets = (ROOT / "client/NetPacksClient.cpp").read_text()
    attack = packets.split("void ApplyFirstClientNetPackVisitor::visitBattleAttack", 1)[1].split(
        "void ApplyClientNetPackVisitor::visitBattleAttack", 1)[0]
    assert attack.index("::battleAttack,") < attack.index("::battleStacksAttacked,") < attack.index("::battleAttackPresentationComplete,")
    injuries = packets.split("void ApplyFirstClientNetPackVisitor::visitStacksInjured", 1)[1].split("\nvoid ", 1)[0]
    assert "battleAttackPresentationComplete" not in injuries
    player = (ROOT / "client/CPlayerInterface.cpp").read_text()
    completion = player.split("void CPlayerInterface::battleAttackPresentationComplete", 1)[1].split("\nvoid ", 1)[0]
    for token in ("BATTLE_EVENT_POSSIBLE_RETURN", "const auto interface = battleInt;",
                  "interface && interface->getBattleID() == battleID", "interface->waitForAnimations();"):
        assert token in completion, token
    stacks = (ROOT / "client/battle/BattleStacksController.cpp").read_text()
    assert "currentAnimations.empty() && !owner.hasQueuedStage(EAnimationEvents::BEFORE_HIT)" in stacks

    print("PASS: spell hit batches remain deferred and physical packets finish presentation before state apply")


if __name__ == "__main__":
    main()
