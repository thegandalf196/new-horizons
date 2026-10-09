/*
 * NewHorizonsBattlecraft.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBattlecraft.h"

#include "CUnitState.h"
#include "NewHorizonsCombatSkills.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/Bonus.h"

namespace newHorizonsBattlecraft
{
int rank(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	try
	{
		return std::clamp(hero->getPerkSkillRank("new-horizons:battlecraft"), 0, 3);
	}
	catch(const std::exception &)
	{
		return 0;
	}
}

int rankPercent(int value)
{
	return std::clamp(value, 0, 3) * 5;
}

bool hasEntrench(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.entrench");
}

bool hasOverwatch(const CGHeroInstance * hero)
{
	return rank(hero) >= 1
		&& hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.overwatch");
}

bool overwatchReady(const CGHeroInstance * hero, const battle::Unit * shooter, int32_t round)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(shooter);
	return round >= 1 && hasOverwatch(hero) && state && shooter->alive() && !shooter->isGhost()
		&& shooter->canMove() && shooter->isShooter() && shooter->canShoot()
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(shooter)
		&& state->battlecraftOverwatchReadyRound == round && state->battlecraftOverwatchUsedRound != round;
}

int overwatchRange(const battle::Unit * shooter)
{
	if(!shooter)
		return 0;
	const auto limited = shooter->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
	return limited ? std::clamp(static_cast<int>(limited->val), 0, OVERWATCH_RANGE) : OVERWATCH_RANGE;
}

bool hasBattlefieldMastery(const CGHeroInstance * hero)
{
	return rank(hero) >= 3
		&& hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.battlefieldMastery");
}

bool canAwardBattlefieldMastery(const CGHeroInstance * hero, const battle::Unit * stack,
	int32_t round, int32_t previousAwardRound, BattlecraftMasteryAction action)
{
	if(round < 1 || previousAwardRound >= round || !hasBattlefieldMastery(hero)
		|| !stack || !stack->alive() || stack->isGhost()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)
		|| stack->unitSlot() == SlotID::WAR_MACHINES_SLOT)
		return false;

	const auto * state = dynamic_cast<const battle::CUnitState *>(stack);
	if(!state)
		return false;

	switch(action)
	{
	case BattlecraftMasteryAction::WAIT:
		return state->waitedThisTurn;
	case BattlecraftMasteryAction::DEFEND:
		return stack->defended();
	}
	return false;
}

int waitDamagePercent(const CGHeroInstance * hero, const battle::CUnitState * stack)
{
	const int bonus = rankPercent(rank(hero));
	return stack && stack->battlecraftWaitMasteryDoubled && stack->battlecraftWaitBonusAvailable()
		? bonus * 2 : bonus;
}

int defendReductionPercent(const CGHeroInstance * hero, const battle::CUnitState * stack)
{
	const int rankBonus = rankPercent(rank(hero));
	const int reduction = stack && stack->defended() && stack->battlecraftDefendMasteryDoubled
		? rankBonus * 2 : rankBonus;
	return reduction > 0 ? reduction + (hasEntrench(hero) ? 5 : 0) : 0;
}

bool hasPreemptiveStrike(const CGHeroInstance * hero)
{
	return rank(hero) >= 2
		&& hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.preEmptiveStrike");
}

int preemptiveStrikeDamagePercent(const CGHeroInstance * defenderHero,
	const battle::Unit * defender, int32_t round)
{
	if(round < 0 || !hasPreemptiveStrike(defenderHero) || !defender || !defender->canMove()
		|| !defender->defended() || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender))
		return 0;
	const auto * state = dynamic_cast<const battle::CUnitState *>(defender);
	if(!state || state->battlecraftPreemptiveStrikeRound == round)
		return 0;
	return 50;
}

int defendReductionPercent(const CGHeroInstance * hero)
{
	const int value = rankPercent(rank(hero));
	return value > 0 ? value + (hasEntrench(hero) ? 5 : 0) : 0;
}

int delayedActivationMovementBonus(const CGHeroInstance * hero, const battle::CUnitState * stack,
	BattleUnitTurnReason reason)
{
	if(!hero || !stack || reason != BattleUnitTurnReason::TURN_QUEUE
		|| !stack->alive() || stack->isGhost()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)
		|| stack->unitSlot() == SlotID::WAR_MACHINES_SLOT
		|| !stack->waiting || !stack->waitedThisTurn)
		return 0;

	return hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.reserve") ? 2 : 0;
}
}
