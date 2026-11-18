/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInterface.h"
#include "BattleAnimationClasses.h"
#include "BattleFieldController.h"
#include "BattleStacksController.h"
#include "PoisonPresentation.h"
#include "ForceLancePresentation.h"
#include "../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../GameEngine.h"
#include "media/ISoundPlayer.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/CSpell.h"

StackAttackedInfo StackAttackedInfo::fromPacket(const BattleStackAttacked & hit,
	const CStack * defender, const CStack * attacker, bool ranged)
{
	StackAttackedInfo info;
	info.defender = defender;
	info.attacker = attacker;
	info.damageDealt = hit.damageAmount;
	info.amountKilled = hit.killedAmount;
	info.replaySpellHit = hit.spellID.hasValue() && (hit.flags & BattleStackAttacked::SPELL_HIT_EFFECT);
	info.spellEffect = (hit.isSpell() || info.replaySpellHit) ? hit.spellID : SpellID(SpellID::NONE);
	info.indirectAttack = ranged;
	info.killed = hit.killed();
	info.rebirth = hit.willRebirth();
	info.cloneKilled = hit.cloneKilled();
	info.shattered = hit.shattered();
	return info;
}

void BattleInterface::displaySpellInjury(const CSpell * spell, const BattleHex & destinationTile, bool replayHit)
{
	if(!spell->getCastSound().empty())
		ENGINE->sound().playSound(spell->getCastSound());
	if(replayHit)
		displaySpellHit(spell, destinationTile);
	displaySpellEffect(spell, destinationTile);
}

void BattleInterface::queueSpellAffectedAnimations(const BattleSpellCast & receipt, const CSpell * spell)
{
	overflowSpellPresentation.remember(*this, receipt);
	const auto resourceOverride = poisonPresentation::acceptedCastOverride(receipt.spellID, receipt.strongWyvernPoison);
	const bool usesChainRay = !spell->animationInfo.ray.empty() && !receipt.affectedCres.empty();

	//queuing affect animation
	if(usesChainRay)
	{
		// only the primary (first) target gets the full affect; the rest get the trailing spark frames
		const auto & affect = spell->animationInfo.affect;
		SpellAnimationQueue sparks(affect.begin() + (affect.empty() ? 0 : 1), affect.end());

		std::vector<Point> targetPoints;
		for(size_t i = 0; i < receipt.affectedCres.size(); ++i)
		{
			auto stack = getBattle()->battleGetStackByID(receipt.affectedCres[i], false);
			if(!stack)
				continue;

			BattleHex hex = stack->getPosition();
			if(i == 0)
				addToAnimationStage(EAnimationEvents::HIT, [this, spell, hex, resourceOverride](){ displaySpellEffect(spell, hex, resourceOverride); });
			else
				addToAnimationStage(EAnimationEvents::HIT, [this, spell, sparks, hex, resourceOverride](){ displaySpellAnimationQueue(spell, sparks, hex, false, resourceOverride); });

			Point directionOffset(30, 0);
			targetPoints.push_back(stacksController->getStackPositionAtHex(hex, stack) + Point(225, 225) + (stacksController->facingRight(stack) ? -directionOffset : directionOffset));
		}

		const CStack * casterStack = getBattle()->battleGetStackByID(receipt.casterStack);
		addToAnimationStage(EAnimationEvents::HIT, [this, casterStack, targetPoints, spell](){
			stacksController->addNewAnim(new ChainLightningAnimation(*this, casterStack, targetPoints, spell));
		});
	}
	else
	{
		size_t affectedIndex = 0;
		for(auto & elem : receipt.affectedCres)
		{
			auto stack = getBattle()->battleGetStackByID(elem, false);
			assert(stack);
			if(stack)
			{
				// secondary affected targets (e.g. the sacrificed unit) use a distinct effect if the spell defines one
				bool useSecondary = affectedIndex > 0 && !spell->animationInfo.affectSecondary.empty();
				addToAnimationStage(EAnimationEvents::HIT, [this, stack, spell, useSecondary, resourceOverride](){
					if(useSecondary)
						displaySpellAnimationQueue(spell, spell->animationInfo.affectSecondary, stack->getPosition(), false, resourceOverride);
					else
						displaySpellEffect(spell, stack->getPosition(), resourceOverride);
				});
			}
			++affectedIndex;
		}
	}

}

void BattleInterface::displaySpellAnimationQueue(const CSpell * spell, const SpellAnimationQueue & q, const BattleHex & destinationTile, bool isHit,
	const AnimationPath & resourceOverride)
{
	for(const auto & animation : q)
	{
		if(animation.pause > 0)
			stacksController->addNewAnim(new DummyAnimation(*this, animation.pause));

		if (!animation.effectName.empty())
		{
			const CStack * destStack = getBattle()->battleGetStackByPos(destinationTile, false);

			if (destStack)
				stacksController->addNewAnim(new ColorTransformAnimation(*this, destStack, animation.effectName, spell ));
		}

		if(!animation.resourceName.empty())
		{
			const auto resourceName = poisonPresentation::queueResource(
				spell ? spell->id : SpellID::NONE, animation.resourceName, resourceOverride);
			int flags = 0;

			if (isHit)
				flags |= EffectAnimation::FORCE_ON_TOP;

			if (animation.verticalPosition == VerticalPosition::BOTTOM)
				flags |= EffectAnimation::ALIGN_TO_BOTTOM;

			if (!destinationTile.isValid())
				flags |= EffectAnimation::SCREEN_FILL;

			if (!destinationTile.isValid())
				stacksController->addNewAnim(new EffectAnimation(*this, resourceName, flags, animation.transparency));
			else
				stacksController->addNewAnim(new EffectAnimation(*this, resourceName, destinationTile, flags, animation.transparency));
		}
	}
}

void BattleInterface::displaySpellCast(const CSpell * spell, const BattleHex & destinationTile)
{
	if(spell)
		displaySpellAnimationQueue(spell, spell->animationInfo.cast, destinationTile, false);
}

void BattleInterface::displaySpellEffect(const CSpell * spell, const BattleHex & destinationTile,
	const AnimationPath & resourceOverride)
{
	if(spell && spell->getJsonKey() == newHorizonsSorcerySpells::FORCE_LANCE)
	{
		if(const auto * target = getBattle()->battleGetStackByPos(destinationTile, false))
			ForceLancePresentation::strike(*this, *target);
		return;
	}
	if(spell)
		displaySpellAnimationQueue(spell, spell->animationInfo.affect, destinationTile, false, resourceOverride);
}

void BattleInterface::displaySpellHit(const CSpell * spell, const BattleHex & destinationTile)
{
	if(spell)
		displaySpellAnimationQueue(spell, spell->animationInfo.hit, destinationTile, true);
}

