"""Town economy routing and accepted-state readback stay presentation-only."""
from pathlib import Path
import unittest
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from import_new_horizons_spell_art import parse_jsonc


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


class TownAdministrationUITest(unittest.TestCase):
    def test_income_menu_keeps_market_and_administration_separate(self):
        source = read("client/windows/TownEstateUI.cpp")
        self.assertIn('setTextOverlay("Marketplace"', source)
        self.assertIn('setTextOverlay("Town Administration"', source)
        self.assertIn('TownIncomeMenu>(\n\t\tGAME->interface()->playerID, std::move(marketplace)', source)
        self.assertIn('town.getOwner() != GAME->interface()->playerID || !GAME->interface()->makingTurn', source)
        # Reuse the existing marketplace routing, including remote owned markets.
        self.assertIn('getTownsInfo(true)', source)
        castle = read("client/windows/CCastleInterface.cpp")
        self.assertIn('townEstateUI::showIncomeMenu(*town, [buildings = builds] { buildings->enterAnyMarket(); })', castle)
        self.assertNotIn('stewardButton', castle)
        self.assertNotIn('createStewardButton', source)

    def test_shortcut_and_cancel_do_not_confirm_an_appointment(self):
        bindings = parse_jsonc(read("config/keyBindingsConfig.json"))
        self.assertEqual(bindings["keyboard"]["townOpenAdministration"], "S")
        self.assertIn('"townOpenAdministration", EShortcut::TOWN_OPEN_ADMINISTRATION',
                      read("client/gui/ShortcutHandler.cpp"))
        source = read("client/windows/TownEstateUI.cpp")
        self.assertIn('buttons[1]->assignedKey = EShortcut::TOWN_OPEN_ADMINISTRATION', source)
        self.assertIn('if(key == EShortcut::GLOBAL_CANCEL)\n\t\t\tclose();', source)
        self.assertIn('WindowBase::close();', source)
        self.assertIn('callback->assignTownSteward(townID, candidates[index]);', source)

    def test_gold_refresh_follows_replicated_state_without_payment(self):
        source = read("client/EstatesNetPacks.cpp")
        self.assertLess(source.index('visitSetResources(notification);'), source.index('window->refreshIncome();'))
        self.assertIn('window->town->getOwner() == pack.owner', source)
        self.assertIn('settings["session"]["headless"].Bool()', source)
        ui = read("client/windows/TownEstateUI.cpp")
        refresh = ui[ui.index('void CCastleInterface::refreshIncome()'):]
        self.assertIn('town->dailyIncome()[EGameResID::GOLD]', refresh)
        self.assertIn('redraw();', refresh)
        for mutation in ('giveResource', 'setResource', 'sendAndApply', 'addResource'):
            self.assertNotIn(mutation, refresh)
