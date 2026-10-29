#!/usr/bin/env python3
"""Source guards for the ordinary upgrade confirmation; not rendered acceptance."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UpgradeConfirmationContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.caller = (ROOT / "client/windows/CCreatureWindow.cpp").read_text()
        cls.dialog = (ROOT / "client/windows/CreatureUpgradeConfirmation.cpp").read_text()
        cls.english = json.loads((ROOT / "Mods/vcmi/Content/config/translations/english.json").read_text())

    def test_selected_authoritative_offer_is_forwarded(self):
        self.assertIn("getAvailableUpgradeCosts().at(buttonIndex) * parent->info->creatureCount", self.caller)
        self.assertIn("const auto target = upgradeInfo.info.getAvailableUpgrades().at(buttonIndex)", self.caller)
        self.assertIn("creatureUpgradeConfirmation::show(totalCost, target, count, onUpgrade)", self.caller)
        self.assertIn("upgradeInfo.callback(upgradeInfo.info.getAvailableUpgrades().at(buttonIndex));", self.caller)
        self.assertNotIn("upgradeInfo.callback", self.dialog)

    def test_full_resource_zero_check_not_gold_only(self):
        self.assertIn("if(totalCost.empty())", self.dialog)
        self.assertNotIn("totalCost[", self.dialog)
        self.assertIn("TResources::nziterator i(totalCost)", self.dialog)
        self.assertIn("i->resType, i->resVal", self.dialog)

    def test_free_offer_names_destination_and_displays_count_and_zero(self):
        self.assertIn('createFromTextID("vcmi.creatureWindow.upgrade.free")', self.dialog)
        self.assertIn("getNameSingularTranslated() : creature->getNamePluralTranslated()", self.dialog)
        self.assertIn("ComponentType::RESOURCE, static_cast<GameResID>(GameResID::GOLD), 0", self.dialog)
        self.assertIn("resComps.back()->newLine = true", self.dialog)
        self.assertIn("ComponentType::CREATURE, target, static_cast<int32_t>(count)", self.dialog)
        self.assertEqual(self.english["vcmi.creatureWindow.upgrade.free"], "Upgrade to %s at no cost?")

    def test_paid_insufficient_and_cancel_keep_native_routes(self):
        self.assertIn("getResourceAmount().canAfford(totalCost)", self.dialog)
        self.assertIn("std::string message = LIBRARY->generaltexth->allTexts[207]", self.dialog)
        self.assertIn("showYesNoDialog(message, onUpgrade, nullptr, resComps)", self.dialog)
        self.assertIn("showInfoDialog(LIBRARY->generaltexth->allTexts[314], resComps)", self.dialog)
        self.assertLess(self.dialog.index("canAfford(totalCost)"), self.dialog.index("if(totalCost.empty())"))

    def test_multiple_upgrades_and_single_shortcut_remain(self):
        self.assertIn("std::min<size_t>(upgradeInfo.info.size(), upgrade.size())", self.caller)
        self.assertIn("buttonIndex < buttonsToCreate", self.caller)
        self.assertIn("if(buttonsToCreate == 1)", self.caller)
        self.assertIn("upgradeBtn->assignedKey = EShortcut::RECRUITMENT_UPGRADE", self.caller)

    def test_build_registration(self):
        cmake = (ROOT / "client/CMakeLists.txt").read_text()
        for suffix in ("cpp", "h"):
            self.assertEqual(cmake.count("windows/CreatureUpgradeConfirmation." + suffix), 1)


if __name__ == "__main__":
    unittest.main()
