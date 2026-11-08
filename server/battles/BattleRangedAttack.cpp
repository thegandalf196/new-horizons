/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattleRangedFollowup.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../lib/battle/NewHorizonsMachineSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/gameState/CGameState.h"
namespace
{
bool hasLegalHostileBallistaShot(const CBattleInfoCallback & battle, const battle::Unit * shooter)
{
	if(!shooter || !shooter->alive() || shooter->isGhost() || shooter->isTimeStopped() || !shooter->canMove()
		|| !shooter->isBallista() || shooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		|| !battle.battleCanShoot(shooter))
		return false;

	for(const auto * target : battle.battleAliveUnits())
	{
		if(!target || !target->alive() || target->isGhost() || battle.battleMatchOwner(shooter, target, true))
			continue;
		for(const auto & hex : target->getHexes())
			if(hex.isValid() && battle.battleCanShoot(shooter, hex))
				return true;
	}
	return false;
}
}

bool BattleActionProcessor::armorerLastStandEndedActivation(const CStack * stack)
{
	if(!stack)
		return false;
	const auto state = stack->acquireState();
	return state && state->armorerLastStandEndedActivation;
}


bool BattleActionProcessor::performPendingRangedFollowUp(const CBattleInfoCallback & battle, const CStack * stack,
	const CStack * destinationStack, BattleHex destination, BattleSide perfectMomentSide, bool allowPursuitContinuation)
{
	// This is its own player-selected attack action: do not route through the
	// generic multi-attack loop, which would repeat this target automatically.
	breakSanctuary(battle, stack);
	auto rainOfArrows = beginRainOfArrows(battle, stack, destinationStack);
	RelentlessAssaultActionContext relentlessAssault;
	const auto battleId = battle.getBattle()->getBattleID();
	const auto * nativeOwner = gameHandler->gameState().getBattle(battleId);
	const auto sourceId = stack->unitId();
	const auto targetId = destinationStack->unitId();
	makeAttack(battle, stack, destinationStack,
		{.targetHex = destination, .attackIndex = 1, .first = false, .ranged = true,
			.perfectMomentSide = perfectMomentSide}, nullptr, &relentlessAssault, &rainOfArrows);
	const auto * current = gameHandler->gameState().getBattle(battleId);
	if(!current || current != nativeOwner) return true;
	stack = current->battleGetStackByID(sourceId, false);
	destinationStack = current->battleGetStackByID(targetId, false);
	if(!stack || !destinationStack) return true;
	// The damage callback above observes the earned percentage. Clear only
	// after that accepted attack has completed its shared damage calculation.
	battleRangedFollowup::publish(battle, *gameHandler, stack, 0);
	battleRangedFollowup::log(battle, *gameHandler, stack,
		"%s fires the Master Gunner follow-up shot.");

	BonusList attackerBonusesToRemove = *stack->getAllBonuses(Bonus::untilAfterAttackSequence);
	BonusList defenderBonusesToRemove = *destinationStack->getAllBonuses(Bonus::untilAfterAttackSequence);
	performOrdinaryRangedRetaliation(battle, destinationStack, stack);
	current = gameHandler->gameState().getBattle(battleId);
	if(!current || current != nativeOwner) return true;
	stack = current->battleGetStackByID(sourceId, false);
	destinationStack = current->battleGetStackByID(targetId, false);
	if(!stack || !destinationStack) return true;
	removeBonuses(battle, stack, attackerBonusesToRemove);
	removeBonuses(battle, destinationStack, defenderBonusesToRemove);
	resolveRainOfArrows(battle, stack, rainOfArrows);
	finalizeOpportunistSequence(battle, stack, stack->getMovementRange(0), allowPursuitContinuation);
	return true;
}

