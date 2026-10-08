#!/usr/bin/env python3
"""Focused Enchanted Command helper contract, alongside native execution tests."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EnchantedCommandContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "lib/battle/NewHorizonsEnchantedCommand.cpp").read_text()

    def test_captured_empowerment_not_current_readiness(self):
        self.assertIn("order.warcastingBonusPercent > 0", self.source)
        self.assertIn("hero->hasActivePerk(SKILL, PERK)", self.source)
        self.assertNotIn("getWarcastingState", self.source)
        self.assertNotIn("orderBonus(", self.source)

    def test_native_duration_and_nonstacking_identity(self):
        self.assertIn("BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::MORALE", self.source)
        self.assertIn("bonus.stacking = PERK", self.source)
        self.assertIn("BonusSource::SECONDARY_SKILL", self.source)

    def test_read_only_recipient_filter(self):
        for token in ("BattleSide::ATTACKER", "BattleSide::DEFENDER", "order.issuedRound != battle.battleGetRound()", "!unit->alive()", "unit->isGhost()", "battle.battleGetOwner(unit) != owner", "isOrdinaryCreatureAttacker(unit)"):
            self.assertIn(token, self.source)
        for token in ("sendAndApply", "addUnitBonus", "randomizer", "getRandomGenerator", "const_cast"):
            self.assertNotIn(token, self.source)

    def test_targeted_coverage_and_melee_capable_shooters(self):
        protect = self.source.split("case HeroCommand::PROTECT:", 1)[1].split("break;", 1)[0]
        wind = self.source.split("case HeroCommand::SECOND_WIND:", 1)[1].split("break;", 1)[0]
        self.assertIn("primaryTargetUnitId", protect)
        self.assertIn("secondaryTargetUnitId", protect)
        self.assertIn("primaryTargetUnitId", wind)
        self.assertNotIn("secondaryTargetUnitId", wind)
        melee_orders = self.source.split("case HeroCommand::CHARGE:", 1)[1].split("default:", 1)[0]
        self.assertNotIn("isMeleeAttacker", melee_orders)
        focus = self.source.split("case HeroCommand::FOCUS_FIRE:", 1)[1].split("break;", 1)[0]
        self.assertIn("hasCombinedArms", focus)


if __name__ == "__main__":
    unittest.main()
