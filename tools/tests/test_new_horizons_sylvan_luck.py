#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main():
    skills = json.loads((ROOT / "config/newHorizonsSkills.json").read_text())
    sylvan_tactics = skills["sylvanLuck"]
    expected = {
        "basic": (1, 25),
        "advanced": (2, 60),
        "expert": (3, 100),
    }
    assert sylvan_tactics["name"] == "Sylvan Tactics"
    for rank, (initiative, _) in expected.items():
        assert sylvan_tactics[rank]["effects"] == {
            "initiative": {
                "type": "STACKS_INITIATIVE_FLAT",
                "valueType": "BASE_NUMBER", "val": initiative,
            }
        }

    perks = json.loads((ROOT / "config/newHorizonsPerks.json").read_text())
    registry = perks["skills"]["new-horizons:sylvanLuck"]
    assert perks["sylvanTacticsPolicy"] == 5
    for rank in expected:
        assert registry["ranks"][rank]["effect"]["status"] == "active"

    # The absent-policy capture keeps the earlier Luck identity and multipliers.
    # It is intentionally independent of the installed Initiative definitions.
    historical = json.loads((ROOT / "test/testdata/vcmi-test/content/test/"
                            "newHorizonsHistoricalHeroLuckPerks.json").read_text())
    assert "sylvanTacticsPolicy" not in historical
    legacy = historical["skills"]["new-horizons:sylvanLuck"]
    assert legacy["name"] == "Sylvan Luck"
    for rank, (luck, lucky_damage) in expected.items():
        text = legacy["ranks"][rank]["description"]
        assert f"+{luck} Luck" in text
        assert f"{2 + lucky_damage / 100:.2f}x" in text

    bonus_enum = (ROOT / "lib/bonuses/BonusEnum.h").read_text()
    assert "BONUS_NAME(LUCKY_STRIKE_DAMAGE_PERCENTAGE)" in bonus_enum
    damage = (ROOT / "lib/combatScripts/NativeDamageCalculator.cpp").read_text()
    assert "facts.attacker.value(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE) / 100" in damage
    print("PASS: Current Sylvan Tactics grants Initiative; historical absent-policy Sylvan Luck retains its captured multipliers")


if __name__ == "__main__":
    main()
