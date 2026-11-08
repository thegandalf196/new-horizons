# SPDX-License-Identifier: GPL-2.0-or-later
"""Current finite-Command UI source guards; not native or rendered acceptance."""
from pathlib import Path
import unittest
from tools.tests.test_new_horizons_perfect_moment_client import function

BATTLE = Path(__file__).resolve().parents[2] / 'client/battle'

class FiniteCommandClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.interface=(BATTLE/'BattleInterface.cpp').read_text()
        cls.panel=(BATTLE/'BattleHeroActionWindow.cpp').read_text()
        cls.availability=(BATTLE/'BattleHeroActionAvailability.cpp').read_text()
        cls.controller=(BATTLE/'BattleActionsController.cpp').read_text()
        cls.window=(BATTLE/'BattleWindow.cpp').read_text()

    def test_current_ui_has_no_retired_mandatory_order_choice(self):
        for source in (self.interface,self.panel,self.availability,self.controller,self.window):
            for retired in ('battleHasPendingDoubleCommand','presentPendingHeroOrderChoice',
                            'Double Command: choose a different Order now'):
                self.assertNotIn(retired,source)

    def test_display_uses_remaining_and_maximum_without_refilling(self):
        body=function(self.availability,'void BattleHeroActionWindow::refresh()')
        for token in ('getCommandResources(side)','resources.remaining','resources.maximum',
                      'Each Order costs 1 Command and 1 Hero Action'):
            self.assertIn(token,body)
        self.assertNotIn('initialize',body)
        self.assertNotIn('remaining =',body)

    def test_command_exhaustion_precedes_target_enumeration(self):
        self.assertIn('No Command remains. Command is not replenished during combat.',self.availability)
        body=function(self.availability,'bool BattleHeroActionWindow::refreshCommandButtons')
        self.assertLess(body.index('commandUnavailableReason(*owner)'),body.index('battleGetHeroCommandTargets'))
        self.assertIn('commonAvailable && supported && targeted',body)
        self.assertIn('newHorizonsCommand::available(*callback, side)',self.availability)

    def test_spell_availability_is_independent_of_command_exhaustion(self):
        body=function(self.availability,'void BattleHeroActionWindow::refresh()')
        self.assertIn('callback->battleCanCastSpell(hero, spells::Mode::HERO)',body)
        self.assertIn('spellButton->block(!canAct || !canSpell)',body)
        self.assertNotIn('spellButton->block(!anyCommand',body)

    def test_dialog_cancel_is_enabled_and_spends_nothing(self):
        body=function(self.panel,'void BattleHeroActionWindow::cancelSelection()')
        self.assertIn('close();',body)
        for mutation in ('sendCommand','giveCommand','battleMake','consume','spend'):
            self.assertNotIn(mutation+'(',body)
        refresh=function(self.availability,'void BattleHeroActionWindow::refresh()')
        self.assertIn('cancel->block(false)',refresh)

    def test_right_click_and_escape_cancel_targeting_without_reopening(self):
        body=function(self.controller,'void BattleActionsController::onHexRightClicked')
        begin=body.index('if(heroOrderTargetingModeActive())')
        cancel=body[begin:body.index('if(repeatedPlacementModeActive())',begin)]
        self.assertIn('cancelHeroOrderTargeting();',cancel)
        self.assertIn('return;',cancel)
        self.assertNotIn('sendCommand',cancel)
        self.assertNotIn('giveCommand',cancel)
        begin=self.window.index('addShortcut(EShortcut::GLOBAL_CANCEL')
        shortcut=self.window[begin:self.window.index('setShortcutBlocked',begin)]
        self.assertIn('endCastingSpell();',shortcut)
        ending=function(self.controller,'void BattleActionsController::endCastingSpell()')
        self.assertIn('cancelHeroOrderTargeting();',ending)

if __name__=='__main__':unittest.main()
