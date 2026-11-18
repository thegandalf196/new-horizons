#!/usr/bin/env python3
"""Living Armor starts blue at its bone-pile pose, then uses native resurrection."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPAWN = (ROOT / 'client/battle/LivingArmorSpawnPresentation.cpp').read_text()
COLOR = (ROOT / 'client/battle/BattleStackColorPresentation.h').read_text()
ANIMATION = (ROOT / 'client/battle/CreatureAnimation.cpp').read_text()


class LivingArmorSpawnPresentationTest(unittest.TestCase):
    def test_initial_bone_pose_is_held_until_hit_without_neutral_fade(self):
        self.assertLess(SPAWN.index('if(instant)'), SPAWN.index('new-horizons:livingArmor'))
        armor = SPAWN.split('if(stack->unitType()->getJsonKey()', 1)[1].split('// Other summons', 1)[0]
        self.assertIn('== "new-horizons:livingArmor" && !stack->isClone()', armor)
        self.assertIn('animation->framesInGroup(ECreatureAnimType::RESURRECTION) > 0', armor)
        self.assertLess(armor.index('animation->setType(ECreatureAnimType::RESURRECTION)'),
                        armor.index('owner.addToAnimationStage(EAnimationEvents::HIT'))
        self.assertIn('animation->playUntil(0)', armor)
        self.assertLess(armor.index('animation->playUntil(static_cast<size_t>(-1))'),
                        armor.index('new ResurrectionAnimation(owner, stack)'))
        self.assertNotIn('setStackColorFilter(', armor)
        self.assertNotIn('new ColorTransformAnimation(', armor)
        self.assertIn('creatureKey == "new-horizons:livingArmor" ? blue', COLOR)

    def test_reuses_reverse_death_and_keeps_other_summons_unchanged(self):
        reverse = ANIMATION.split('if(forward->size(size_t(ECreatureAnimType::RESURRECTION)) == 0)', 1)[1]
        self.assertIn('forward->size(size_t(ECreatureAnimType::DEATH)) - 1 - i', reverse)
        ordinary = SPAWN.split('// Other summons', 1)[1]
        for token in ('setStackColorFilter(Colors::TRANSPARENCY, 0', '"summonFadeIn"',
                      'if(stack->isClone())', '"cloning"'):
            self.assertIn(token, ordinary)
        controller = (ROOT / 'client/battle/BattleStacksController.cpp').read_text()
        self.assertIn('stageAddedStackAppearance(stack, instant)', controller)
        for forbidden in ('UnitChanges', 'addBonus', 'server.apply', 'battleMakeAction', 'BonusType::FROZEN'):
            self.assertNotIn(forbidden, SPAWN)


if __name__ == '__main__':
    unittest.main()