bool BattleActionProcessor::doShootActionCore(const CBattleInfoCallback & battle, const BattleAction & ba,
	bool allowPursuitContinuation)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	const bool pendingFollowUp = battle.battleHasPendingRangedFollowUp(stack);
	battle::Target target = ba.getTarget(&battle);

	if (!canStackAct(battle, stack))
		return false;
	const auto perfectMomentSide = battle.playerToSide(battle.battleGetOwner(stack));

	if(target.empty())
	{
		gameHandler->complain("Destination required for shot action.");
		return false;
	}

	auto destination = target.at(0).hexValue;

	const CStack * destinationStack = battle.battleGetStackByPos(destination);
	const auto nativeBattleId = battle.getBattle()->getBattleID();
	const auto * nativeBattleOwner = gameHandler->gameState().getBattle(nativeBattleId);
	const auto sourceId = stack->unitId();
	const auto targetId = destinationStack ? std::optional{destinationStack->unitId()} : std::nullopt;
	const auto reacquire = [&]()
	{
		const auto * current = gameHandler->gameState().getBattle(nativeBattleId);
		if(!current || current != nativeBattleOwner) return false;
		stack = current->battleGetStackByID(sourceId, false);
		destinationStack = targetId ? current->battleGetStackByID(*targetId, false) : nullptr;
		return stack && (!targetId || destinationStack);
	};

	if (!battle.battleCanShootAction(stack, destination))
	{
		gameHandler->complain("Cannot shoot!");
		return false;
	}

	const bool emptyTileAreaAttack = battle.battleCanTargetEmptyHex(stack);

	if (!destinationStack && !emptyTileAreaAttack)
	{
		gameHandler->complain("No target to shoot!");
		return false;
	}
	if(pendingFollowUp && (!destinationStack || !destinationStack->alive() || destinationStack->isGhost()
		|| battle.battleMatchActionController(stack, destinationStack, true)))
	{
		gameHandler->complain("Master Gunner follow-up must target a living enemy creature.");
		return false;
	}
	if(pendingFollowUp && !battle.battleCanTakeRangedFollowUp(stack))
	{
		gameHandler->complain("The Master Gunner follow-up is no longer usable.");
		return false;
	}

	if(pendingFollowUp)
		return performPendingRangedFollowUp(battle, stack, destinationStack, destination, perfectMomentSide, allowPursuitContinuation);

	// A ranged attack, including a valid area shot, is an offensive action.
	breakSanctuary(battle, stack);
	auto rainOfArrows = beginRainOfArrows(battle, stack, destinationStack);
	RelentlessAssaultActionContext relentlessAssault;

	int64_t sequenceKills = 0;
	bool firstStrike = false;
	if(!emptyTileAreaAttack)
	{
		static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll).Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged));
		firstStrike = destinationStack->hasBonus(firstStrikeSelector) && !destinationStack->hasBonusOfType(BonusType::NOT_ACTIVE);
	}

	if (!firstStrike)
	{
		makeAttack(battle, stack, destinationStack, {.targetHex = destination, .first = true, .ranged = true, .perfectMomentSide = perfectMomentSide, .hostileCreatureKills = &sequenceKills}, nullptr, &relentlessAssault, &rainOfArrows);
		if(!reacquire()) return true;
	}

	BonusList attackerBonusesToRemove = *stack->getAllBonuses(Bonus::untilAfterAttackSequence);	//they need to be gathered here since bonuses with this duration added during attack (like blind) should not be removed
	BonusList defenderBonusesToRemove;
	if (destinationStack)
		defenderBonusesToRemove = *destinationStack->getAllBonuses(Bonus::untilAfterAttackSequence);

	//ranged counterattack
	if(!emptyTileAreaAttack)
	{
		performOrdinaryRangedRetaliation(battle, destinationStack, stack);
		if(!reacquire()) return true;
	}
	//allow more than one additional attack

	int totalRangedAttacks = stack->getTotalAttacks(true);

	//TODO: move to CUnitState
	const auto * attackingHero = battle.battleGetFightingHero(ba.side);
	if(attackingHero)
	{
		totalRangedAttacks += attackingHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(stack->creatureId()));
	}

	for(int i = firstStrike ? 0:1; i < totalRangedAttacks; ++i)
	{
		if(stack->alive()
			&& (emptyTileAreaAttack || destinationStack->alive())
		&& stack->shots.canUse()
		&& !armorerLastStandEndedActivation(stack))
		{
			// when the defender strikes first the opening shot above is skipped and this loop makes
			// it instead, so the shot that abilities fire on is the first one this loop makes
			makeAttack(battle, stack, destinationStack, {.targetHex = destination, .attackIndex = i, .first = i == 0, .ranged = true, .perfectMomentSide = i == 0 ? perfectMomentSide : BattleSide::NONE, .hostileCreatureKills = &sequenceKills}, nullptr, &relentlessAssault, &rainOfArrows);
			if(!reacquire()) return true;
		}
	}

	if(destinationStack && sequenceKills > 0)
		performRelentlessBarrage(battle, stack, destinationStack, destination, totalRangedAttacks, relentlessAssault, rainOfArrows);
	if(!reacquire()) return true;

	removeBonuses(battle, stack, attackerBonusesToRemove);
	removeBonuses(battle, destinationStack, defenderBonusesToRemove);
	resolveRainOfArrows(battle, stack, rainOfArrows);

	const auto * masterGunnerHero = battle.battleGetOwnerHero(stack);
	const auto state = stack->acquireState();
	if(destinationStack && stack->alive() && !stack->isGhost()
		&& !armorerLastStandEndedActivation(stack)
		&& !battle.battleMatchOwner(stack, destinationStack, true)
		&& stack->isBallista() && !stack->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		&& state && state->rangedFollowUpDamagePercent == 0
		&& masterGunnerHero && masterGunnerHero->hasActivePerk(
			"new-horizons:warMachines", "new-horizons:warMachines.masterGunner")
		&& hasLegalHostileBallistaShot(battle, stack))
	{
		battleRangedFollowup::grant(battle, *gameHandler, stack, masterGunnerHero, newHorizonsMachineSpecialties::MASTER_GUNNER_ORDINARY_FOLLOW_UP_DAMAGE_PERCENT);
	}
	if(!allowPursuitContinuation || !battle.battleHasPendingRangedFollowUp(stack))
		finalizeOpportunistSequence(battle, stack, stack->getMovementRange(0), allowPursuitContinuation);

	return true;
}

