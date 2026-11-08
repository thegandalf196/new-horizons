/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ThunarRangedPresentation.h"
#include "CreatureAnimation.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/NewHorizonsCreatureSpecialtyCapabilities.h"

namespace thunarRangedPresentation
{
namespace
{
Admission admission(const CStack & stack)
{
	if(!newHorizonsCreatureSpecialtyCapabilities::stonecallerRanged(stack))
		return {};
	static const auto earth = CreatureID(CreatureID::decode("core:earthElemental"));
	static const auto magma = CreatureID(CreatureID::decode("core:magmaElemental"));
	if(stack.creatureId() == earth)
		return {true, Family::EARTH};
	if(stack.creatureId() == magma)
		return {true, Family::MAGMA};
	return {};
}

std::optional<Direction> directionOf(ECreatureAnimType group)
{
	switch(group)
	{
		case ECreatureAnimType::SHOOT_UP:
		case ECreatureAnimType::ATTACK_UP: return Direction::UP;
		case ECreatureAnimType::SHOOT_FRONT:
		case ECreatureAnimType::ATTACK_FRONT: return Direction::FRONT;
		case ECreatureAnimType::SHOOT_DOWN:
		case ECreatureAnimType::ATTACK_DOWN: return Direction::DOWN;
		default: return std::nullopt;
	}
}

std::optional<Pose> poseFor(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType group)
{
	const auto direction = directionOf(group);
	if(!direction)
		return std::nullopt;
	constexpr std::array shoots{ECreatureAnimType::SHOOT_UP, ECreatureAnimType::SHOOT_FRONT, ECreatureAnimType::SHOOT_DOWN};
	constexpr std::array attacks{ECreatureAnimType::ATTACK_UP, ECreatureAnimType::ATTACK_FRONT, ECreatureAnimType::ATTACK_DOWN};
	const auto index = static_cast<std::size_t>(*direction);
	return fallbackPose(admission(stack), *direction,
		animation.framesInGroup(shoots[index]), animation.framesInGroup(attacks[index]));
}
}

ECreatureAnimType shootingGroup(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType requested)
{
	const auto fallback = poseFor(stack, animation, requested);
	return fallback ? fallback->group : requested;
}

std::optional<std::size_t> climaxForShot(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType group)
{
	const auto fallback = poseFor(stack, animation, group);
	return fallback && group == fallback->group ? std::optional<std::size_t>(fallback->climaxFrame) : std::nullopt;
}

std::optional<Point> originForShot(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType group, bool facingRight)
{
	const auto fallback = poseFor(stack, animation, group);
	const auto facts = admission(stack);
	const auto direction = directionOf(group);
	if(!fallback || !facts.family || !direction || group != fallback->group)
		return std::nullopt;
	return provisionalOrigin(*facts.family, *direction, facingRight, animation.getWidth());
}
}
