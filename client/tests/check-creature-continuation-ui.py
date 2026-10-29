#!/usr/bin/env python3
"""Source binding guards; native policy cases and rendered continuation remain separate."""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ContinuationUIBinding(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.interface = (ROOT / 'client/battle/BattleInterface.cpp').read_text()
        cls.actions = (ROOT / 'client/battle/BattleActionsController.cpp').read_text()
        cls.window = (ROOT / 'client/battle/BattleWindow.cpp').read_text()
        cls.read = (ROOT / 'client/battle/CreatureContinuationUI.cpp').read_text()
        cls.policy = (ROOT / 'client/battle/CreatureContinuationUI.h').read_text()

    def test_raw_pending_and_authoritative_query_contract(self):
        self.assertIn('state->dreadCavalryState.pending', self.read)
        for name in ('battleHasPendingDreadCavalry', 'battleGetDreadCavalryDamagePercent',
                     'battleGetDreadCavalryMovementRemaining', 'battleHasPendingRangedFollowUp'):
            self.assertIn(name + '(stack)', self.read)
        for forbidden in ('core:tamika', 'hasActivePerk', 'battleGetDistances', 'getReachability',
                          'damagePercent /', 'damagePercent *', 'pursuitMovementRemaining ='):
            self.assertNotIn(forbidden, self.read)

    def test_both_command_paths_revalidate_before_submission(self):
        self.assertIn('read(*getBattle(), activeStack).normalizeCommand(action)', self.interface)
        self.assertIn('read(*getBattle(), actor).normalizeCommand(command.actionType)', self.interface)
        self.assertLess(self.interface.index('normalizeCommand(command.actionType)'),
                        self.interface.index('curInt->cb->battleMakeUnitAction'))
        self.assertIn('action = EActionType::NO_ACTION', self.policy)
        self.assertNotIn('action = EActionType::WAIT', self.policy)

    def test_existing_melee_legality_and_no_rescaled_preview(self):
        self.assertIn('return !continuation.allows(action.get())', self.actions)
        for binding in ('battleCanAttackUnitAction(currentStack, targetStack)',
                        'battleCanAttackHex(currentStack, targetHex)',
                        'battleEstimateDamage(attackInfo, &retaliation)',
                        'BattleAction::makeMeleeAttack(attacker, targetHex, attackFromHex, returnAfterAttack)'):
            self.assertIn(binding, self.actions)
        self.assertIn('creatureContinuationUI::attackPreview(', self.actions)
        self.assertNotIn('50', self.read)
        self.assertIn('text.replaceNumber(state.damagePercent)', self.read)

    def test_wait_disabled_and_native_close_control_restored(self):
        self.assertIn('!canWait || continuationPending', self.window)
        self.assertIn('widget<CButton>("wait")->setEnabled(!continuationPending', self.window)
        self.assertIn('creatureContinuationUI::closeTitle(continuation', self.window)
        self.assertIn('CButton::tooltipLocalized("core.help.387")', self.window)
        self.assertIn('EActionType::SHOOT : EActionType::WALK_AND_ATTACK', self.policy)
        self.assertIn('Decline the pending Master Gunner second shot and end this activation.', self.read)

    def test_skip_charge_copy_preserves_native_pursuit_tail(self):
        texts = json.loads((ROOT / 'config/newHorizonsCombatTexts.json').read_text())
        shipped = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())['translations']
        for suffix in ('skip', 'skip.help', 'preview'):
            key = 'new-horizons.combat.dreadCavalry.' + suffix
            self.assertEqual(texts[key], shipped[key])
        self.assertEqual(texts['new-horizons.combat.dreadCavalry.skip'], 'Skip charge')
        self.assertIn('Any existing Pursuit movement remains', texts['new-horizons.combat.dreadCavalry.skip.help'])
        self.assertIn('no movement left', texts['new-horizons.combat.dreadCavalry.preview'])
        self.assertIn('%d% damage', texts['new-horizons.combat.dreadCavalry.preview'])
        self.assertNotIn('%d%%', texts['new-horizons.combat.dreadCavalry.preview'])

    def test_build_registrations_use_existing_targets(self):
        client = (ROOT / 'client/CMakeLists.txt').read_text()
        tests = (ROOT / 'test/CMakeLists.txt').read_text()
        self.assertEqual(client.count('battle/CreatureContinuationUI.cpp'), 1)
        self.assertEqual(client.count('battle/CreatureContinuationUI.h'), 1)
        self.assertEqual(tests.count('battle/CreatureContinuationUITest.cpp'), 1)


if __name__ == '__main__':
    unittest.main()
