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

    print("PASS: projectile Metamagic stages drain through sound-only gaps before EndAction resumes control")


if __name__ == "__main__":
    main()
