"""Keep spellbook School-lock presentation aligned with authoritative casting."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ScrollCastingUI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "client/windows/CSpellWindow.cpp").read_text()

    def test_school_lock_does_not_reject_authoritatively_castable_scroll(self):
        area = self.source.split("void CSpellWindow::SpellArea::setSpell(", 1)[1]
        expression = area.split("schoolLocked = requiredRank", 1)[1].split(";", 1)[0]
        self.assertIn("> 0 && !inscribedInSpellbook", expression)
        self.assertIn("!newHorizonsMagic::hasSchoolProficiency(owner->myHero, mySpell->getId())", expression)
        self.assertIn("&& !owner->myHero->canCastThisSpell(mySpell)", expression)
        self.assertNotIn("SPELL_SCROLL", expression)
        self.assertNotIn("getSourcesForSpell", expression)

    def test_other_casting_locks_are_not_replaced_by_school_eligibility(self):
        self.assertIn("divineMandateLocked = !owner->canUseSpellForCurrentDivineMandateFollowup", self.source)
        self.assertIn("spellCost > owner->myHero->getManaAvailable()", self.source)
        self.assertIn("owner->isAdventureSpellUsedToday(mySpell->id)", self.source)

    def test_spell_selection_still_uses_authoritative_availability(self):
        self.assertIn("myHero->canCastThisSpell(spell.get()) || ownedOrGranted", self.source)
        self.assertIn("if(schoolLocked)", self.source)
        self.assertIn("GAME->interface()->showInfoDialog(schoolRequirementText);", self.source)


if __name__ == "__main__":
    unittest.main()
