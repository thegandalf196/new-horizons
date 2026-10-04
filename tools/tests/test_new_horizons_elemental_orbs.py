"""Focused data guards for actual-element Orbs, independent of Magic Schools."""

import json
import re
import unittest
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "Mods/new-horizons/Content"


class ElementalOrbDataTest(unittest.TestCase):
    def test_all_four_orbs_replace_the_legacy_bonus(self):
        data = json.loads((CONTENT / "config/artifacts/elementalOrbs.json").read_text())
        expected = {
            "core:orbOfTheFirmament": "spellElementAir",
            "core:orbOfSilt": "spellElementEarth",
            "core:orbOfTempestuousFire": "spellElementFire",
            "core:orbOfDrivingRain": "spellElementWater",
        }
        self.assertEqual(set(data), set(expected))
        for identifier, element in expected.items():
            self.assertEqual(data[identifier]["bonuses"], {
                "spellDamage": {
                    "type": "ELEMENTAL_SPELL_DAMAGE",
                    "subtype": element,
                    "val": 25,
                    "valueType": "BASE_NUMBER",
                }
            })

    def test_actual_theme_tags_have_no_school_replacement(self):
        data = json.loads((CONTENT / "config/spells/elementalDamageTags.json").read_text())
        expected = {
            "core:fireball": "fire", "core:landMine": "fire",
            "core:fireWall": "fire", "core:inferno": "fire",
            "core:iceBolt": "water", "core:frostRing": "water",
            "core:lightningBolt": "air", "core:chainLightning": "air",
            "new-horizons:masterChainLightning": "air", "core:meteorShower": "earth",
            "core:landMineTrigger": "fire", "core:fireWallTrigger": "fire",
        }
        self.assertEqual(data, {key: {"damageElement": value} for key, value in expected.items()})
        for untagged in ("core:magicArrow", "core:armageddon", "core:implosion",
                         "core:earthquake", "new-horizons:lifeDrain",
                         "new-horizons:soulReaper", "core:fireShield"):
            self.assertNotIn(untagged, data)

    def test_module_registers_both_content_files(self):
        module = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())
        self.assertIn("config/artifacts/elementalOrbs.json", module["artifacts"])
        self.assertIn("config/spells/elementalDamageTags.json", module["spells"])

    def test_element_schema_is_explicit_and_rejects_schools(self):
        text = (ROOT / "config/schemas/spell.json").read_text()
        # VCMI schemas use JSON-with-comments; preserve quoted URLs/strings.
        text = re.sub(r'("(?:\\.|[^"\\])*")|//[^\n]*|/\*.*?\*/',
                      lambda match: match.group(1) or "", text, flags=re.S)
        text = re.sub(r'("(?:\\.|[^"\\])*")|,(?=\s*[}\]])',
                      lambda match: match.group(1) or "", text)
        schema = json.loads(text)
        element_schema = schema["properties"]["damageElement"]
        for element in ("none", "air", "fire", "water", "earth"):
            jsonschema.validate(element, element_schema)
        for invalid in ("havoc", "nature", "shadow", "neutral", "lightning", 0, None):
            with self.assertRaises(jsonschema.ValidationError):
                jsonschema.validate(invalid, element_schema)


if __name__ == "__main__":
    unittest.main()
