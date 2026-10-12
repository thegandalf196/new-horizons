#!/usr/bin/env python3
"""Presentation source gate; no game, renderer, audio or process launch."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class HeroPositiveProcPresentationTest(unittest.TestCase):
    def test_existing_positive_resources_only(self):
        effects = (ROOT/'client/battle/BattleEffectsController.cpp').read_text()
        method = effects.split('void BattleEffectsController::displayHeroPositiveEffect', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('effect != EBattleEffect::GOOD_LUCK && effect != EBattleEffect::GOOD_MORALE', method)
        self.assertIn('"GOODLUCK" : "GOODMRLE"', method)
        self.assertIn('graphics->battleACToDef', method)
        self.assertIn('EffectAnimation::FORCE_ON_TOP', method)
        self.assertNotIn('getPosition()', method)

    def test_spell_flags_or_one_luck_cue(self):
        helper = (ROOT/'client/battle/HeroPositiveProcPresentation.h').read_text()
        self.assertIn('luckyDay || erraticMana || druidicLuck', helper)
        self.assertEqual(helper.count('EBattleEffect::GOOD_LUCK'), 1)
        self.assertEqual(helper.count('EBattleEffect::GOOD_MORALE'), 1)

    def test_actual_hero_anchor_both_sides(self):
        hero = (ROOT/'client/battle/BattleHero.cpp').read_text()
        self.assertIn('Point(pos.center().x, pos.y + pos.h / 4) - parent->pos.topLeft()', hero)
        interface = (ROOT/'client/battle/BattleInterface.cpp').read_text()
        method = interface.split('void BattleInterface::presentHeroPositiveProcs', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('side != BattleSide::ATTACKER && side != BattleSide::DEFENDER', method)
        self.assertIn('side == BattleSide::DEFENDER ? defendingHero : attackingHero', method)
        self.assertIn('hero->positiveProcAnchor()', method)
        self.assertIn('i == 0 ? EAnimationEvents::BEFORE_HIT : EAnimationEvents::HIT', method)

    def test_authoritative_original_spell_and_consumed_order_only(self):
        interface = (ROOT/'client/battle/BattleInterface.cpp').read_text()
        self.assertIn('sc->activeCast && sc->castByHero && sc->casterStack < 0 && sc->heroLuck', interface)
        order = interface.split('void BattleInterface::presentAcceptedHeroOrder', 1)[1].split('\nvoid ', 1)[0]
        self.assertLess(order.index('std::move(pendingHeroOrderPresentation)'), order.index('presentHeroPositiveProcs('))
        self.assertIn('state->heroLuck->has(newHorizonsHeroLuck::Proc::LuckyDay)', order)
        self.assertIn('state->heroMorale->has(newHorizonsHeroLuck::Proc::HeroicSpirit)', order)

if __name__ == '__main__':
    unittest.main()
