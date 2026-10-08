"""Focused source contracts for Transformer UI prediction and reconciliation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "client/windows/GUIClasses.cpp").read_text()


def body(name, next_name):
    return SOURCE.split(name, 1)[1].split(next_name, 1)[0]


class TransformerUIContractTest(unittest.TestCase):
    def test_native_art_and_shared_pooled_preview(self):
        constructor = body("CTransformerWindow::CTransformerWindow(", "void CTransformerWindow::close()")
        self.assertIn('ImagePath::builtin("SKTRNBK")', constructor)
        preview = body("void CTransformerWindow::updateConversionPreview()", "bool CTransformerWindow::holdsGarrison")
        self.assertIn("newHorizonsSkeletonTransformer::plan(", preview)
        self.assertIn("getHeroCapabilityRules()", preview)
        self.assertIn("preview.skeletonCount", preview)
        self.assertIn("convert->block(!preview.isReady())", preview)
        for status in ("ZERO_OUTPUT", "HP_OVERFLOW", "OUTPUT_CAPACITY", "LEADERSHIP_LIMIT"):
            self.assertIn(status, preview)

    def test_nh_request_is_single_vector_and_not_fake_confirmation(self):
        deal = body("void CTransformerWindow::makeDeal()", "void CTransformerWindow::addAll()")
        canonical = deal.split("if(preview.status !=", 1)[1].split("\n\tfor(auto & elem", 1)[0]
        self.assertEqual(canonical.count("cb->trade("), 1)
        self.assertIn("if(!preview.isReady())", canonical)
        self.assertIn("std::vector<TradeItemSell> sources", canonical)
        self.assertIn("std::vector<TradeItemBuy>{}", canonical)
        self.assertNotIn("playSound", canonical)
        self.assertIn("item->move()", canonical)
        self.assertIn("redraw()", canonical)

    def test_stale_slot_replacement_clears_selection(self):
        update = body("bool CTransformerWindow::CItem::update()", "CTransformerWindow::CItem::CItem(")
        self.assertIn("hasStackAtSlot", update)
        self.assertIn("displayedCreature != creature->getId() || size != currentSize", update)
        self.assertIn("move()", update)
        reconcile = body("void CTransformerWindow::updateGarrisons()", "std::vector<SlotID> CTransformerWindow::selectedSourceSlots()")
        self.assertIn("removeChild", reconcile)
        self.assertIn("!slot.validSlot() || !stack", reconcile)
        self.assertIn("redraw()", reconcile)

    def test_town_and_hero_updates_match_displayed_army(self):
        holder = body("bool CTransformerWindow::holdsGarrison(", "CTransformerWindow::CTransformerWindow(")
        self.assertIn("return army == this->army", holder)


if __name__ == "__main__":
    unittest.main()
