"""Prayer catalogue uses the native spellbook text and shared effect readout."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PrayerCatalogueUITest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "client/windows/SpellbookPrayers.cpp").read_text()

    def test_native_spellbook_font_and_separate_page_headings(self):
        self.assertNotIn("FONT_BIG", self.source)
        self.assertNotIn("FONT_SMALL", self.source)
        self.assertIn("8 + entryWidth / 2, 22, FONT_TINY", self.source)
        self.assertIn("Rect(rightColumn, 16, entryWidth, 46)", self.source)
        self.assertNotIn("bounds.w / 2", self.source)

    def test_faith_shown_once_and_effects_still_use_actual_strength(self):
        self.assertEqual(self.source.count('"Faith " + std::to_string(faith)'), 1)
        self.assertNotIn('"Potency Faith', self.source)
        self.assertIn("ActivePrayer snapshot{prayer, potency, rounds, policy}", self.source)
        self.assertIn("prayerEffect(snapshot, 1)", self.source)
        self.assertIn("strengthBonus > 0", self.source)
        self.assertIn('"\\n\\nDuration: "', self.source)

    def test_six_readonly_entries_and_name_filter_remain(self):
        self.assertEqual(self.source.count('"NH_prayer_'), 6)
        self.assertIn("TextOperations::isFuzzyMatch(search, entryNames[index])", self.source)
        self.assertIn("WindowBase::close();", self.source)
        self.assertNotIn("castSpell", self.source)
