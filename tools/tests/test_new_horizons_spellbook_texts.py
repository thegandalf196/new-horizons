"""Keep spellbook presentation references backed by canonical generated text."""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SpellbookTexts(unittest.TestCase):
    def test_all_presentation_keys_and_numeric_family_labels_resolve(self):
        files = [
            "client/windows/SpellbookEntryPresentation.cpp",
            "client/windows/CSpellWindow.cpp",
            "lib/spells/SpellEffectPreview.cpp",
            "lib/spells/TimedSpellEffectPreview.cpp",
            "lib/spells/TimedMagnitudeSpellEffectPreview.cpp",
            "lib/spells/AfflictionSpellEffectPreview.cpp",
            "lib/spells/DirectSpellEffectPreview.cpp",
            "lib/spells/ConditionalSpellEffectPreview.cpp",
            "lib/spells/PlacementSpellEffectPreview.cpp",
            "lib/spells/UtilityActionSpellEffectPreview.cpp",
            "lib/spells/KnownUtilitySpellEffectPreview.cpp",
            "lib/spells/BuffSpellEffectPreview.cpp",
            "lib/spells/StormSpellEffectPreview.cpp",
            "lib/spells/NatureSummonSpellEffectPreview.cpp",
            "lib/spells/RestorationSpellEffectPreview.cpp",
        ]
        sources = "\n".join((ROOT / name).read_text() for name in files)
        keys = set(re.findall(r'"(new-horizons\.spellbook\.[^"\n]+)"', sources))
        keys = {key for key in keys if not key.endswith(".")}
        for family in set(re.findall(r'(?:quantity|value)\("([a-zA-Z]+)"', sources)) | {"berserk", "realityWarp", "confusion", "puppetMaster", "teleport", "spellLock", "theChosen", "purify", "polymorph", "slow", "slowDual", "haste", "fortune", "implosion", "shadowGift", "naturesWrath", "pandemonium", "holyWrath", "handOfFate", "soulReaper"}:
            keys.update({"new-horizons.spellbook.effect." + family,
                         "new-horizons.spellbook.effect." + family + ".short"})
        canonical = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text())
        module = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())["translations"]
        keys.update(key + ".narrow" for key in canonical if key.startswith("new-horizons.spellbook.effect.") and key.endswith(".short"))
        self.assertFalse(any("%%" in value for key,value in canonical.items() if key.startswith("new-horizons.spellbook.")))
        self.assertTrue(keys)
        self.assertIn("new-horizons.spellbook.requirement", keys)
        self.assertIn("new-horizons.spellbook.or", keys)
        for key in sorted(keys):
            with self.subTest(key=key):
                self.assertIn(key, canonical)
                self.assertTrue(canonical[key])
                self.assertEqual(module.get(key), canonical[key])

    def test_school_requirement_keeps_rank_and_school_placeholders(self):
        canonical = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text())
        self.assertEqual(canonical["new-horizons.spellbook.requirement"].count("%s"), 2)
        self.assertTrue(canonical["new-horizons.spellbook.or"].strip())


if __name__ == "__main__":
    unittest.main()
