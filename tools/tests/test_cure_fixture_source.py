"""Asset/process-free deterministic fixture source boundaries; not compiled acceptance."""
from pathlib import Path
import hashlib
import unittest

ROOT = Path(__file__).resolve().parents[2]

class FixtureSource(unittest.TestCase):
    def test_macro_off_flow_is_exact_beforeimage(self):
        source = (ROOT/'server/battles/BattleFlowProcessor.cpp').read_text()
        for fragment in ('#ifdef NH_CURE_SIMULATION_FIXTURE\n#include "../tests/CureFixture.h"\n#endif\n',
            '#ifdef NH_CURE_SIMULATION_FIXTURE\n\t\t\t\tnext = cureFixture::beforeHumanActivation(*gameHandler, battle, *next);\n#endif\n'):
            self.assertEqual(source.count(fragment), 1)
            source = source.replace(fragment, '')
        # Frozen macro-OFF baseline: independent of checkout history and HEAD.
        self.assertEqual(hashlib.sha256(source.encode()).hexdigest(),
            '6ea602e746323362dab6dd147ac64c30945891467b777ca8bac072052632ef84')

    def test_one_authoritative_publication_reacquires_before_activation(self):
        source = (ROOT/'server/tests/CureFixture.cpp').read_text()
        ordered = ('owner->pendingFor(', 'validateWorld(handler.gameState(), battle);',
            'validateArmies(battle, next)', 'handler.sendAndApply(effect);',
            'const auto * current = handler.gameState().getBattle(battleID);',
            'target = current->battleGetStackByID(targetID);', 'owner->published(', 'return active;')
        self.assertEqual([source.index(x) for x in ordered], sorted(source.index(x) for x in ordered))
        self.assertEqual(source.count('handler.sendAndApply('), 1)
        self.assertIn('stack->getInitiative() == (slot == 0 ? 6 : 9)', source)
        for forbidden in ('getRandomGenerator', 'resolveAdverseCombatRoll', 'srand(', 'makePlayerBattleAction(',
                'setActiveStack(', 'restoreFrozenApplicationRound', 'recordFrozenApplication'):
            self.assertNotIn(forbidden, source)
        for required in ('stack->waited()', 'battle.battleGetRound() == 1', 'SlotID(1)',
                'hero->hasSpellbook()', 'hero->getManaAvailable() >= 4', 'battleGetSpellActionAllowance',
                'stack->getCount()', 'frozenLastAppliedRound() == 1', 'current == initialBattle'):
            if required == 'stack->getCount()':
                required = 'stack.getCount()'
            self.assertIn(required, source)

    def test_no_ordinary_target_macro_or_class_layout_change(self):
        cmake = (ROOT/'server/CMakeLists.txt').read_text()
        special = cmake.split('if(ENABLE_NH_CURE_CLIENT_INTEGRATION)',1)[1]
        self.assertIn('OBJECT EXCLUDE_FROM_ALL', special)
        self.assertIn('battles/BattleFlowProcessor.cpp tests/CureFixture.cpp', special)
        self.assertNotIn('NH_CURE_SIMULATION_FIXTURE', cmake.split('if(ENABLE_NH_CURE_CLIENT_INTEGRATION)',1)[0])
        self.assertNotIn('enable_pch(nhCureSimulationFixture)', cmake)
        self.assertNotIn('install(', special)
        client = (ROOT/'clientapp/CMakeLists.txt').read_text()
        self.assertIn('PRIVATE nhCureSimulationFixture vcmiclientcommon', client)
        for name in ('server/CGameHandler.h','server/CVCMIServer.h','server/battles/BattleFlowProcessor.h',
                'server/battles/BattleProcessor.h'):
            self.assertNotIn('NH_CURE_SIMULATION_FIXTURE', (ROOT/name).read_text())

    def test_replica_and_real_presenter_gates(self):
        bootstrap = (ROOT/'client/tests/CureIntegrationBootstrap.cpp').read_text()
        for required in ('fixture->isReceipt(', 'target.getAvailableHealth()', 'target.frozenLastAppliedRound() == 1',
                'newHorizonsMagic::cureAfflictions', 'Cure deterministic fixture replica READY:'):
            self.assertIn(required, bootstrap)
        driver = (ROOT/'client/tests/CureIntegrationDriver.cpp').read_text()
        self.assertLess(driver.index('bootstrap.verifyFixtureTarget(initialTarget, *callback);'),
            driver.index('modal = showCureAfflictionModal('))
        self.assertIn('bootstrap.verifyReplicatedFixture(*current->getBattle());', driver)

if __name__=='__main__':unittest.main()
