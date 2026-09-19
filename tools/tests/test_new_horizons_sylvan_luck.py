#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main():
    skills = json.loads((ROOT / "config/newHorizonsSkills.json").read_text())
    sylvan_luck = skills["sylvanLuck"]
    expected = {
        "basic": (1, 25),
        "advanced": (2, 60),
        "expert": (3, 100),
    }
    for rank, (luck, lucky_damage) in expected.items():
        effects = sylvan_luck[rank]["effects"]
        assert effects["luck"] == {
            "type": "LUCK", "valueType": "BASE_NUMBER", "val": luck
        }
        assert effects["luckyStrikeDamage"] == {
            "type": "LUCKY_STRIKE_DAMAGE_PERCENTAGE",
            "valueType": "BASE_NUMBER",
            "val": lucky_damage,
        }

    perks = json.loads((ROOT / "config/newHorizonsPerks.json").read_text())
    registry = perks["skills"]["new-horizons:sylvanLuck"]
    for rank in expected:
        assert registry["ranks"][rank]["effect"]["status"] == "active"

    bonus_enum = (ROOT / "lib/bonuses/BonusEnum.h").read_text()
    assert "BONUS_NAME(LUCKY_STRIKE_DAMAGE_PERCENTAGE)" in bonus_enum
    damage = (ROOT / "scripts/damage/damageCalculator.lua").read_text()
    assert 'getBonusValueOfType(info.attacker, info.attackerBonuses, "LUCKY_STRIKE_DAMAGE_PERCENTAGE") / 100' in damage
    print("PASS: Sylvan Luck ranks grant +1/+2/+3 Luck and 2.25x/2.60x/3.00x lucky strikes")


if __name__ == "__main__":
    main()
