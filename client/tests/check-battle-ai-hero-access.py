#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard player-scoped BattleAI paths from polling an unknown enemy hero."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ATTACK = (ROOT / "AI/BattleAI/AttackPossibility.cpp").read_text()
EVALUATOR = (ROOT / "AI/BattleAI/BattleEvaluator.cpp").read_text()


def between(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def main():
    count = between(ATTACK, "int AttackPossibility::getAttackCount", "AttackPossibility AttackPossibility::evaluate")
    obstacles = between(ATTACK, "void DamageCache::buildObstacleDamageCache", "void DamageCache::buildDamageCache")
    orders = between(EVALUATOR, "float canonicalOrderHeuristic", "std::vector<BattleHex> BattleEvaluator::getBrokenWallMoatHexes")

    guards = (
        (count, "perspective == BattleSide::ALL_KNOWING || perspective == attacker.unitSide()",
         "attackerHeroKnown ? state.battleGetFightingHero(attacker.unitSide()) : nullptr"),
        (obstacles, "perspective == BattleSide::ALL_KNOWING || perspective == spellObstacle->casterSide",
         "casterKnown ? hb->battleGetFightingHero(spellObstacle->casterSide) : nullptr"),
        (orders, "perspective == BattleSide::ALL_KNOWING || perspective == side",
         "heroKnown ? battle.battleGetFightingHero(side) : nullptr"),
    )
    for body, perspective_guard, guarded_access in guards:
        assert perspective_guard in body, perspective_guard
        assert guarded_access in body, guarded_access
        assert body.index(perspective_guard) < body.index(guarded_access)

    # Unknown enemy attack prediction retains only public unit state and cannot
    # trigger CBattleInfoEssentials' access-check logger in a hot simulation loop.
    assert "int result = attacker.getTotalAttacks(shooting);" in count
    print("PASS: BattleAI hero reads are perspective-gated; unknown enemy attacks use exposed unit state")


if __name__ == "__main__":
    main()
