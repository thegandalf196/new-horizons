/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include <algorithm>
#include "NewHorizonsDreadCavalry.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "IBattleState.h"
#include "NewHorizonsFrozen.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsDreadCavalry.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsDreadCavalry
{
bool abandonsPendingOnActorHandoff(const IBattleInfo & battle, uint32_t nextActor,
	BattleUnitTurnReason reason)
{
	const auto previous = battle.getActiveStackID();
	if(previous < 0 || static_cast<uint32_t>(previous) == nextActor
		|| reason == BattleUnitTurnReason::CRISIS_ORDER)
		return false;
	return std::ranges::none_of(battle.getCrisisCommandState().returns,
		[previous](const auto & frame) { return frame.originalActor == previous; });
}
std::optional<battle::DreadCavalryState> grantAfterMelee(const CBattleInfoCallback & battle,
	const battle::Unit * source, uint32_t firstVictim, BattleSide originalController,
	int remaining, int sharedCap, int vanish)
{
	if(firstVictim == battle::DreadCavalryState::NO_VICTIM)
		return std::nullopt;
	const auto percent = entitlement(battle, source);
	const auto * state = dynamic_cast<const battle::CUnitState *>(source);
	const auto * victim = battle.battleGetUnitByID(firstVictim);
	if(!percent || !state || state->dreadCavalryState.usedThisActivation || !victim
		|| source->unitId() == firstVictim
		|| originalController != battle.playerToSide(battle.battleGetActionController(source)))
		return std::nullopt;
	battle::DreadCavalryState result;
	result.arm(firstVictim, originalController, remaining, *percent, sharedCap, vanish);
	return result;
}
bool acceptedBlowStillLegal(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * target, uint32_t originalActor, const battle::DreadCavalryState & accepted)
{
	if(!accepted.pending)
		return true;
	const auto percent = entitlement(battle, source);
	return source && target && target->alive() && percent && *percent == accepted.damagePercent
		&& source->unitId() == originalActor && target->unitId() != accepted.victimId
		&& accepted.controller == battle.playerToSide(battle.battleGetActionController(source))
		&& battle.battleMatchOwner(source, target) && battle.battleMatchActionController(source, target);
}
BattleUnitTurnReason continuationReason(const battle::Unit * source)
{
	return hasRawPending(source) ? BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION
		: BattleUnitTurnReason::PURSUIT_CONTINUATION;
}
bool family(const battle::Unit * source)
{
	if(!source || !source->creatureId().hasValue())
		return false;
	const auto * type = source->creatureId().toCreature();
	return type && (type->getJsonKey() == "core:blackKnight" || type->getJsonKey() == "core:dreadKnight");
}
std::optional<int> entitlement(const CBattleInfoCallback & battle, const battle::Unit * source)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(source);
	if(!state || !family(source) || !source->alive() || source->isGhost() || !source->canMove()
		|| source->isTimeStopped() || newHorizonsFrozen::isFrozen(*source)
		|| state->armorerLastStandEndedActivation)
		return std::nullopt;
	const auto * hero = battle.battleGetOwnerHero(source);
	return newHorizonsHeroes::hasDreadCavalry(hero)
		? newHorizonsHeroes::dreadCavalryDamagePercent(hero->getPrimaryGrowthRules()) : std::nullopt;
}
bool hasRawPending(const battle::Unit * source)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(source);
	return state && state->dreadCavalryState.pending;
}
bool hasPending(const CBattleInfoCallback & battle, const battle::Unit * source, bool requireActive)
{
	if(!hasRawPending(source) || !battle.getBattle())
		return false;
	const auto * unit = dynamic_cast<const battle::CUnitState *>(source);
	const auto & state = unit->dreadCavalryState;
	if(requireActive && (battle.getBattle()->getActiveStackID() < 0
		|| source->unitId() != static_cast<uint32_t>(battle.getBattle()->getActiveStackID())))
		return false;
	const auto percent = entitlement(battle, source);
	return state.usedThisActivation && state.victimId != battle::DreadCavalryState::NO_VICTIM
		&& state.remainingMovement >= 0 && percent && *percent == state.damagePercent
		&& state.controller == battle.playerToSide(battle.battleGetActionController(source));
}
bool permitsTarget(const CBattleInfoCallback & battle, const battle::Unit * source, const battle::Unit * target)
{
	if(!hasRawPending(source))
		return true;
	const auto * unit = dynamic_cast<const battle::CUnitState *>(source);
	return hasPending(battle, source) && target && target->alive() && !target->isGhost()
		&& source->unitId() != target->unitId() && target->unitId() != unit->dreadCavalryState.victimId
		&& battle.battleMatchOwner(source, target) && battle.battleMatchActionController(source, target);
}
int outputPercent(const CBattleInfoCallback & battle, const battle::Unit * source)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(source);
	return hasPending(battle, source) ? state->dreadCavalryState.damagePercent : 100;
}
int movementRemaining(const CBattleInfoCallback & battle, const battle::Unit * source)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(source);
	return hasPending(battle, source)
		? static_cast<int>(std::min(source->getMovementRange(0),
			static_cast<uint32_t>(state->dreadCavalryState.remainingMovement))) : 0;
}
}
