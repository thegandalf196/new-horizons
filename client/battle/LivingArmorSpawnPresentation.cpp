/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleStacksController.h"
#include "BattleAnimationClasses.h"
#include "BattleInterface.h"
#include "CreatureAnimation.h"
#include "render/Colors.h"
#include "../../lib/CStack.h"
#include "../../lib/spells/CSpell.h"

void BattleStacksController::stageAddedStackAppearance(const CStack * stack, bool instant)
{
	if(instant)
		return;

	auto animation = stackAnimation.at(stack->unitId());
	if(stack->unitType()->getJsonKey() == "new-horizons:livingArmor" && !stack->isClone()
		&& animation->framesInGroup(ECreatureAnimType::RESURRECTION) > 0)
	{
		// The engine derives RESURRECTION from the original death frames in
		// reverse order. Hold its bone-pile frame before the cast's HIT stage.
		// A living pose group avoids treating the summon as a dead stack when
		// its ordinary initial wounded-HP update arrives before that stage.
		animation->setType(ECreatureAnimType::RESURRECTION);
		animation->playUntil(0);
		owner.addToAnimationStage(EAnimationEvents::HIT, [this, stack, animation]()
		{
			animation->playUntil(static_cast<size_t>(-1));
			addNewAnim(new ResurrectionAnimation(owner, stack));
		});
		// Do not install summonFadeIn: its neutral color overrides the blue
		// identity tint until the fade ends. The rising bones appear blue first.
		return;
	}

	// Other summons and clones retain their existing fade and color behavior.
	setStackColorFilter(Colors::TRANSPARENCY, 0, stack, nullptr, true);
	owner.addToAnimationStage(EAnimationEvents::HIT, [this, stack]()
	{
		addNewAnim(new ColorTransformAnimation(owner, stack, "summonFadeIn", nullptr));
		if(stack->isClone())
			addNewAnim(new ColorTransformAnimation(owner, stack, "cloning", SpellID(SpellID::CLONE).toSpell()));
	});
}
