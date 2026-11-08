#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-routing contract; renderer and native gameplay remain separate gates."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
interface = (ROOT / 'client/battle/BattleInterface.cpp').read_text()
queue = (ROOT / 'client/battle/BattleSpellPresentation.cpp').read_text()
selector = (ROOT / 'client/battle/PoisonPresentation.h').read_text()
effects = (ROOT / 'client/battle/BattleEffectsController.cpp').read_text()

assert 'queueSpellAffectedAnimations(*sc, spell)' in interface
assert 'acceptedCastOverride(receipt.spellID, receipt.strongWyvernPoison)' in queue
assert '[this, stack, spell, useSecondary, resourceOverride]' in queue
assert 'spell == SpellID(SpellID::POISON) && strongWyvernPoison' in selector
assert 'ordinary == AnimationPath::builtin("SP11_")' in selector
assert 'resourceOverride == AnimationPath::builtin(RED_CLOUD)' in selector
assert 'const auto resourceName = poisonPresentation::queueResource' in queue
for token in ('animation.pause', 'animation.verticalPosition', 'animation.transparency',
              'SpellAnimationQueue sparks', 'EAnimationEvents::HIT',
              'displaySpellAnimationQueue(spell, spell->animationInfo.hit, destinationTile, true)'):
    assert token in queue
# Select the cast alias before target effects have been applied. Cast presentation
# cannot depend on previous target poison, the current hero or a mutable prototype.
cast = queue.split('void BattleInterface::queueSpellAffectedAnimations', 1)[1].split(
    'void BattleInterface::displaySpellAnimationQueue', 1)[0]
for forbidden in ('getBonuses', 'hasActivePerk', 'getHeroType', 'ColorTransformAnimation'):
    assert forbidden not in cast
# A later tick uses the exact core poison cap, never an inactive health record.
tick = effects.split('case BonusType::POISON:', 1)[1].split('case BonusType::FEARFUL:', 1)[0]
assert 'Selector::type()(BonusType::POISON)' in tick
assert 'BonusSourceID(SpellID(SpellID::POISON))' in tick
assert 'isStrongWyvernPoison(bonus.get())' in tick
assert 'displayEffect(EBattleEffect::POISON' in tick
assert tick.count('AudioPath::builtin("POISON")') == 2
assert 'STACK_HEALTH' not in tick
assert 'paletteRemap' not in queue + effects
print('PASS: source-receipt cast alias, strict cap-marker tick, ordinary green and existing queue stages preserved')
