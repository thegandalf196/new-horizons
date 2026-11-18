#!/usr/bin/env python3
"""Source contracts for cast-local Overflow flight and delayed injury feedback."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
PRESENTATION = (ROOT / 'client/battle/OverflowSpellPresentation.cpp').read_text()
INTERFACE = (ROOT / 'client/battle/BattleInterface.cpp').read_text()
STACKS = (ROOT / 'client/battle/BattleStacksController.cpp').read_text()
SPELL = (ROOT / 'client/battle/BattleSpellPresentation.cpp').read_text()


class OverflowSpellPresentationTest(unittest.TestCase):
    def test_only_matching_manual_cast_supplies_a_stable_origin(self):
        begin = PRESENTATION.split('void OverflowSpellPresentation::begin', 1)[1].split('void OverflowSpellPresentation::remember', 1)[0]
        self.assertIn('reset();', begin)
        self.assertIn('action.actionType == EActionType::HERO_SPELL', begin)
        remember = PRESENTATION.split('void OverflowSpellPresentation::remember', 1)[1].split('bool OverflowSpellPresentation::queue', 1)[0]
        for guard in ('!receipt.activeCast', '!receipt.castByHero', 'receipt.casterStack >= 0',
                      'receipt.spellID != manualSpell', 'receipt.side != manualSide',
                      '!receipt.tile.isValid()', 'primaryHex.isValid()'):
            self.assertIn(guard, remember)
        self.assertIn('primaryHex = receipt.tile;', remember)
        self.assertIn('receipt.affectedCres.size() == 1', remember)
        self.assertIn('primary->coversPos(receipt.tile)', remember)
        self.assertIn('overflowSpellPresentation.remember(*this, receipt)', SPELL)
        self.assertIn('overflowSpellPresentation.begin(action)', INTERFACE)
        self.assertIn('overflowSpellPresentation.reset()', INTERFACE.split('void BattleInterface::endAction', 1)[1].split('void BattleInterface::startAction', 1)[0])

    def test_authored_flight_precedes_injury_without_a_second_cast(self):
        queue = PRESENTATION.split('bool OverflowSpellPresentation::queue', 1)[1]
        for guard in ('!injury.replaySpellHit', 'injury.spellEffect != manualSpell',
                      '!primaryHex.isValid()', 'spell->animationInfo.projectile.empty()'):
            self.assertIn(guard, queue)
        self.assertIn('getStackPositionAtHex(primaryHex, nullptr)', queue)
        self.assertIn('EAnimationEvents::SPELL_OVERFLOW', queue)
        stages = (ROOT / 'client/battle/BattleConstants.h').read_text()
        self.assertLess(stages.index('\n\tHIT,'), stages.index('\n\tSPELL_OVERFLOW,'))
        self.assertLess(stages.index('\n\tSPELL_OVERFLOW,'), stages.index('\n\tAFTER_HIT,'))
        self.assertIn('createSpellProjectile(nullptr, origin, destination, spell)', PRESENTATION)
        self.assertIn('emitStackProjectile(nullptr)', PRESENTATION)
        flight = PRESENTATION.split('void tick(uint32_t)', 1)[1].split('public:', 1)[0]
        self.assertLess(flight.index('hasActiveProjectile(nullptr, false)'), flight.index('onArrival();'))
        self.assertIn('queueInjuryAnimation(attackedInfo)', STACKS)
        self.assertIn('if(!owner.overflowSpellPresentation.queue(owner, attackedInfo, showInjury))', PRESENTATION)
        for forbidden in ('new HeroCastAnimation', 'BattleSpellCast(', 'damageWithContext', 'server.apply', 'battleMakeAction'):
            self.assertNotIn(forbidden, PRESENTATION)


if __name__ == '__main__':
    unittest.main()
