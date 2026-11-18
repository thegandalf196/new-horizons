/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForceLancePresentation.h"
#include "BattleAnimationClasses.h"
#include "BattleFieldController.h"
#include "BattleInterface.h"
#include "BattleRenderer.h"
#include "BattleStacksController.h"
#include "../GameEngine.h"
#include "media/ISoundPlayer.h"
#include "render/CAnimation.h"
#include "render/Canvas.h"
#include "render/IRenderHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/spells/NewHorizonsSorcerySpells.h"

namespace
{
constexpr uint32_t RELEASE_MILLISECONDS = 250;

bool pinned(const CStack & stack)
{
	if(!stack.alive() || stack.isGhost())
		return false;
	for(const auto & bonus : *stack.getBonusesOfType(BonusType::BIND_EFFECT))
		if(bonus->source == BonusSource::SPELL_EFFECT && bonus->duration == BonusDuration::N_TURNS
			&& bonus->turnsRemain > 0 && bonus->stacking == newHorizonsSorcerySpells::FORCE_LANCE
			&& bonus->sid.toString() == newHorizonsSorcerySpells::FORCE_LANCE)
			return true;
	return false;
}

Point origin(BattleInterface & owner, const CStack & target)
{
	const auto tile = owner.fieldController->hexPositionLocal(target.getPosition());
	Point foot(tile.x + tile.w / 2, tile.y + tile.h);
	if(target.doubleWide())
		foot.x += (target.unitSide() == BattleSide::ATTACKER ? -1 : 1) * tile.w / 2;
	// All four selected 160px canvases share the same fixed-world spear-tip anchor.
	return foot - Point(110, 138);
}

std::shared_ptr<CAnimation> load(const char * key)
{
	return ENGINE->renderHandler().loadAnimation(AnimationPath::builtin(key), EImageBlitMode::SIMPLE);
}
}

void ForceLancePresentation::strike(BattleInterface & owner, const CStack & target)
{
	ENGINE->sound().playSound(AudioPath::builtin("nh_force_lance_strike.wav"));
	owner.stacksController->addNewAnim(new EffectAnimation(owner,
		AnimationPath::builtin("newHorizons/forceLance/strikeImpact"), origin(owner, target),
		target.getPosition()));
}

void ForceLancePresentation::refresh(BattleInterface & owner)
{
	std::map<uint32_t, Pin> current;
	for(const auto * stack : owner.getBattle()->battleGetAllStacks(false))
		if(pinned(*stack))
			current.emplace(stack->unitId(), Pin{origin(owner, *stack), stack->getPosition()});

	for(const auto & [id, previous] : held)
	{
		if(current.contains(id))
			continue;
		const auto * stack = owner.getBattle()->battleGetStackByID(id, false);
		if(stack && stack->alive() && !stack->isGhost())
			releasing.insert_or_assign(id, Release{previous, 0, {}});
	}
	std::erase_if(releasing, [&owner, &current](const auto & entry)
	{
		const auto * stack = owner.getBattle()->battleGetStackByID(entry.first, false);
		return current.contains(entry.first) || !stack || !stack->alive() || stack->isGhost();
	});
	held = std::move(current);
}

void ForceLancePresentation::tick(uint32_t elapsed)
{
	for(auto & [id, release] : releasing)
		release.elapsed += release.phase.elapsedForAdvance(elapsed);
	std::erase_if(releasing, [](const auto & entry) { return entry.second.elapsed >= RELEASE_MILLISECONDS; });
}

void ForceLancePresentation::collect(BattleInterface & owner, BattleRenderer & renderer)
{
	refresh(owner);
	if(!held.empty() && !holdImage)
		holdImage = load("newHorizons/forceLance/hold");
	if(!releasing.empty() && !releaseImage)
		releaseImage = load("newHorizons/forceLance/release");
	const auto draw = [&renderer](const Pin & pin, const std::shared_ptr<CAnimation> & animation)
	{
		const auto image = animation ? animation->getImage(0, 0, true) : nullptr;
		if(image)
			renderer.insert(EBattleFieldLayer::EFFECTS, pin.tile, [image, position = pin.origin](Canvas & canvas)
			{
				canvas.draw(image, position);
			});
	};
	for(const auto & [id, pin] : held)
		draw(pin, holdImage);
	for(auto & [id, release] : releasing)
	{
		draw(release.pin, releaseImage);
		release.phase.onRenderAttempt();
	}
}
