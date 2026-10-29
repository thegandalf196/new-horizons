/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleStacksController.h"
#include "BattleInterface.h"
#include "BattleAnimationClasses.h"
#include "CreatureAnimation.h"
#include "AerialSupremacyReturnPresentation.h"
#include "../../lib/CStack.h"

void BattleStacksController::stageAerialExcursion(const StackAttackInfo & info)
{
	if(!info.aerialExcursion)
		return;
	const auto excursion = *info.aerialExcursion;
	const auto * actor = info.attacker;
	// Presentation consumes the validated packet; it never changes CStack placement.
	if(!actor || actor->unitId() != excursion.actor || actor->getPosition() != excursion.origin)
		throw std::runtime_error("Aerial presentation references a different actor position");
	if(!aerialSupremacyPresentation::stageExcursionFacing(excursion.origin, excursion.landing, [&]()
	{
		owner.addToAnimationStage(EAnimationEvents::BEFORE_HIT, [this, actor, excursion, tile = info.tile]()
		{
			if(shouldRotate(actor, excursion.landing, tile))
				addNewAnim(new ReverseAnimation(owner, actor, excursion.landing));
		});
	}))
		return;
	stackAmountBoxHidden.insert(actor->unitId());
	owner.addToAnimationStage(EAnimationEvents::MOVE_START, [this, actor]()
	{
		addNewAnim(new MovementStartAnimation(owner, actor));
	});
	owner.addToAnimationStage(EAnimationEvents::MOVEMENT, [this, actor, excursion]()
	{
		addNewAnim(new MovementAnimation(owner, actor, BattleHexArray{excursion.landing},
			BattleHex::getDistance(excursion.origin, excursion.landing), excursion.origin));
	});
	owner.addToAnimationStage(EAnimationEvents::MOVE_END, [this, actor, excursion]()
	{
		addNewAnim(new MovementEndAnimation(owner, actor, excursion.landing));
	});
	owner.addToAnimationStage(EAnimationEvents::AERIAL_RETURN_START, [this, actor]()
	{
		const auto animation = stackAnimation.find(actor->unitId());
		aerialSupremacyPresentation::animateReturn(animation == stackAnimation.end() ? nullptr : animation->second.get(), [&]()
		{
			addNewAnim(new MovementStartAnimation(owner, actor));
		});
	});
	owner.addToAnimationStage(EAnimationEvents::AERIAL_RETURN_MOVE, [this, actor, excursion]()
	{
		const auto animation = stackAnimation.find(actor->unitId());
		aerialSupremacyPresentation::animateReturn(animation == stackAnimation.end() ? nullptr : animation->second.get(), [&]()
		{
			addNewAnim(new MovementAnimation(owner, actor, BattleHexArray{excursion.origin},
				BattleHex::getDistance(excursion.landing, excursion.origin), excursion.landing));
		});
	});
	owner.addToAnimationStage(EAnimationEvents::AERIAL_RETURN_END, [this, actor, excursion]()
	{
		stackAmountBoxHidden.erase(actor->unitId());
		const auto animation = stackAnimation.find(actor->unitId());
		aerialSupremacyPresentation::finishReturn(animation == stackAnimation.end() ? nullptr : animation->second.get(),
			getStackPositionAtHex(excursion.origin, actor), [&]()
		{
			addNewAnim(new MovementEndAnimation(owner, actor, excursion.origin));
		});
	});
}
